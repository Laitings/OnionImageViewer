#include "PsdReader.h"

#include "ColorTransform.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

enum ColorMode {
    ModeBitmap = 0,
    ModeGrayscale = 1,
    ModeIndexed = 2,
    ModeRgb = 3,
    ModeCmyk = 4,
    ModeMultichannel = 7,
    ModeDuotone = 8,
    ModeLab = 9,
};

enum Compression {
    CompressionRaw = 0,
    CompressionRle = 1,
    CompressionZip = 2,
    CompressionZipPrediction = 3,
};

constexpr int MaxDimension = 300000; // PSB limit
constexpr qint64 MaxPixels = qint64(1) << 31;

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QString colorModeName(int mode)
{
    switch (mode) {
    case ModeBitmap: return QStringLiteral("Bitmap");
    case ModeGrayscale: return QStringLiteral("Grayscale");
    case ModeIndexed: return QStringLiteral("Indexed");
    case ModeRgb: return QStringLiteral("RGB");
    case ModeCmyk: return QStringLiteral("CMYK");
    case ModeMultichannel: return QStringLiteral("Multichannel");
    case ModeDuotone: return QStringLiteral("Duotone");
    case ModeLab: return QStringLiteral("Lab");
    default: return QStringLiteral("Mode %1").arg(mode);
    }
}

// Big-endian reads from the file. Any failure (short read, bad seek) sticks, so callers can
// read a whole structure and check ok() once.
class Reader
{
public:
    explicit Reader(QFile &file)
        : m_file(file)
        , m_size(file.size())
    {
    }

    bool ok() const { return m_ok; }
    void setFailed() { m_ok = false; }
    qint64 pos() const { return m_file.pos(); }
    qint64 size() const { return m_size; }

    bool seek(qint64 position)
    {
        if (!m_ok || position < 0 || position > m_size || !m_file.seek(position)) {
            m_ok = false;
        }
        return m_ok;
    }

    bool skip(qint64 count) { return seek(pos() + count); }

    QByteArray bytes(qint64 count)
    {
        if (!m_ok || count < 0 || count > m_size - pos()) {
            m_ok = false;
            return QByteArray();
        }
        QByteArray data = m_file.read(count);
        if (data.size() != count) {
            m_ok = false;
        }
        return data;
    }

    bool raw(void *target, qint64 count)
    {
        if (!m_ok || count < 0 || m_file.read(static_cast<char *>(target), count) != count) {
            m_ok = false;
            std::memset(target, 0, size_t(qMax<qint64>(count, 0)));
        }
        return m_ok;
    }

    quint8 u8()
    {
        quint8 v = 0;
        raw(&v, 1);
        return v;
    }
    quint16 u16()
    {
        uchar b[2];
        raw(b, 2);
        return qFromBigEndian<quint16>(b);
    }
    quint32 u32()
    {
        uchar b[4];
        raw(b, 4);
        return qFromBigEndian<quint32>(b);
    }
    quint64 u64()
    {
        uchar b[8];
        raw(b, 8);
        return qFromBigEndian<quint64>(b);
    }
    qint16 i16() { return qint16(u16()); }
    qint32 i32() { return qint32(u32()); }
    QByteArray tag() { return bytes(4); }
    // Section lengths are 8 bytes in PSB files, 4 in PSD files.
    qint64 length(bool wide)
    {
        const quint64 v = wide ? u64() : u32();
        if (v > quint64(m_size)) {
            m_ok = false;
            return 0;
        }
        return qint64(v);
    }

private:
    QFile &m_file;
    qint64 m_size;
    bool m_ok = true;
};

struct Document
{
    bool large = false;
    int channels = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    int mode = 0;
    QByteArray palette;      // indexed: 256 reds, 256 greens, 256 blues
    int transparentIndex = -1;
    QByteArray icc;
    QByteArray previewJpeg;
    QSize previewSize;
    bool hasComposite = true;
    qint64 layerSectionStart = 0;
    qint64 layerSectionEnd = 0;
    qint64 imageDataStart = 0;
};

bool isSignature(const QByteArray &sig)
{
    return sig == "8BIM" || sig == "8B64";
}

// Keys whose tagged-block length is 8 bytes in PSB files.
bool hasWideLength(const QByteArray &key)
{
    static const char *const keys[] = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                       "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    for (const char *k : keys) {
        if (key == k) {
            return true;
        }
    }
    return false;
}

// After a tagged block, writers pad to 2 or 4 bytes and don't always count the padding in the
// length. Step over it if the next block's signature isn't right there.
void alignToNextBlock(Reader &r, qint64 end)
{
    const qint64 start = r.pos();
    for (int pad = 0; pad < 4 && start + pad + 4 <= end; ++pad) {
        r.seek(start + pad);
        if (isSignature(r.bytes(4))) {
            r.seek(start + pad);
            return;
        }
    }
    r.seek(start);
}

