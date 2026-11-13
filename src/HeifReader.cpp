#include "HeifReader.h"

#include <QFileInfo>

#ifdef ONION_HAVE_LIBHEIF
#include <QColorSpace>
#include <QFile>
#include <QPointF>
#include <QStringList>

#include <libheif/heif.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>
#endif

namespace {

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

#ifdef ONION_HAVE_LIBHEIF

QString errorText(const heif_error &error)
{
    return error.message && *error.message ? QString::fromUtf8(error.message)
                                           : QStringLiteral("libheif error %1").arg(static_cast<int>(error.code));
}

// libheif reads through these callbacks from a QFile, so Unicode paths work on Windows and only
// the parts of the file that are needed get read (the header and Exif for the metadata panel,
// the embedded thumbnail for thumbnails).
int64_t readerPosition(void *userdata)
{
    return static_cast<QFile *>(userdata)->pos();
}

int readerRead(void *data, size_t size, void *userdata)
{
    QFile *file = static_cast<QFile *>(userdata);
    const qint64 wanted = static_cast<qint64>(size);
    return file->read(static_cast<char *>(data), wanted) == wanted ? 0 : 1;
}

int readerSeek(int64_t position, void *userdata)
{
    return static_cast<QFile *>(userdata)->seek(position) ? 0 : 1;
}

heif_reader_grow_status readerWaitForSize(int64_t targetSize, void *userdata)
{
    return targetSize <= static_cast<QFile *>(userdata)->size() ? heif_reader_grow_status_size_reached
                                                                : heif_reader_grow_status_size_beyond_eof;
}

// An open file: the libheif context and the primary image's handle.
class Context final
{
public:
    Context(const QString &filePath, QString *error)
        : m_file(filePath)
    {
        static std::once_flag initialised;
        std::call_once(initialised, [] { heif_init(nullptr); });

        if (!m_file.open(QIODevice::ReadOnly)) {
            fail(error, m_file.errorString());
            return;
        }
        m_reader.reader_api_version = 1;
        m_reader.get_position = readerPosition;
        m_reader.read = readerRead;
        m_reader.seek = readerSeek;
        m_reader.wait_for_file_size = readerWaitForSize;

        m_context = heif_context_alloc();
        heif_error result = heif_context_read_from_reader(m_context, &m_reader, &m_file, nullptr);
        if (result.code != heif_error_Ok) {
            fail(error, errorText(result));
            return;
        }
        result = heif_context_get_primary_image_handle(m_context, &m_primary);
        if (result.code != heif_error_Ok) {
            m_primary = nullptr;
            fail(error, errorText(result));
        }
    }

    ~Context()
    {
        if (m_primary) {
            heif_image_handle_release(m_primary);
        }
        if (m_context) {
            heif_context_free(m_context);
        }
    }

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    heif_context *context() const { return m_context; }
    heif_image_handle *primary() const { return m_primary; }

private:
    QFile m_file;
    heif_reader m_reader{};
    heif_context *m_context = nullptr;
    heif_image_handle *m_primary = nullptr;
};

// Releases an image handle when it goes out of scope.
struct HandleGuard
{
    heif_image_handle *handle = nullptr;
    ~HandleGuard()
    {
        if (handle) {
            heif_image_handle_release(handle);
        }
    }
};

// The colour space the pixels are in, from the ICC profile or the nclx box. Invalid when it is
// unknown or already sRGB / Rec.709, and for HDR transfer curves (PQ, HLG), which are left as
// stored rather than guessed at.
QColorSpace sourceColorSpace(const heif_image_handle *handle)
{
    const size_t iccSize = heif_image_handle_get_raw_color_profile_size(handle);
    if (iccSize > 0) {
        QByteArray icc(static_cast<qsizetype>(iccSize), Qt::Uninitialized);
        if (heif_image_handle_get_raw_color_profile(handle, icc.data()).code == heif_error_Ok) {
            const QColorSpace space = QColorSpace::fromIccProfile(icc);
            if (space.isValid() && space != QColorSpace(QColorSpace::SRgb)) {
                return space;
            }
        }
        return QColorSpace();
    }

    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code != heif_error_Ok || !nclx) {
        return QColorSpace();
    }
    const int primaries = static_cast<int>(nclx->color_primaries);
    const int transfer = static_cast<int>(nclx->transfer_characteristics);
    heif_nclx_color_profile_free(nclx);

