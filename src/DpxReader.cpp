#include "DpxReader.h"

#include <QFile>
#include <QFileInfo>
#include <QTransform>

#include <vector>

namespace {

constexpr quint32 MagicBigEndian = 0x53445058;    // "SDPX"
constexpr quint32 MagicLittleEndian = 0x58504453; // "XPDS"
constexpr qint64 HeaderSize = 2048;

// Offsets from SMPTE 268M.
constexpr int OffsetImageData = 4;
constexpr int OffsetOrientation = 768;
constexpr int OffsetElementCount = 770;
constexpr int OffsetPixelsPerLine = 772;
constexpr int OffsetLinesPerElement = 776;
constexpr int OffsetElement0 = 780;
constexpr int ElementDescriptor = 20;
constexpr int ElementBitSize = 23;
constexpr int ElementPacking = 24;
constexpr int ElementEncoding = 26;
constexpr int ElementDataOffset = 28;
constexpr int ElementEndOfLinePadding = 32;

class ByteReader
{
public:
    ByteReader(const uchar *data, qint64 size, bool bigEndian)
        : m_data(data)
        , m_size(size)
        , m_bigEndian(bigEndian)
    {
    }

    bool has(qint64 offset, qint64 length) const
    {
        return offset >= 0 && length >= 0 && offset + length <= m_size;
    }

    quint8 u8(qint64 offset) const { return m_data[offset]; }

    quint16 u16(qint64 offset) const
    {
        const uchar *p = m_data + offset;
        return m_bigEndian ? quint16((p[0] << 8) | p[1]) : quint16((p[1] << 8) | p[0]);
    }

    quint32 u32(qint64 offset) const
    {
        const uchar *p = m_data + offset;
        if (m_bigEndian) {
            return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
        }
        return (quint32(p[3]) << 24) | (quint32(p[2]) << 16) | (quint32(p[1]) << 8) | quint32(p[0]);
    }

private:
    const uchar *m_data;
    qint64 m_size;
    bool m_bigEndian;
};

int componentCount(int descriptor)
{
    switch (descriptor) {
    case 1: // red
    case 2: // green
    case 3: // blue
    case 4: // alpha
    case 6: // luma
        return 1;
    case 50: // RGB
        return 3;
    case 51: // RGBA
    case 52: // ABGR
        return 4;
    default:
        return 0;
    }
}

bool fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

// Bytes one line occupies in the file, before any end-of-line padding.
// Every layout starts each line on a 32-bit boundary.
qint64 lineByteCount(int valueCount, int bitSize, int packing)
{
    switch (bitSize) {
    case 8:
        return ((qint64(valueCount) + 3) / 4) * 4;
    case 16:
        return ((qint64(valueCount) * 2 + 3) / 4) * 4;
    case 12:
        if (packing != 0) {
            return ((qint64(valueCount) * 2 + 3) / 4) * 4;
        }
        return ((qint64(valueCount) * 12 + 31) / 32) * 4;
    default: // 10
        if (packing != 0) {
            return ((qint64(valueCount) + 2) / 3) * 4;
        }
        return ((qint64(valueCount) * 10 + 31) / 32) * 4;
    }
}

// Decodes one line of component values, scaled to 0..65535.
void decodeLine(const ByteReader &reader, qint64 offset, int valueCount, int bitSize, int packing,
    std::vector<quint16> &out)
{
    out.resize(static_cast<size_t>(valueCount));

    switch (bitSize) {
    case 8:
        for (int i = 0; i < valueCount; ++i) {
            out[i] = quint16(reader.u8(offset + i) * 257);
        }
        return;

    case 16:
        for (int i = 0; i < valueCount; ++i) {
            out[i] = reader.u16(offset + qint64(i) * 2);
        }
        return;

    case 10:
        if (packing == 1 || packing == 2) {
            // Three 10-bit values per 32-bit word. Method A pads the low 2 bits, method B the high 2.
            const int shift = packing == 1 ? 2 : 0;
            for (int i = 0; i < valueCount; ++i) {
                const quint32 word = reader.u32(offset + qint64(i / 3) * 4);
                const int slot = 2 - (i % 3);
                const quint32 value = (word >> (shift + slot * 10)) & 0x3FF;
                out[i] = quint16((value << 6) | (value >> 4));
            }
            return;
        }
        break;

    case 12:
        if (packing == 1 || packing == 2) {
            // One 12-bit value per 16-bit word. Method A keeps it in the high bits.
            for (int i = 0; i < valueCount; ++i) {
                const quint16 word = reader.u16(offset + qint64(i) * 2);
                const quint32 value = packing == 1 ? (word >> 4) : (word & 0x0FFF);
                out[i] = quint16((value << 4) | (value >> 8));
            }
            return;
        }
        break;

    default:
        return;
    }

    // Packed (packing 0) 10 or 12 bit: a continuous bit stream in 32-bit words,
    // most significant bits first, with each line ending on a word boundary.
    const qint64 totalBits = qint64(valueCount) * bitSize;
    const qint64 words = (totalBits + 31) / 32;
    const quint32 mask = (1u << bitSize) - 1;
    for (int i = 0; i < valueCount; ++i) {
        const qint64 bit = qint64(i) * bitSize;
        const qint64 wordIndex = bit / 32;
        const int bitInWord = int(bit % 32);
        quint64 bits = quint64(reader.u32(offset + wordIndex * 4)) << 32;
        if (wordIndex + 1 < words) {
            bits |= reader.u32(offset + (wordIndex + 1) * 4);
        }
        const quint32 value = quint32(bits >> (64 - bitInWord - bitSize)) & mask;
        out[i] = bitSize == 10 ? quint16((value << 6) | (value >> 4)) : quint16((value << 4) | (value >> 8));
    }
}

QImage flip(const QImage &image, Qt::Orientations directions)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return image.flipped(directions);
#else
    return image.mirrored(directions.testFlag(Qt::Horizontal), directions.testFlag(Qt::Vertical));
#endif
}