bool readDocument(QFile &file, Document *doc, QString *error)
{
    Reader r(file);
    if (r.tag() != "8BPS") {
        fail(error, QStringLiteral("Not a Photoshop file"));
        return false;
    }
    const int version = r.u16();
    if (version != 1 && version != 2) {
        fail(error, QStringLiteral("Unsupported PSD version %1").arg(version));
        return false;
    }
    doc->large = version == 2;
    r.skip(6);
    doc->channels = r.u16();
    doc->height = int(qMin<quint32>(r.u32(), MaxDimension + 1));
    doc->width = int(qMin<quint32>(r.u32(), MaxDimension + 1));
    doc->depth = r.u16();
    doc->mode = r.u16();
    if (!r.ok()) {
        fail(error, QStringLiteral("Truncated PSD header"));
        return false;
    }
    if (doc->width <= 0 || doc->height <= 0 || doc->width > MaxDimension || doc->height > MaxDimension
        || doc->channels < 1 || doc->channels > 56) {
        fail(error, QStringLiteral("Invalid PSD dimensions"));
        return false;
    }
    if (doc->depth != 1 && doc->depth != 8 && doc->depth != 16 && doc->depth != 32) {
        fail(error, QStringLiteral("Unsupported PSD bit depth %1").arg(doc->depth));
        return false;
    }

    // Colour mode data: the palette of indexed images (duotone data is not needed).
    const qint64 colorDataLength = r.u32();
    const qint64 colorDataStart = r.pos();
    if (doc->mode == ModeIndexed && colorDataLength >= 768) {
        doc->palette = r.bytes(768);
    }
    r.seek(colorDataStart + colorDataLength);

    // Image resources.
    const qint64 resourcesLength = r.u32();
    const qint64 resourcesEnd = r.pos() + resourcesLength;
    while (r.ok() && r.pos() + 12 <= resourcesEnd) {
        r.tag(); // "8BIM" (or another vendor's signature)
        const int id = r.u16();
        const int nameLength = r.u8();
        r.skip(nameLength + ((nameLength + 1) % 2)); // Pascal string padded to even
        const qint64 size = r.u32();
        const qint64 dataStart = r.pos();
        if (!r.ok() || dataStart + size > resourcesEnd) {
            break;
        }
        if (id == 1039) { // ICC profile
            doc->icc = r.bytes(size);
        } else if (id == 1057 && size >= 5) { // version info
            r.u32();
            doc->hasComposite = r.u8() != 0;
        } else if (id == 1047 && size >= 2) { // transparency index
            doc->transparentIndex = r.u16();
        } else if (id == 1036 && size > 28) { // thumbnail (JPEG, RGB)
            const quint32 format = r.u32();
            const int w = int(r.u32());
            const int h = int(r.u32());
            r.skip(16);
            if (format == 1) {
                doc->previewJpeg = r.bytes(size - 28);
                doc->previewSize = QSize(w, h);
            }
        }
        r.seek(dataStart + size + (size % 2));
    }
    if (!r.ok()) {
        fail(error, QStringLiteral("Truncated PSD resources"));
        return false;
    }
    r.seek(resourcesEnd);

    const qint64 layerSectionLength = r.length(doc->large);
    doc->layerSectionStart = r.pos();
    doc->layerSectionEnd = doc->layerSectionStart + layerSectionLength;
    doc->imageDataStart = doc->layerSectionEnd;
    if (!r.ok() || doc->imageDataStart > r.size()) {
        fail(error, QStringLiteral("Truncated PSD layer section"));
        return false;
    }
    return true;
}

struct LayerSection
{
    qint64 infoStart = 0; // the layer count; 0 if the document has no layers
    qint64 infoEnd = 0;
    int count = 0;
    bool mergedTransparency = false; // the composite's first extra channel is its transparency
};

// The layer info is either at the start of the layer section or (16/32-bit documents) inside an
// Lr16/Lr32 block after it. The composite has transparency when the layer count is negative or an
// Mtrn/Mt16/Mt32 block is present.
LayerSection scanLayerSection(Reader &r, const Document &doc)
{
    LayerSection section;
    if (doc.layerSectionEnd - doc.layerSectionStart < 4) {
        return section;
    }
    r.seek(doc.layerSectionStart);
    const qint64 layerInfoLength = r.length(doc.large);
    if (!r.ok()) {
        return section;
    }
    if (layerInfoLength >= 2) {
        section.infoStart = r.pos();
        section.infoEnd = r.pos() + layerInfoLength;
        section.count = r.i16();
    }
    r.seek(r.pos() - (section.infoStart ? 2 : 0) + layerInfoLength);
    const qint64 globalMaskLength = r.u32();
    r.skip(globalMaskLength);
    while (r.ok() && r.pos() + 12 <= doc.layerSectionEnd) {
        alignToNextBlock(r, doc.layerSectionEnd);
        if (!isSignature(r.tag())) {
            break;
        }
        const QByteArray key = r.tag();
        const qint64 length = r.length(doc.large && hasWideLength(key));
        const qint64 blockEnd = r.pos() + length;
        if (!r.ok()) {
            break;
        }
        if ((key == "Lr16" || key == "Lr32" || key == "Layr") && length >= 2 && section.infoStart == 0) {
            section.infoStart = r.pos();
            section.infoEnd = blockEnd;
            section.count = r.i16();
        } else if (key == "Mtrn" || key == "Mt16" || key == "Mt32") {
            section.mergedTransparency = true;
        }
        r.seek(blockEnd);
    }
    if (section.count < 0) {
        section.mergedTransparency = true;
    }
    return section;
}

// ----------------------------------------------------------------------------------------------
// Pixel data

// One decoded channel, 16 bits per sample, holding every step-th line and pixel.
struct Plane
{
    int width = 0;
    int height = 0;
    std::vector<quint16> values;
    quint16 at(int x, int y) const { return values[size_t(y) * size_t(width) + size_t(x)]; }
};

quint16 encodeSrgb(float linear)
{
    if (!(linear > 0.0f)) {
        return 0;
    }
    if (linear >= 1.0f) {
        return 65535;
    }
    const float v = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return quint16(v * 65535.0f + 0.5f);
}

quint16 sixteenBitsFromFloat(float value)
{
    if (!(value > 0.0f)) {
        return 0;
    }
    return value >= 1.0f ? 65535 : quint16(value * 65535.0f + 0.5f);
}

// How the decoded rows and columns map to the kept samples: rows firstRow, firstRow + step, ...
struct Sampling
{
    int step = 1;
    int firstRow = 0;
    int firstColumn = 0;
    int rows(int height) const { return height > firstRow ? (height - firstRow + step - 1) / step : 0; }
    int columns(int width) const { return width > firstColumn ? (width - firstColumn + step - 1) / step : 0; }
};

