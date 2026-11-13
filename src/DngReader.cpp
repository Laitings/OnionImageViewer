#include "DngReader.h"

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

int openDng(LibRaw &raw, const QString &path)
{
#ifdef Q_OS_WIN
    const int result = raw.open_file(path.toStdWString().c_str());
#else
    const int result = raw.open_file(QFile::encodeName(path).constData());
#endif
    if (result == LIBRAW_SUCCESS && raw.imgdata.idata.dng_version == 0) {
        return LIBRAW_FILE_UNSUPPORTED;
    }
    return result;
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
            *error = result == LIBRAW_SUCCESS ? QStringLiteral("Could not allocate or render the DNG image.")
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

namespace Dng {

bool isAvailable()
{
#ifdef ONION_HAVE_LIBRAW
    return true;
#else
    return false;
#endif
}

bool isDngFile(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QLatin1String("dng"), Qt::CaseInsensitive) == 0;
}

QImage read(const QString &filePath, QString *error)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    const int result = openDng(raw, filePath);
    if (result == LIBRAW_SUCCESS) {
        return develop(raw, false, error);
    }
    if (error) {
        *error = QString::fromLocal8Bit(LibRaw::strerror(result));
    }
#else
    Q_UNUSED(filePath);
    if (error) {
        *error = QStringLiteral("Built without LibRaw (DNG support).");
    }
#endif
    return {};
}

QImage readThumbnail(const QString &filePath, const QSize &size)
{
#ifdef ONION_HAVE_LIBRAW
    LibRaw raw(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    if (openDng(raw, filePath) == LIBRAW_SUCCESS) {
        const QImage preview = embeddedPreview(raw, size);
        if (!preview.isNull()) {
            return preview;
        }
        // A failed thumbnail extraction can reset LibRaw's state.
        if (openDng(raw, filePath) == LIBRAW_SUCCESS) {
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
    const int result = openDng(raw, filePath);
    if (result != LIBRAW_SUCCESS) {
        return {{QStringLiteral("Error"), QString::fromLocal8Bit(LibRaw::strerror(result))}};
    }
    const auto &data = raw.imgdata;
    const unsigned version = data.idata.dng_version;
    QList<QPair<QString, QString>> entries = {
        {QStringLiteral("Version"), QStringLiteral("%1.%2.%3.%4")
             .arg(version >> 24).arg((version >> 16) & 255).arg((version >> 8) & 255).arg(version & 255)},
        {QStringLiteral("Raw size"), QStringLiteral("%1 × %2").arg(data.sizes.raw_width).arg(data.sizes.raw_height)},
    };
    const QString camera = QStringLiteral("%1 %2").arg(QString::fromLatin1(data.idata.make),
                                                       QString::fromLatin1(data.idata.model)).trimmed();
    if (!camera.isEmpty()) {
        entries.append({QStringLiteral("Camera"), camera});
    }
    return entries;
#else
    Q_UNUSED(filePath);
    return {{QStringLiteral("Error"), QStringLiteral("Built without LibRaw (DNG support).")}};
#endif
}

} // namespace Dng