QImage applyOrientation(const QImage &image, int orientation)
{
    switch (orientation) {
    case 1: // right to left, top to bottom
        return flip(image, Qt::Horizontal);
    case 2: // left to right, bottom to top
        return flip(image, Qt::Vertical);
    case 3: // right to left, bottom to top
        return flip(image, Qt::Horizontal | Qt::Vertical);
    case 4: // top to bottom, left to right (transposed)
    case 5:
    case 6:
    case 7: {
        QImage transposed = image.transformed(QTransform(0, 1, 1, 0, 0, 0));
        if (orientation == 5 || orientation == 7) {
            transposed = flip(transposed, Qt::Horizontal);
        }
        if (orientation == 6 || orientation == 7) {
            transposed = flip(transposed, Qt::Vertical);
        }
        return transposed;
    }
    default:
        return image;
    }
}

QImage decode(const uchar *data, qint64 size, QString *error, const QSize &targetSize)
{
    if (size < HeaderSize) {
        fail(error, QStringLiteral("File is too small to be a DPX image."));
        return QImage();
    }

    const quint32 magicBigEndian = (quint32(data[0]) << 24) | (quint32(data[1]) << 16) | (quint32(data[2]) << 8) | data[3];

    bool bigEndian = true;
    if (magicBigEndian == MagicBigEndian) {
        bigEndian = true;
    } else if (magicBigEndian == MagicLittleEndian) {
        bigEndian = false;
    } else {
        fail(error, QStringLiteral("Not a DPX file."));
        return QImage();
    }

    const ByteReader reader(data, size, bigEndian);
    const int orientation = reader.u16(OffsetOrientation);
    const int elementCount = reader.u16(OffsetElementCount);
    const quint32 width = reader.u32(OffsetPixelsPerLine);
    const quint32 height = reader.u32(OffsetLinesPerElement);

    if (elementCount < 1 || width == 0 || height == 0 || width > 65535 || height > 65535) {
        fail(error, QStringLiteral("Unsupported DPX image size (%1 x %2).").arg(width).arg(height));
        return QImage();
    }

    const int descriptor = reader.u8(OffsetElement0 + ElementDescriptor);
    const int bitSize = reader.u8(OffsetElement0 + ElementBitSize);
    const int packing = reader.u16(OffsetElement0 + ElementPacking);
    const int encoding = reader.u16(OffsetElement0 + ElementEncoding);
    quint32 dataOffset = reader.u32(OffsetElement0 + ElementDataOffset);
    quint32 endOfLinePadding = reader.u32(OffsetElement0 + ElementEndOfLinePadding);

    const int components = componentCount(descriptor);
    if (components == 0) {
        fail(error, QStringLiteral("Unsupported DPX pixel layout (descriptor %1).").arg(descriptor));
        return QImage();
    }
    if (bitSize != 8 && bitSize != 10 && bitSize != 12 && bitSize != 16) {
        fail(error, QStringLiteral("Unsupported DPX bit depth (%1).").arg(bitSize));
        return QImage();
    }
    if (packing > 2) {
        fail(error, QStringLiteral("Unsupported DPX packing (%1).").arg(packing));
        return QImage();
    }
    if (encoding != 0) {
        fail(error, QStringLiteral("Run-length encoded DPX files are not supported."));
        return QImage();
    }

    // "Undefined" fields are all ones in DPX.
    if (dataOffset == 0 || dataOffset == 0xFFFFFFFF) {
        dataOffset = reader.u32(OffsetImageData);
    }
    if (endOfLinePadding == 0xFFFFFFFF) {
        endOfLinePadding = 0;
    }


    // Subsampling step for previews: the largest step that still gives at least targetSize.
    quint32 step = 1;
    if (targetSize.isValid() && targetSize.width() > 0 && targetSize.height() > 0) {
        step = qMax(1u, qMin(width / quint32(targetSize.width()), height / quint32(targetSize.height())));
    }
    const quint32 outWidth = (width + step - 1) / step;
    const quint32 outHeight = (height + step - 1) / step;

    const int valuesPerLine = int(width) * components;
    QImage image(int(outWidth), int(outHeight), components == 4 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (image.isNull()) {
        fail(error, QStringLiteral("Not enough memory for the DPX image."));
        return QImage();
    }

    const qint64 lineBytes = lineByteCount(valuesPerLine, bitSize, packing);
    const qint64 lineStride = lineBytes + endOfLinePadding;
    std::vector<quint16> values;
    for (quint32 outY = 0; outY < outHeight; ++outY) {
        const qint64 offset = qint64(dataOffset) + qint64(outY) * step * lineStride;
        if (!reader.has(offset, lineBytes)) {
            fail(error, QStringLiteral("DPX file is truncated."));
            return QImage();
        }

        decodeLine(reader, offset, valuesPerLine, bitSize, packing, values);

        auto *line = reinterpret_cast<QRgb *>(image.scanLine(int(outY)));
        for (quint32 outX = 0; outX < outWidth; ++outX) {
            const quint16 *v = values.data() + size_t(outX) * step * size_t(components);
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 255;
            switch (descriptor) {
            case 50:
                r = v[0] >> 8;
                g = v[1] >> 8;
                b = v[2] >> 8;
                break;
            case 51:
                r = v[0] >> 8;
                g = v[1] >> 8;
                b = v[2] >> 8;
                a = v[3] >> 8;
                break;
            case 52:
                a = v[0] >> 8;
                b = v[1] >> 8;
                g = v[2] >> 8;
                r = v[3] >> 8;
                break;
            case 1:
                r = v[0] >> 8;
                break;
            case 2:
                g = v[0] >> 8;
                break;
            case 3:
                b = v[0] >> 8;
                break;
            default: // luma or alpha shown as grey
                r = g = b = v[0] >> 8;
                break;
            }
            line[outX] = qRgba(r, g, b, a);
        }
    }

    return applyOrientation(image, orientation);
}

} // namespace

namespace Dpx {

bool isDpxFile(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("dpx"), Qt::CaseInsensitive) == 0;
}

QImage read(const QString &filePath, QString *error, const QSize &targetSize)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return QImage();
    }

    const qint64 size = file.size();
    if (uchar *mapped = file.map(0, size)) {
        const QImage image = decode(mapped, size, error, targetSize);
        file.unmap(mapped);
        return image;
    }

    const QByteArray bytes = file.readAll();
    return decode(reinterpret_cast<const uchar *>(bytes.constData()), bytes.size(), error, targetSize);
}

} // namespace Dpx