// The first index i >= 0 where (origin + i) is a multiple of step.
int phase(int origin, int step)
{
    const int m = origin % step;
    return m == 0 ? 0 : (m > 0 ? step - m : -m);
}

class PlaneDecoder
{
public:
    PlaneDecoder(int width, int height, int depth, bool large, const Sampling &sampling)
        : m_width(width)
        , m_height(height)
        , m_depth(depth)
        , m_large(large)
        , m_sampling(sampling)
        , m_rowBytes((qint64(width) * depth + 7) / 8)
    {
    }

    // Decodes `channelCount` planes stored one after another (the composite stores all its
    // channels this way; each layer channel is one plane). linearFloat: 32-bit values are linear
    // light and get encoded to sRGB (alpha and masks are not).
    // Only the first keepChannels planes are kept.
    bool decode(const uchar *data, qint64 size, int compression, int channelCount, int keepChannels,
                const std::vector<bool> &linearFloat, std::vector<Plane> *planes)
    {
        planes->assign(size_t(qMin(keepChannels, channelCount)), Plane());
        const int outW = m_sampling.columns(m_width);
        const int outH = m_sampling.rows(m_height);
        for (Plane &plane : *planes) {
            plane.width = outW;
            plane.height = outH;
            plane.values.assign(size_t(outW) * size_t(outH), 0);
        }
        if (outW == 0 || outH == 0 || m_rowBytes == 0) {
            return true;
        }
        m_planes = planes;
        m_linear = &linearFloat;

        switch (compression) {
        case CompressionRaw:
            return decodeRaw(data, size, channelCount);
        case CompressionRle:
            return decodeRle(data, size, channelCount);
        case CompressionZip:
        case CompressionZipPrediction:
            return decodeZip(data, size, channelCount, compression == CompressionZipPrediction);
        default:
            return false;
        }
    }

private:
    bool keepsRow(int y) const
    {
        return y >= m_sampling.firstRow && (y - m_sampling.firstRow) % m_sampling.step == 0;
    }

    void storeRow(int channel, int y, const uchar *row)
    {
        if (size_t(channel) >= m_planes->size()) {
            return;
        }
        Plane &plane = (*m_planes)[size_t(channel)];
        const int outY = (y - m_sampling.firstRow) / m_sampling.step;
        if (outY >= plane.height) {
            return;
        }
        quint16 *out = plane.values.data() + size_t(outY) * size_t(plane.width);
        const bool linear = m_depth == 32 && size_t(channel) < m_linear->size() && (*m_linear)[size_t(channel)];
        int x = m_sampling.firstColumn;
        for (int i = 0; i < plane.width; ++i, x += m_sampling.step) {
            switch (m_depth) {
            case 1:
                // Bitmap mode: a set bit is black.
                out[i] = ((row[x >> 3] >> (7 - (x & 7))) & 1) ? 0 : 65535;
                break;
            case 8:
                out[i] = quint16(row[x] * 257);
                break;
            case 16:
                out[i] = qFromBigEndian<quint16>(row + size_t(x) * 2);
                break;
            default: {
                const float v = qFromBigEndian<float>(row + size_t(x) * 4);
                out[i] = linear ? encodeSrgb(v) : sixteenBitsFromFloat(v);
                break;
            }
            }
        }
    }

    bool decodeRaw(const uchar *data, qint64 size, int channelCount)
    {
        for (int c = 0; c < channelCount; ++c) {
            for (int y = 0; y < m_height; ++y) {
                if (!keepsRow(y)) {
                    continue;
                }
                const qint64 offset = (qint64(c) * m_height + y) * m_rowBytes;
                if (offset + m_rowBytes > size) {
                    return true; // truncated: keep what is there, the rest stays black
                }
                storeRow(c, y, data + offset);
            }
        }
        return true;
    }

    bool decodeRle(const uchar *data, qint64 size, int channelCount)
    {
        const int countBytes = m_large ? 4 : 2;
        const qint64 rows = qint64(channelCount) * m_height;
        qint64 offset = rows * countBytes;
        if (offset > size) {
            return false;
        }
        std::vector<uchar> row(static_cast<size_t>(m_rowBytes));
        for (qint64 i = 0; i < rows; ++i) {
            const qint64 length = m_large ? qint64(qFromBigEndian<quint32>(data + i * 4))
                                          : qint64(qFromBigEndian<quint16>(data + i * 2));
            if (offset + length > size) {
                return false;
            }
            const int c = int(i / m_height);
            const int y = int(i % m_height);
            if (keepsRow(y)) {
                unpackBits(data + offset, length, row.data(), m_rowBytes);
                storeRow(c, y, row.data());
            }
            offset += length;
        }
        return true;
    }

    static void unpackBits(const uchar *in, qint64 inSize, uchar *out, qint64 outSize)
    {
        qint64 i = 0;
        qint64 o = 0;
        while (i < inSize && o < outSize) {
            const int n = qint8(in[i++]);
            if (n >= 0) {
                const qint64 count = qMin<qint64>(qMin<qint64>(n + 1, inSize - i), outSize - o);
                std::memcpy(out + o, in + i, size_t(count));
                i += n + 1;
                o += count;
            } else if (n != -128) {
                if (i >= inSize) {
                    break;
                }
                const qint64 count = qMin<qint64>(1 - n, outSize - o);
                std::memset(out + o, in[i++], size_t(count));
                o += count;
            }
        }
        if (o < outSize) {
            std::memset(out + o, 0, size_t(outSize - o));
        }
    }