    // sRGB, unspecified, and the Rec.709-style curves (BT.709, BT.601, BT.2020 10/12 bit), which
    // are shown as encoded on an sRGB screen like everywhere else.
    const bool sdrTransfer = transfer == 13 || transfer == 2 || transfer == 1 || transfer == 6
        || transfer == 14 || transfer == 15;
    if (!sdrTransfer) {
        return QColorSpace();
    }
    const QPointF d65(0.3127, 0.3290);
    switch (primaries) {
    case 12: // Display P3 (SMPTE EG 432-1): iPhone photos
        return QColorSpace(QColorSpace::Primaries::DciP3D65, QColorSpace::TransferFunction::SRgb);
    case 9: // Rec.2020
        return QColorSpace(d65, QPointF(0.708, 0.292), QPointF(0.170, 0.797), QPointF(0.131, 0.046),
                           QColorSpace::TransferFunction::SRgb);
    default: // Rec.709 / sRGB, unspecified, or something rare: shown as stored
        return QColorSpace();
    }
}

QImage decode(const heif_image_handle *handle, QString *error)
{
    const bool alpha = heif_image_handle_has_alpha_channel(handle) != 0;
    const bool deep = heif_image_handle_get_luma_bits_per_pixel(handle) > 8;
    const heif_chroma chroma = deep ? (alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE)
                                    : (alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB);

    heif_image *decoded = nullptr;
    const heif_error result = heif_decode_image(handle, &decoded, heif_colorspace_RGB, chroma, nullptr);
    if (result.code != heif_error_Ok || !decoded) {
        fail(error, errorText(result));
        return QImage();
    }

    const int width = heif_image_get_width(decoded, heif_channel_interleaved);
    const int height = heif_image_get_height(decoded, heif_channel_interleaved);
    int stride = 0;
    const uint8_t *plane = heif_image_get_plane_readonly(decoded, heif_channel_interleaved, &stride);
    if (!plane || width <= 0 || height <= 0) {
        heif_image_release(decoded);
        fail(error, QStringLiteral("libheif returned no pixels"));
        return QImage();
    }

    QImage image;
    if (!deep) {
        image = QImage(width, height, alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
        const size_t rowBytes = static_cast<size_t>(width) * (alpha ? 4 : 3);
        for (int y = 0; y < height; ++y) {
            std::memcpy(image.scanLine(y), plane + static_cast<qsizetype>(y) * stride, rowBytes);
        }
    } else {
        // 10/12-bit values, little endian, widened to 16 bit.
        const int bits = std::max(1, heif_image_get_bits_per_pixel_range(decoded, heif_channel_interleaved));
        const quint32 maximum = (1u << std::min(bits, 16)) - 1u;
        const int channels = alpha ? 4 : 3;
        image = QImage(width, height, alpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64);
        for (int y = 0; y < height; ++y) {
            const uint8_t *source = plane + static_cast<qsizetype>(y) * stride;
            quint16 *target = reinterpret_cast<quint16 *>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                for (int c = 0; c < 4; ++c) {
                    quint32 value = 65535u;
                    if (c < channels) {
                        const uint8_t *bytes = source + (x * channels + c) * 2;
                        const quint32 raw = std::min<quint32>(static_cast<quint32>(bytes[0] | (bytes[1] << 8)), maximum);
                        value = (raw * 65535u + maximum / 2u) / maximum;
                    }
                    target[x * 4 + c] = static_cast<quint16>(value);
                }
            }
        }
    }
    heif_image_release(decoded);

    const QColorSpace space = sourceColorSpace(handle);
    if (space.isValid()) {
        if (!deep) {
            image = image.convertToFormat(alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        }
        image.setColorSpace(space);
        image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    return image;
}

QString sizeText(int width, int height)
{
    return QStringLiteral("%1 × %2").arg(width).arg(height);
}

QString primariesName(int value)
{
    switch (value) {
    case 1: return QStringLiteral("Rec.709 / sRGB");
    case 2: return QStringLiteral("unspecified");
    case 5: case 6: return QStringLiteral("Rec.601");
    case 9: return QStringLiteral("Rec.2020");
    case 11: return QStringLiteral("DCI-P3");
    case 12: return QStringLiteral("Display P3");
    default: return QStringLiteral("code %1").arg(value);
    }
}

QString transferName(int value)
{
    switch (value) {
    case 1: case 6: case 14: case 15: return QStringLiteral("Rec.709");
    case 2: return QStringLiteral("unspecified");
    case 8: return QStringLiteral("linear");
    case 13: return QStringLiteral("sRGB");
    case 16: return QStringLiteral("PQ (HDR)");
    case 18: return QStringLiteral("HLG (HDR)");
    default: return QStringLiteral("code %1").arg(value);
    }
}

QString colorProfileText(const heif_image_handle *handle)
{
    const size_t iccSize = heif_image_handle_get_raw_color_profile_size(handle);
    if (iccSize > 0) {
        QByteArray icc(static_cast<qsizetype>(iccSize), Qt::Uninitialized);
        if (heif_image_handle_get_raw_color_profile(handle, icc.data()).code == heif_error_Ok) {
            const QString description = QColorSpace::fromIccProfile(icc).description();
            return description.isEmpty() ? QStringLiteral("ICC profile") : QStringLiteral("ICC: %1").arg(description);
        }
    }
    heif_color_profile_nclx *nclx = nullptr;
    if (heif_image_handle_get_nclx_color_profile(handle, &nclx).code == heif_error_Ok && nclx) {
        const QString text = QStringLiteral("%1 primaries, %2 transfer%3")
            .arg(primariesName(static_cast<int>(nclx->color_primaries)),
                 transferName(static_cast<int>(nclx->transfer_characteristics)),
                 nclx->full_range_flag ? QStringLiteral(", full range") : QStringLiteral(", video range"));
        heif_nclx_color_profile_free(nclx);
        return text;
    }
    return QStringLiteral("None");
}

QString mainBrand(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray head = file.read(12);
    if (head.size() < 12 || head.mid(4, 4) != "ftyp") {
        return QString();
    }
    return QString::fromLatin1(head.mid(8, 4)).trimmed();
}

#endif // ONION_HAVE_LIBHEIF

} // namespace

namespace Heif {

bool isAvailable()
{
#ifdef ONION_HAVE_LIBHEIF
    return true;
#else
    return false;
#endif
}

bool isHeifFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix();
    for (const char *heif : {"heic", "heif", "hif"}) {
        if (suffix.compare(QLatin1String(heif), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QImage read(const QString &filePath, QString *error, const QSize &targetSize)
{
#ifdef ONION_HAVE_LIBHEIF
    const Context file(filePath, error);
    if (!file.primary()) {
        return QImage();
    }

    if (targetSize.isValid()) {
        const int wanted = std::max(targetSize.width(), targetSize.height());
        const int count = heif_image_handle_get_number_of_thumbnails(file.primary());
        std::vector<heif_item_id> ids(static_cast<size_t>(std::max(count, 0)));
        if (count > 0) {
            heif_image_handle_get_list_of_thumbnail_IDs(file.primary(), ids.data(), count);
        }
        for (const heif_item_id id : ids) {
            HandleGuard thumbnail;
            if (heif_image_handle_get_thumbnail(file.primary(), id, &thumbnail.handle).code != heif_error_Ok) {
                thumbnail.handle = nullptr;
                continue;
            }
            const int size = std::max(heif_image_handle_get_width(thumbnail.handle),
                                      heif_image_handle_get_height(thumbnail.handle));
            if (size >= wanted) {
                QImage image = decode(thumbnail.handle, nullptr);
                if (!image.isNull()) {
                    return image;
                }
            }
        }
    }

    return decode(file.primary(), error);
#else
    Q_UNUSED(targetSize);
    fail(error, QStringLiteral("Built without HEIF support (libheif)"));
    Q_UNUSED(filePath);
    return QImage();
#endif
}

QList<QPair<QString, QString>> describe(const QString &filePath, QString *error)
{
    QList<QPair<QString, QString>> facts;
#ifdef ONION_HAVE_LIBHEIF
    const Context file(filePath, error);
    if (!file.primary()) {
        return facts;
    }
    const heif_image_handle *handle = file.primary();
    const auto yesNo = [](bool value) { return value ? QStringLiteral("Yes") : QStringLiteral("No"); };

    const QString brand = mainBrand(filePath);
    if (!brand.isEmpty()) {
        facts.append({QStringLiteral("Brand"), brand});
    }
    facts.append({QStringLiteral("Size"), sizeText(heif_image_handle_get_width(handle), heif_image_handle_get_height(handle))});
    const int luma = heif_image_handle_get_luma_bits_per_pixel(handle);
    const int chroma = heif_image_handle_get_chroma_bits_per_pixel(handle);
    facts.append({QStringLiteral("Bit depth"), luma == chroma || chroma <= 0
                      ? QStringLiteral("%1 bits").arg(luma)
                      : QStringLiteral("%1 bits luma, %2 bits chroma").arg(luma).arg(chroma)});
    facts.append({QStringLiteral("Alpha"), heif_image_handle_has_alpha_channel(handle)
                      ? (heif_image_handle_is_premultiplied_alpha(handle) ? QStringLiteral("Yes (premultiplied)") : QStringLiteral("Yes"))
                      : QStringLiteral("No")});
    facts.append({QStringLiteral("Color profile"), colorProfileText(handle)});
    facts.append({QStringLiteral("Images in file"), QString::number(heif_context_get_number_of_top_level_images(file.context()))});

    const int thumbnailCount = heif_image_handle_get_number_of_thumbnails(handle);
    if (thumbnailCount > 0) {
        std::vector<heif_item_id> ids(static_cast<size_t>(thumbnailCount));
        heif_image_handle_get_list_of_thumbnail_IDs(handle, ids.data(), thumbnailCount);
        QStringList sizes;
        for (const heif_item_id id : ids) {
            HandleGuard thumbnail;
            if (heif_image_handle_get_thumbnail(handle, id, &thumbnail.handle).code == heif_error_Ok) {
                sizes << sizeText(heif_image_handle_get_width(thumbnail.handle), heif_image_handle_get_height(thumbnail.handle));
            } else {
                thumbnail.handle = nullptr;
            }
        }
        facts.append({QStringLiteral("Embedded thumbnail"), sizes.join(QStringLiteral(", "))});
    }
    facts.append({QStringLiteral("Depth map"), yesNo(heif_image_handle_has_depth_image(handle) != 0)});

    const int blockCount = heif_image_handle_get_number_of_metadata_blocks(handle, nullptr);
    if (blockCount > 0) {
        std::vector<heif_item_id> ids(static_cast<size_t>(blockCount));
        heif_image_handle_get_list_of_metadata_block_IDs(handle, nullptr, ids.data(), blockCount);
        QStringList types;
        for (const heif_item_id id : ids) {
            const QString type = QString::fromLatin1(heif_image_handle_get_metadata_type(handle, id));
            const QString content = QString::fromLatin1(heif_image_handle_get_metadata_content_type(handle, id));
            types << (type == QStringLiteral("mime") && content.contains(QStringLiteral("rdf+xml")) ? QStringLiteral("XMP")
                      : type == QStringLiteral("mime") && !content.isEmpty() ? content
                                                                                : type);
        }
        types.removeDuplicates();
        facts.append({QStringLiteral("Metadata blocks"), types.join(QStringLiteral(", "))});
    }
#else
    Q_UNUSED(filePath);
    fail(error, QStringLiteral("Built without HEIF support (libheif)"));
#endif
    return facts;
}

QByteArray exifData(const QString &filePath)
{
#ifdef ONION_HAVE_LIBHEIF
    const Context file(filePath, nullptr);
    if (!file.primary()) {
        return QByteArray();
    }
    heif_item_id id = 0;
    if (heif_image_handle_get_list_of_metadata_block_IDs(file.primary(), "Exif", &id, 1) < 1) {
        return QByteArray();
    }
    const size_t size = heif_image_handle_get_metadata_size(file.primary(), id);
    if (size < 8) {
        return QByteArray();
    }
    QByteArray block(static_cast<qsizetype>(size), Qt::Uninitialized);
    if (heif_image_handle_get_metadata(file.primary(), id, block.data()).code != heif_error_Ok) {
        return QByteArray();
    }
    // A HEIF Exif item starts with the 32-bit big-endian offset of the TIFF header.
    const quint32 offset = (static_cast<quint32>(static_cast<uchar>(block[0])) << 24)
        | (static_cast<quint32>(static_cast<uchar>(block[1])) << 16)
        | (static_cast<quint32>(static_cast<uchar>(block[2])) << 8)
        | static_cast<quint32>(static_cast<uchar>(block[3]));
    const qsizetype start = 4 + static_cast<qsizetype>(offset);
    if (offset > static_cast<quint32>(block.size()) || start >= block.size()) {
        return QByteArray();
    }
    return block.mid(start);
#else
    Q_UNUSED(filePath);
    return QByteArray();
#endif
}

} // namespace Heif
