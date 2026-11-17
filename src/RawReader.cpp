#include "RawReader.h"

#include <QFileInfo>

#ifdef ONION_HAVE_LIBRAW
#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTransform>

#include <libraw/libraw.h>

#include <limits>
#include <memory>

namespace {

// Opens the file and parses its header. LibRaw reads it through its own buffered stream, so
// header-only calls (describe, dateTaken, the embedded preview) read little of a large file.
int openRaw(LibRaw &raw, const QString &path)
{
#ifdef Q_OS_WIN
    return raw.open_file(path.toStdWString().c_str());
#else
    return raw.open_file(QFile::encodeName(path).constData());
#endif
}

QImage develop(LibRaw &raw, bool thumbnail, QString *error)
{
    auto &params = raw.imgdata.params;
    params.half_size = thumbnail;
    params.use_camera_wb = 1;
    params.output_color = 1; // sRGB primaries
    params.gamm[0] = 1.0 / 2.4;
    params.gamm[1] = 12.92;
    int result = raw.unpack();
    if (result == LIBRAW_SUCCESS) {
        result = raw.dcraw_process();
    }
    QImage image;
    if (result == LIBRAW_SUCCESS) {
        int width = 0;
        int height = 0;
        int channels = 0;
        int bits = 0;
        raw.get_mem_image_format(&width, &height, &channels, &bits);
        if (bits == 8 && (channels == 1 || channels == 3)) {
            image = QImage(width, height, channels == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
            if (!image.isNull()) {
                result = raw.copy_mem_image(image.bits(), int(image.bytesPerLine()), 0);
            }
        }
    }
    if (result != LIBRAW_SUCCESS || image.isNull()) {
        if (error) {
            *error = result == LIBRAW_SUCCESS ? QStringLiteral("Could not allocate or render the raw image.")
                                              : QString::fromLocal8Bit(LibRaw::strerror(result));
        }
        return {};
    }
    image.setColorSpace(QColorSpace::SRgb);
    return image;
}

QImage orientPreview(const QImage &image, int flip)
{
    // Map stored pixels to the viewer using LibRaw's transpose and reflection flags.
    const int x = (flip & 1) ? -1 : 1;
    const int y = (flip & 2) ? -1 : 1;
    return image.transformed((flip & 4) ? QTransform(0, x, y, 0, 0, 0)
                                       : QTransform(x, 0, 0, y, 0, 0));
}

QImage embeddedPreview(LibRaw &raw, const QSize &size)
{
    if (raw.unpack_thumb() != LIBRAW_SUCCESS) {
        return {};
    }
    const std::unique_ptr<libraw_processed_image_t, decltype(&LibRaw::dcraw_clear_mem)>
        preview(raw.dcraw_make_mem_thumb(), &LibRaw::dcraw_clear_mem);
    if (!preview || preview->data_size > unsigned(std::numeric_limits<int>::max())) {
        return {};
    }
    if (preview->type == LIBRAW_IMAGE_JPEG) {
        QBuffer buffer;
        buffer.setData(QByteArray::fromRawData(reinterpret_cast<const char *>(preview->data), preview->data_size));
        QImageReader reader(&buffer, QByteArrayLiteral("jpeg"));
        reader.setAutoTransform(true);
        const QSize dimensions = reader.size();
        if (dimensions.isValid() && (dimensions.width() > size.width() || dimensions.height() > size.height())) {
            reader.setScaledSize(dimensions.scaled(size, Qt::KeepAspectRatio));
        }
        return reader.read();
    }
    if (preview->type == LIBRAW_IMAGE_BITMAP && preview->bits == 8
        && (preview->colors == 1 || preview->colors == 3)) {
        const int stride = preview->width * preview->colors;
        if (qint64(stride) * preview->height <= preview->data_size) {
            const QImage image(preview->data, preview->width, preview->height, stride,
                               preview->colors == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
            return orientPreview(image.copy(), raw.imgdata.sizes.flip);
        }
    }
    return {};
}

} // namespace
#endif

namespace Raw {

bool isAvailable()
{
#ifdef ONION_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}

const QStringList &suffixes()
{
    // Sony, Canon, Nikon, Adobe, Fujifilm, Olympus/OM, Panasonic, Leica, Pentax, Samsung,
    // Hasselblad, Phase One, Epson, Kodak, Minolta and Leaf.
    static const QStringList list = {
        QStringLiteral("arw"), QStringLiteral("srf"), QStringLiteral("sr2"),
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("crw"),
        QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("dng"),
        QStringLiteral("raf"), QStringLiteral("orf"), QStringLiteral("rw2"),
        QStringLiteral("rwl"), QStringLiteral("pef"), QStringLiteral("srw"),
        QStringLiteral("3fr"), QStringLiteral("fff"), QStringLiteral("iiq"),
        QStringLiteral("erf"), QStringLiteral("kdc"), QStringLiteral("dcr"),
        QStringLiteral("mrw"), QStringLiteral("mos"),
    };
    return list;
}

bool isRawFile(const QString &filePath)
{
    return suffixes().contains(QFileInfo(filePath).suffix().toLower());
}

QImage read(const QString &filePath, QString *error)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    const int result = openRaw(raw, filePath);
    if (result == LIBRAW_SUCCESS) {
        return develop(raw, false, error);
    }
    if (error) {
        *error = QString::fromLocal8Bit(LibRaw::strerror(result));
    }
#else
    Q_UNUSED(filePath);
    if (error) {
        *error = QStringLiteral("Built without LibRaw (camera raw support).");
    }
#endif
    return {};
}

QImage readThumbnail(const QString &filePath, const QSize &size)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    if (openRaw(raw, filePath) == LIBRAW_SUCCESS) {
        const QImage preview = embeddedPreview(raw, size);
        if (!preview.isNull()) {
            return preview;
        }
        // A failed thumbnail extraction can reset LibRaw's state.
        if (openRaw(raw, filePath) == LIBRAW_SUCCESS) {
            return develop(raw, true, nullptr);
        }
    }
#else
    Q_UNUSED(filePath);
    Q_UNUSED(size);
#endif
    return {};
}

QList<QPair<QString, QString>> describe(const QString &filePath)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    const int result = openRaw(raw, filePath);
    if (result != LIBRAW_SUCCESS) {
        return {{QStringLiteral("Error"), QString::fromLocal8Bit(LibRaw::strerror(result))}};
    }
    const auto &data = raw.imgdata;
    QList<QPair<QString, QString>> entries;
    const unsigned version = data.idata.dng_version;
    if (version != 0) {
        entries.append({QStringLiteral("DNG version"), QStringLiteral("%1.%2.%3.%4")
                            .arg(version >> 24).arg((version >> 16) & 255).arg((version >> 8) & 255).arg(version & 255)});
    }
    const QString camera = QStringLiteral("%1 %2").arg(QString::fromLatin1(data.idata.make),
                                                       QString::fromLatin1(data.idata.model)).trimmed();
    if (!camera.isEmpty()) {
        entries.append({QStringLiteral("Camera"), camera});
    }
    const QString lens = QString::fromLatin1(data.lens.Lens).trimmed();
    if (!lens.isEmpty()) {
        entries.append({QStringLiteral("Lens"), lens});
    }
    const auto &other = data.other;
    if (other.timestamp > 0) {
        entries.append({QStringLiteral("Date taken"),
                        QDateTime::fromSecsSinceEpoch(qint64(other.timestamp)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))});
    }
    if (other.shutter > 0.0f) {
        entries.append({QStringLiteral("Shutter"), other.shutter < 1.0f
                            ? QStringLiteral("1/%1 s").arg(qRound(1.0f / other.shutter))
                            : QStringLiteral("%1 s").arg(double(other.shutter), 0, 'g', 3)});
    }
    if (other.aperture > 0.0f) {
        entries.append({QStringLiteral("Aperture"), QStringLiteral("f/%1").arg(double(other.aperture), 0, 'g', 3)});
    }
    if (other.iso_speed > 0.0f) {
        entries.append({QStringLiteral("ISO"), QString::number(qRound(other.iso_speed))});
    }
    if (other.focal_len > 0.0f) {
        entries.append({QStringLiteral("Focal length"), QStringLiteral("%1 mm").arg(double(other.focal_len), 0, 'g', 4)});
    }
    const auto &sizes = data.sizes;
    const bool swapped = (sizes.flip & 4) != 0;
    entries.append({QStringLiteral("Image size"), QStringLiteral("%1 × %2")
                        .arg(swapped ? sizes.height : sizes.width).arg(swapped ? sizes.width : sizes.height)});
    entries.append({QStringLiteral("Raw size"), QStringLiteral("%1 × %2").arg(sizes.raw_width).arg(sizes.raw_height)});
    if (data.color.raw_bps > 0) {
        entries.append({QStringLiteral("Bit depth"), QStringLiteral("%1 bit").arg(data.color.raw_bps)});
    }
    if (data.thumbnail.twidth > 0 && data.thumbnail.theight > 0) {
        entries.append({QStringLiteral("Embedded preview"),
                        QStringLiteral("%1 × %2").arg(data.thumbnail.twidth).arg(data.thumbnail.theight)});
    }
    return entries;
#else
    Q_UNUSED(filePath);
    return {{QStringLiteral("Error"), QStringLiteral("Built without LibRaw (camera raw support).")}};
#endif
}

QDateTime dateTaken(const QString &filePath)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    if (openRaw(raw, filePath) == LIBRAW_SUCCESS && raw.imgdata.other.timestamp > 0) {
        // LibRaw turns the Exif local time into a time_t with mktime(), so this gives it back.
        return QDateTime::fromSecsSinceEpoch(qint64(raw.imgdata.other.timestamp));
    }
#else
    Q_UNUSED(filePath);
#endif
    return {};
}

} // namespace Raw