    bool decodeZip(const uchar *data, qint64 size, int channelCount, bool prediction)
    {
        const qint64 expected = qint64(channelCount) * m_height * m_rowBytes;
        if (expected > 0x7fffffffLL) {
            return false;
        }
        // qUncompress wants the uncompressed size in front of the zlib stream.
        QByteArray packed;
        packed.reserve(qsizetype(size + 4));
        uchar prefix[4];
        qToBigEndian<quint32>(quint32(expected), prefix);
        packed.append(reinterpret_cast<const char *>(prefix), 4);
        packed.append(reinterpret_cast<const char *>(data), qsizetype(size));
        QByteArray unpacked = qUncompress(packed);
        if (unpacked.size() < expected) {
            return false;
        }
        auto *bytes = reinterpret_cast<uchar *>(unpacked.data());
        std::vector<uchar> shuffled(static_cast<size_t>(m_rowBytes));
        for (int c = 0; c < channelCount; ++c) {
            for (int y = 0; y < m_height; ++y) {
                uchar *row = bytes + (qint64(c) * m_height + y) * m_rowBytes;
                if (prediction) {
                    // The prediction runs along every row, so every row has to be undone.
                    undoPrediction(row, shuffled.data());
                }
                if (keepsRow(y)) {
                    storeRow(c, y, row);
                }
            }
        }
        return true;
    }

    void undoPrediction(uchar *row, uchar *scratch) const
    {
        if (m_depth == 16) {
            quint16 previous = 0;
            for (int x = 0; x < m_width; ++x) {
                previous = quint16(previous + qFromBigEndian<quint16>(row + x * 2));
                qToBigEndian<quint16>(previous, row + x * 2);
            }
        } else if (m_depth == 32) {
            // Bytes are deltas over the whole row, and the row holds all first bytes of the
            // floats, then all second bytes, and so on.
            for (qint64 i = 1; i < m_rowBytes; ++i) {
                row[i] = uchar(row[i] + row[i - 1]);
            }
            for (int x = 0; x < m_width; ++x) {
                scratch[x * 4 + 0] = row[x];
                scratch[x * 4 + 1] = row[x + m_width];
                scratch[x * 4 + 2] = row[x + m_width * 2];
                scratch[x * 4 + 3] = row[x + m_width * 3];
            }
            std::memcpy(row, scratch, size_t(m_rowBytes));
        } else {
            for (qint64 i = 1; i < m_rowBytes; ++i) {
                row[i] = uchar(row[i] + row[i - 1]);
            }
        }
    }

    int m_width;
    int m_height;
    int m_depth;
    bool m_large;
    Sampling m_sampling;
    qint64 m_rowBytes;
    std::vector<Plane> *m_planes = nullptr;
    const std::vector<bool> *m_linear = nullptr;
};

// The number of colour channels each mode's pixels use.
int colorChannelCount(int mode)
{
    switch (mode) {
    case ModeRgb:
    case ModeLab:
        return 3;
    case ModeCmyk:
        return 4;
    default: // bitmap, greyscale, indexed, duotone
        return 1;
    }
}

void labToRgb(float l, float a, float b, quint16 *rgb)
{
    // CIE Lab (D50) -> XYZ -> sRGB (Bradford-adapted to D65).
    const float fy = (l + 16.0f) / 116.0f;
    const float fx = fy + a / 500.0f;
    const float fz = fy - b / 200.0f;
    auto inverse = [](float t) {
        return t > 6.0f / 29.0f ? t * t * t : 3.0f * (6.0f / 29.0f) * (6.0f / 29.0f) * (t - 4.0f / 29.0f);
    };
    const float x = 0.96422f * inverse(fx);
    const float y = inverse(fy);
    const float z = 0.82521f * inverse(fz);
    rgb[0] = encodeSrgb(3.1338561f * x - 1.6168667f * y - 0.4906146f * z);
    rgb[1] = encodeSrgb(-0.9787684f * x + 1.9161415f * y + 0.0334540f * z);
    rgb[2] = encodeSrgb(0.0719453f * x - 0.2289914f * y + 1.4052427f * z);
}

// Turns the composite's planes into RGB(A).
// Greyscale documents carry a grey ICC profile (e.g. Dot Gain 15%) that Photoshop shows them
// through; this is that mapping to sRGB, or empty when it isn't available (Qt before 6.8).
std::vector<quint16> greyToSrgb(const Document &doc)
{
    std::vector<quint16> lut;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (doc.mode != ModeGrayscale || doc.depth == 32 || doc.icc.isEmpty()) {
        return lut;
    }
    const QColorSpace space = QColorSpace::fromIccProfile(doc.icc);
    if (!space.isValid() || space.colorModel() != QColorSpace::ColorModel::Gray) {
        return lut;
    }
    QImage ramp(65536, 1, QImage::Format_Grayscale16);
    auto *values = reinterpret_cast<quint16 *>(ramp.scanLine(0));
    for (int i = 0; i < 65536; ++i) {
        values[i] = quint16(i);
    }
    ramp.setColorSpace(space);
    const QImage rgb = ramp.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBX64);
    if (rgb.isNull()) {
        return lut;
    }
    const auto *converted = reinterpret_cast<const QRgba64 *>(rgb.constScanLine(0));
    lut.resize(65536);
    for (int i = 0; i < 65536; ++i) {
        lut[size_t(i)] = converted[i].green();
    }
#else
    Q_UNUSED(doc)
#endif
    return lut;
}

// Removes Photoshop's white matte from a composite colour: it stores colours blended over white.
quint16 unmatte(quint16 value, quint16 alpha)
{
    if (alpha == 0) {
        return 0;
    }
    const qint64 v = (qint64(value) - (65535 - alpha)) * 65535 / alpha;
    return quint16(qBound(qint64(0), v, qint64(65535)));
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
// CMYK through the document's ICC profile, as Photoshop shows it. Null if the profile can't be used.
QImage cmykThroughProfile(const Document &doc, const std::vector<Plane> &planes, int alphaChannel)
{
    if (doc.mode != ModeCmyk || doc.icc.isEmpty() || planes.size() < 4) {
        return QImage();
    }
    const QColorSpace space = QColorSpace::fromIccProfile(doc.icc);
    if (!space.isValid() || space.colorModel() != QColorSpace::ColorModel::Cmyk) {
        return QImage();
    }
    const int width = planes.front().width;
    const int height = planes.front().height;
    QImage cmyk(width, height, QImage::Format_CMYK8888);
    if (cmyk.isNull()) {
        return cmyk;
    }
    for (int y = 0; y < height; ++y) {
        uchar *line = cmyk.scanLine(y);
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 4; ++c) {
                line[x * 4 + c] = uchar(255 - (planes[size_t(c)].at(x, y) >> 8)); // stored inverted
            }
        }
    }
    cmyk.setColorSpace(space);
    QImage rgb = cmyk.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGB32);
    if (rgb.isNull() || alphaChannel < 0 || size_t(alphaChannel) >= planes.size()) {
        return rgb;
    }
    rgb = rgb.convertToFormat(QImage::Format_ARGB32);
    const Plane &alpha = planes[size_t(alphaChannel)];
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(rgb.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const quint16 a = alpha.at(x, y);
            const QRgb c = line[x];
            line[x] = qRgba(unmatte(quint16(qRed(c) * 257), a) >> 8, unmatte(quint16(qGreen(c) * 257), a) >> 8,
                            unmatte(quint16(qBlue(c) * 257), a) >> 8, a >> 8);
        }
    }
    return rgb;
}
#endif

QImage assemble(const Document &doc, const std::vector<Plane> &planes, int alphaChannel)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    {
        QImage viaProfile = cmykThroughProfile(doc, planes, alphaChannel);
        if (!viaProfile.isNull()) {
            return viaProfile;
        }
    }
#endif
    const std::vector<quint16> greyLut = greyToSrgb(doc);
    const int width = planes.front().width;
    const int height = planes.front().height;
    const bool alphaPlane = alphaChannel >= 0 && size_t(alphaChannel) < planes.size();
    const bool alpha = alphaPlane || (doc.mode == ModeIndexed && doc.transparentIndex >= 0);
    const bool deep = doc.depth >= 16;
    QImage image(width, height, deep ? (alpha ? QImage::Format_RGBA64 : QImage::Format_RGBX64)
                                     : (alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32));
    if (image.isNull()) {
        return image;
    }
    auto channel = [&](int c, int x, int y) -> quint16 {
        return size_t(c) < planes.size() ? planes[size_t(c)].at(x, y) : quint16(0);
    };
    const uchar *palette = reinterpret_cast<const uchar *>(doc.palette.constData());
    for (int y = 0; y < height; ++y) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            quint16 rgb[3];
            switch (doc.mode) {
            case ModeRgb:
                rgb[0] = channel(0, x, y);
                rgb[1] = channel(1, x, y);
                rgb[2] = channel(2, x, y);
                break;
            case ModeCmyk: {
                // Stored inverted: 65535 is no ink.
                const quint32 k = channel(3, x, y);
                for (int i = 0; i < 3; ++i) {
                    rgb[i] = quint16((quint32(channel(i, x, y)) * k + 32767) / 65535);
                }
                break;
            }
            case ModeLab:
                labToRgb(channel(0, x, y) * (100.0f / 65535.0f), channel(1, x, y) * (255.0f / 65535.0f) - 128.0f,
                         channel(2, x, y) * (255.0f / 65535.0f) - 128.0f, rgb);
                break;
            case ModeIndexed: {
                const int index = channel(0, x, y) >> 8;
                for (int i = 0; i < 3; ++i) {
                    rgb[i] = palette ? quint16(palette[i * 256 + index] * 257) : quint16(index * 257);
                }
                break;
            }
            default: // bitmap, greyscale, duotone, multichannel (its first channel)
                rgb[0] = rgb[1] = rgb[2] = greyLut.empty() ? channel(0, x, y) : greyLut[channel(0, x, y)];
                break;
            }
            quint16 a = alphaPlane ? channel(alphaChannel, x, y) : 65535;
            if (alphaPlane && a < 65535) {
                for (quint16 &v : rgb) {
                    v = unmatte(v, a);
                }
            }
            if (doc.mode == ModeIndexed && doc.transparentIndex >= 0 && (channel(0, x, y) >> 8) == doc.transparentIndex) {
                a = 0;
            }
            if (deep) {
                reinterpret_cast<QRgba64 *>(line)[x] = qRgba64(rgb[0], rgb[1], rgb[2], a);
            } else {
                reinterpret_cast<QRgb *>(line)[x] = qRgba(rgb[0] >> 8, rgb[1] >> 8, rgb[2] >> 8, a >> 8);
            }
        }
    }
    return image;
}

QImage readComposite(QFile &file, const Document &doc, int step, QString *error)
{
    Reader layerReader(file);
    const LayerSection layers = scanLayerSection(layerReader, doc);
    Reader r(file);
    r.seek(doc.imageDataStart);
    const int compression = r.u16();
    QByteArray data = r.bytes(r.size() - r.pos());
    if (!r.ok()) {
        fail(error, QStringLiteral("Truncated PSD image data"));
        return QImage();
    }

    const int colorChannels = colorChannelCount(doc.mode);
    // Indexed transparency comes from the palette; for the others the first extra channel is the
    // composite's transparency if the layer section says so (otherwise it's a saved selection).
    const int alphaChannel = layers.mergedTransparency && doc.channels > colorChannels && doc.mode != ModeIndexed
            && doc.mode != ModeMultichannel ? colorChannels : -1;
    const int channelsToDecode = alphaChannel >= 0 ? colorChannels + 1 : qMin(colorChannels, doc.channels);

    Sampling sampling;
    sampling.step = step;
    std::vector<bool> linear(size_t(channelsToDecode), true);
    if (alphaChannel >= 0) {
        linear[size_t(alphaChannel)] = false;
    }
    std::vector<Plane> planes;
    PlaneDecoder decoder(doc.width, doc.height, doc.depth, doc.large, sampling);
    if (!decoder.decode(reinterpret_cast<const uchar *>(data.constData()), data.size(), compression, doc.channels,
                        channelsToDecode, linear, &planes)) {
        fail(error, QStringLiteral("Could not decode the PSD composite (compression %1)").arg(compression));
        return QImage();
    }
    data = QByteArray(); // free the file data before building the image
    return assemble(doc, planes, alphaChannel);
}

// ----------------------------------------------------------------------------------------------
// Layers, for documents without a composite

struct LayerChannel
{
    int id = 0;
    qint64 offset = 0; // compression field
    qint64 length = 0;
};

struct Layer
{
    QRect rect;
    QRect maskRect;
    int maskDefault = 0;
    bool maskEnabled = false;
    std::vector<LayerChannel> channels;
    int opacity = 255;
    int fill = 255;
    bool clipped = false;
    bool hidden = false;
    int sectionType = 0; // 1/2: group (open/closed), 3: end of group
    bool visible = true; // including the groups it is in
};

QRect readRect(Reader &r)
{
    const int top = r.i32();
    const int left = r.i32();
    const int bottom = r.i32();
    const int right = r.i32();
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

bool readLayers(QFile &file, const Document &doc, std::vector<Layer> *layers)
{
    Reader r(file);
    const LayerSection section = scanLayerSection(r, doc);
    if (section.infoStart == 0) {
        return false;
    }
    const qint64 infoEnd = section.infoEnd;
    r.seek(section.infoStart + 2); // past the layer count
    const int count = qAbs(section.count);
    layers->resize(size_t(count));
    for (Layer &layer : *layers) {
        layer.rect = readRect(r);
        const int channelCount = r.u16();
        if (!r.ok() || channelCount > 64) {
            return false;
        }
        layer.channels.resize(size_t(channelCount));
        for (LayerChannel &channel : layer.channels) {
            channel.id = r.i16();
            channel.length = r.length(doc.large);
        }
        r.tag(); // "8BIM"
        const QByteArray blendMode = r.tag();
        Q_UNUSED(blendMode)
        layer.opacity = r.u8();
        layer.clipped = r.u8() != 0;
        layer.hidden = (r.u8() & 0x02) != 0;
        r.u8();
        const qint64 extraLength = r.u32();
        const qint64 extraEnd = r.pos() + extraLength;

        const qint64 maskLength = r.u32();
        const qint64 maskEnd = r.pos() + maskLength;
        if (maskLength >= 18) {
            layer.maskRect = readRect(r);
            layer.maskDefault = r.u8();
            const int flags = r.u8();
            layer.maskEnabled = (flags & 0x02) == 0;
        }
        r.seek(maskEnd);
        const qint64 rangesLength = r.u32();
        r.skip(rangesLength);
        const int nameLength = r.u8();
        r.skip(nameLength + (3 - nameLength % 4)); // padded to 4 including the length byte

        while (r.ok() && r.pos() + 12 <= extraEnd) {
            alignToNextBlock(r, extraEnd);
            if (!isSignature(r.tag())) {
                break;
            }
            const QByteArray key = r.tag();
            const qint64 length = r.length(doc.large && hasWideLength(key));
            const qint64 blockEnd = r.pos() + length;
            if ((key == "lsct" || key == "lsdk") && length >= 4) {
                layer.sectionType = int(r.u32());
            } else if (key == "iOpa" && length >= 1) {
                layer.fill = r.u8();
            }
            r.seek(blockEnd);
        }
        r.seek(extraEnd);
        if (!r.ok()) {
            return false;
        }
    }

    // Channel data follows the records in the same order.
    qint64 offset = r.pos();
    for (Layer &layer : *layers) {
        for (LayerChannel &channel : layer.channels) {
            channel.offset = offset;
            offset += channel.length;
        }
    }
    if (offset > infoEnd + 4) {
        return false;
    }

    // Groups: records run bottom to top and a group's own record comes after its contents, so
    // walk top-down to know which groups each layer is in.
    std::vector<bool> groups;
    for (auto it = layers->rbegin(); it != layers->rend(); ++it) {
        if (it->sectionType == 1 || it->sectionType == 2) {
            groups.push_back(!it->hidden);
            it->visible = false;
            continue;
        }
        if (it->sectionType == 3) {
            if (!groups.empty()) {
                groups.pop_back();
            }
            it->visible = false;
            continue;
        }
        it->visible = !it->hidden && std::all_of(groups.begin(), groups.end(), [](bool v) { return v; });
    }
    return true;
}

// A layer channel decoded at the document's sampling grid.
struct GridPlane
{
    Plane plane;
    int gridX = 0; // position of plane sample (0,0) on the output grid
    int gridY = 0;
    bool valid = false;
};

GridPlane readLayerChannel(QFile &file, const Document &doc, const LayerChannel &channel, const QRect &rect,
                           int step, bool linear)
{
    GridPlane result;
    if (rect.isEmpty() || channel.length < 2) {
        return result;
    }
    Reader r(file);
    r.seek(channel.offset);
    const int compression = r.u16();
    const QByteArray data = r.bytes(channel.length - 2);
    if (!r.ok()) {
        return result;
    }
    Sampling sampling;
    sampling.step = step;
    sampling.firstColumn = phase(rect.left(), step);
    sampling.firstRow = phase(rect.top(), step);
    PlaneDecoder decoder(rect.width(), rect.height(), doc.depth, doc.large, sampling);
    std::vector<Plane> planes;
    if (!decoder.decode(reinterpret_cast<const uchar *>(data.constData()), data.size(), compression, 1, 1,
                        std::vector<bool>{linear}, &planes)) {
        return result;
    }
    result.plane = std::move(planes.front());
    result.gridX = (rect.left() + sampling.firstColumn) / step;
    result.gridY = (rect.top() + sampling.firstRow) / step;
    result.valid = true;
    return result;
}

// Coverage of a layer (transparency x mask) on the output grid, before opacity.
struct Coverage
{
    QRect gridRect;
    std::vector<float> values;
    float at(int gx, int gy) const
    {
        if (!gridRect.contains(gx, gy)) {
            return 0.0f;
        }
        return values[size_t(gy - gridRect.top()) * size_t(gridRect.width()) + size_t(gx - gridRect.left())];
    }
};

// unrendered: set when a visible layer has no pixels to blend (shape, text, fill or adjustment
// layers, whose look comes from data this reader doesn't interpret).
QImage compositeLayers(QFile &file, const Document &doc, const std::vector<Layer> &layers, int step, bool *unrendered)
{
    *unrendered = false;
    const int outW = (doc.width + step - 1) / step;
    const int outH = (doc.height + step - 1) / step;
    QImage canvas(outW, outH, QImage::Format_RGBA64);
    if (canvas.isNull()) {
        return canvas;
    }
    canvas.fill(Qt::transparent);
    const bool grey = doc.mode != ModeRgb;
    const int colorChannels = grey ? 1 : 3;
    Coverage base; // the layer clipped layers are clipped to

    for (const Layer &layer : layers) {
        if (layer.sectionType != 0 || layer.rect.isEmpty()) {
            if (layer.sectionType == 0 && layer.visible) {
                *unrendered = true;
            }
            if (!layer.clipped) {
                base = Coverage();
            }
            continue;
        }
        const QRect gridRect(QPoint((layer.rect.left() + phase(layer.rect.left(), step)) / step,
                                    (layer.rect.top() + phase(layer.rect.top(), step)) / step),
                             QPoint((layer.rect.right() - (((layer.rect.right() % step) + step) % step)) / step,
                                    (layer.rect.bottom() - (((layer.rect.bottom() % step) + step) % step)) / step));
        const QRect visibleGrid = gridRect.intersected(QRect(0, 0, outW, outH));

        if (!layer.visible) {
            if (!layer.clipped) {
                base = Coverage(); // layers clipped to a hidden layer are hidden too
            }
            continue;
        }
        GridPlane color[3];
        GridPlane alpha;
        GridPlane mask;
        {
            for (const LayerChannel &channel : layer.channels) {
                if (channel.id >= 0 && channel.id < colorChannels) {
                    color[channel.id] = readLayerChannel(file, doc, channel, layer.rect, step, true);
                } else if (channel.id == -1) {
                    alpha = readLayerChannel(file, doc, channel, layer.rect, step, false);
                } else if (channel.id == -2 && layer.maskEnabled) {
                    mask = readLayerChannel(file, doc, channel, layer.maskRect, step, false);
                }
            }
        }

        // Coverage of this layer.
        Coverage coverage;
        coverage.gridRect = visibleGrid;
        coverage.values.assign(size_t(qMax(0, visibleGrid.width())) * size_t(qMax(0, visibleGrid.height())), 1.0f);
        for (int gy = visibleGrid.top(); gy <= visibleGrid.bottom(); ++gy) {
            for (int gx = visibleGrid.left(); gx <= visibleGrid.right(); ++gx) {
                float a = 1.0f;
                if (alpha.valid) {
                    const int px = gx - alpha.gridX;
                    const int py = gy - alpha.gridY;
                    a = px >= 0 && py >= 0 && px < alpha.plane.width && py < alpha.plane.height
                        ? alpha.plane.at(px, py) / 65535.0f : 0.0f;
                }
                if (layer.maskEnabled && !layer.maskRect.isNull()) {
                    float m = layer.maskDefault / 255.0f;
                    if (mask.valid) {
                        const int px = gx - mask.gridX;
                        const int py = gy - mask.gridY;
                        if (px >= 0 && py >= 0 && px < mask.plane.width && py < mask.plane.height) {
                            m = mask.plane.at(px, py) / 65535.0f;
                        }
                    }
                    a *= m;
                }
                coverage.values[size_t(gy - visibleGrid.top()) * size_t(visibleGrid.width()) + size_t(gx - visibleGrid.left())] = a;
            }
        }

        if (!color[0].valid) {
            *unrendered = true;
        } else {
            const float opacity = (layer.opacity / 255.0f) * (layer.fill / 255.0f);
            for (int gy = visibleGrid.top(); gy <= visibleGrid.bottom(); ++gy) {
                auto *line = reinterpret_cast<QRgba64 *>(canvas.scanLine(gy));
                for (int gx = visibleGrid.left(); gx <= visibleGrid.right(); ++gx) {
                    float a = coverage.at(gx, gy) * opacity;
                    if (layer.clipped) {
                        a *= base.at(gx, gy);
                    }
                    if (a <= 0.0f) {
                        continue;
                    }
                    float src[3];
                    for (int c = 0; c < 3; ++c) {
                        const GridPlane &p = color[grey ? 0 : c];
                        const int px = gx - p.gridX;
                        const int py = gy - p.gridY;
                        src[c] = p.valid && px >= 0 && py >= 0 && px < p.plane.width && py < p.plane.height
                            ? p.plane.at(px, py) / 65535.0f : 0.0f;
                    }
                    const QRgba64 dst = line[gx];
                    const float da = dst.alpha() / 65535.0f;
                    const float outA = a + da * (1.0f - a);
                    const float dstWeight = da * (1.0f - a);
                    const float d[3] = {dst.red() / 65535.0f, dst.green() / 65535.0f, dst.blue() / 65535.0f};
                    quint16 out[3];
                    for (int c = 0; c < 3; ++c) {
                        out[c] = sixteenBitsFromFloat((src[c] * a + d[c] * dstWeight) / outA);
                    }
                    line[gx] = qRgba64(out[0], out[1], out[2], sixteenBitsFromFloat(outA));
                }
            }
        }
        if (!layer.clipped) {
            base = std::move(coverage);
        }
    }
    return doc.depth >= 16 ? canvas : canvas.convertToFormat(QImage::Format_ARGB32);
}

int stepFor(const Document &doc, const QSize &targetSize)
{
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        return 1;
    }
    return qMax(1, qMin(doc.width / targetSize.width(), doc.height / targetSize.height()));
}

QString sourceFormatText(const Document &doc)
{
    QString text = QStringLiteral("%1 %2-bit %3").arg(doc.large ? QStringLiteral("PSB") : QStringLiteral("PSD"))
                       .arg(doc.depth).arg(colorModeName(doc.mode));
    if (doc.depth == 32) {
        text += QStringLiteral(" (linear, shown as sRGB)");
    }
    return text;
}

QImage decodePreview(const Document &doc)
{
    QByteArray bytes = doc.previewJpeg;
    QBuffer buffer(&bytes);
    QImageReader reader(&buffer, "jpeg");
    return reader.read();
}

bool isUniform(const QImage &image)
{
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA64);
    const QRgba64 first = reinterpret_cast<const QRgba64 *>(rgba.constScanLine(0))[0];
    for (int y = 0; y < rgba.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgba64 *>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x) {
            if (line[x] != first) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace Psd {

bool isPsdFile(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix();
    return suffix.compare(QStringLiteral("psd"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("psb"), Qt::CaseInsensitive) == 0;
}

QImage read(const QString &filePath, QString *error, const QSize &targetSize)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return QImage();
    }
    Document doc;
    if (!readDocument(file, &doc, error)) {
        return QImage();
    }

    // Thumbnails: Photoshop's own preview (up to 160 px) is enough for small ones, and it also
    // shows files without a composite correctly.
    if (targetSize.isValid() && !doc.previewJpeg.isEmpty()
        && doc.previewSize.width() >= qMin(targetSize.width(), doc.width)
        && doc.previewSize.height() >= qMin(targetSize.height(), doc.height)) {
        QImage preview = decodePreview(doc);
        if (!preview.isNull()) {
            preview.setText(ColorTransform::sourceFormatTextKey(), sourceFormatText(doc));
            return preview;
        }
    }

    if (qint64(doc.width) * doc.height > MaxPixels && !targetSize.isValid()) {
        fail(error, QStringLiteral("PSD is too large to show (%1 × %2)").arg(doc.width).arg(doc.height));
        return QImage();
    }

    const int step = stepFor(doc, targetSize);
    QImage image;
    if (!doc.hasComposite && (doc.mode == ModeRgb || doc.mode == ModeGrayscale)) {
        // Saved without "Maximize compatibility": the stored composite is blank or a "this file
        // was saved without a composite" notice, so blend the layers.
        std::vector<Layer> layers;
        bool unrendered = false;
        if (readLayers(file, doc, &layers) && !layers.empty()) {
            image = compositeLayers(file, doc, layers, step, &unrendered);
        }
        if ((image.isNull() || (unrendered && isUniform(image))) && !doc.previewJpeg.isEmpty()) {
            // Nothing drawable (e.g. only shape, text or fill layers): Photoshop's small preview
            // is better than a blank page.
            const QImage preview = decodePreview(doc);
            if (!preview.isNull()) {
                image = preview.scaled(QSize((doc.width + step - 1) / step, (doc.height + step - 1) / step),
                                       Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
    }
    if (image.isNull()) {
        image = readComposite(file, doc, step, error);
    }
    if (image.isNull()) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("Could not read PSD");
        }
        return image;
    }

    if (doc.mode == ModeRgb && doc.depth != 32 && !doc.icc.isEmpty()) {
        const QColorSpace space = QColorSpace::fromIccProfile(doc.icc);
        if (space.isValid() && space != QColorSpace(QColorSpace::SRgb)) {
            image.setColorSpace(space);
            image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
        }
    }
    image.setText(ColorTransform::sourceFormatTextKey(), sourceFormatText(doc));
    return image;
}

bool readInfo(const QString &filePath, Info *info, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return false;
    }
    Document doc;
    if (!readDocument(file, &doc, error)) {
        return false;
    }
    info->large = doc.large;
    info->width = doc.width;
    info->height = doc.height;
    info->depth = doc.depth;
    info->channels = doc.channels;
    info->colorMode = colorModeName(doc.mode);
    info->hasComposite = doc.hasComposite;
    Reader r(file);
    const LayerSection section = scanLayerSection(r, doc);
    info->layerCount = qAbs(section.count);
    info->mergedTransparency = section.mergedTransparency;
    if (!doc.icc.isEmpty()) {
        const QColorSpace space = QColorSpace::fromIccProfile(doc.icc);
        info->iccProfile = space.isValid() && !space.description().isEmpty() ? space.description()
                                                                               : QStringLiteral("Embedded");
    }
    info->embeddedPreview = doc.previewSize;
    return true;
}

} // namespace Psd
