#include "Metadata.h"

#include "DpxReader.h"
#include "ExrReader.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QStringList>

#include <cmath>
#include <cstring>

#ifdef ONION_HAVE_OPENEXR
#include <ImfBoxAttribute.h>
#include <ImfChannelListAttribute.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfCompressionAttribute.h>
#include <ImfDoubleAttribute.h>
#include <ImfEnvmapAttribute.h>
#include <ImfFloatAttribute.h>
#include <ImfHeader.h>
#include <ImfIntAttribute.h>
#include <ImfKeyCodeAttribute.h>
#include <ImfLineOrderAttribute.h>
#include <ImfMatrixAttribute.h>
#include <ImfMultiPartInputFile.h>
#include <ImfPreviewImageAttribute.h>
#include <ImfRationalAttribute.h>
#include <ImfStringAttribute.h>
#include <ImfStringVectorAttribute.h>
#include <ImfTileDescriptionAttribute.h>
#include <ImfTimeCodeAttribute.h>
#include <ImfVecAttribute.h>

#include <exception>
#endif

namespace Metadata {
namespace {

QString number(double value)
{
    // Up to 6 significant digits, without trailing zeros.
    return QString::number(value, 'g', 6);
}

QString fileSizeText(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 bytes").arg(bytes);
    }
    return QStringLiteral("%1 (%2 bytes)").arg(QLocale::system().formattedDataSize(bytes),
                                               QLocale::system().toString(bytes));
}

void addFileEntries(QList<Entry> &entries, const QFileInfo &info)
{
    const QString group = QStringLiteral("File");
    entries.append({group, QStringLiteral("Name"), info.fileName()});
    entries.append({group, QStringLiteral("Folder"), QDir::toNativeSeparators(info.absolutePath())});
    entries.append({group, QStringLiteral("Size"), fileSizeText(info.size())});
    entries.append({group, QStringLiteral("Modified"), QLocale::system().toString(info.lastModified(), QLocale::ShortFormat)});
}

// ----------------------------------------------------------------------------------------------
// EXR

#ifdef ONION_HAVE_OPENEXR

QString compressionName(Imf::Compression compression)
{
    switch (static_cast<int>(compression)) {
    case 0: return QStringLiteral("None");
    case 1: return QStringLiteral("RLE");
    case 2: return QStringLiteral("ZIPS (1 line)");
    case 3: return QStringLiteral("ZIP (16 lines)");
    case 4: return QStringLiteral("PIZ");
    case 5: return QStringLiteral("PXR24");
    case 6: return QStringLiteral("B44");
    case 7: return QStringLiteral("B44A");
    case 8: return QStringLiteral("DWAA");
    case 9: return QStringLiteral("DWAB");
    default: return QStringLiteral("type %1").arg(static_cast<int>(compression));
    }
}

QString channelText(const Imf::ChannelList &channels)
{
    QStringList parts;
    for (auto it = channels.begin(); it != channels.end(); ++it) {
        const char *type = it.channel().type == Imf::HALF ? "half" : it.channel().type == Imf::FLOAT ? "float" : "uint";
        QString part = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(it.name()), QString::fromLatin1(type));
        if (it.channel().xSampling != 1 || it.channel().ySampling != 1) {
            part += QStringLiteral(" %1×%2 sampled").arg(it.channel().xSampling).arg(it.channel().ySampling);
        }
        parts << part;
    }
    return parts.join(QStringLiteral(", "));
}

QString boxText(int minX, int minY, int maxX, int maxY)
{
    return QStringLiteral("(%1, %2) – (%3, %4)   %5 × %6")
        .arg(minX).arg(minY).arg(maxX).arg(maxY).arg(maxX - minX + 1).arg(maxY - minY + 1);
}

template <typename Matrix>
QString matrixText(const Matrix &m, int size)
{
    QStringList rows;
    for (int r = 0; r < size; ++r) {
        QStringList row;
        for (int c = 0; c < size; ++c) {
            row << number(m[r][c]);
        }
        rows << row.join(QLatin1Char(' '));
    }
    return rows.join(QStringLiteral(" | "));
}

QString attributeText(const Imf::Attribute &attribute)
{
    using namespace Imf;
    if (const auto *a = dynamic_cast<const StringAttribute *>(&attribute)) {
        return QString::fromUtf8(a->value().c_str());
    }
    if (const auto *a = dynamic_cast<const IntAttribute *>(&attribute)) {
        return QString::number(a->value());
    }
    if (const auto *a = dynamic_cast<const FloatAttribute *>(&attribute)) {
        return number(a->value());
    }
    if (const auto *a = dynamic_cast<const DoubleAttribute *>(&attribute)) {
        return QString::number(a->value(), 'g', 10);
    }
    if (const auto *a = dynamic_cast<const V2iAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2").arg(a->value().x).arg(a->value().y);
    }
    if (const auto *a = dynamic_cast<const V2fAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2").arg(number(a->value().x), number(a->value().y));
    }
    if (const auto *a = dynamic_cast<const V2dAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2").arg(number(a->value().x), number(a->value().y));
    }
    if (const auto *a = dynamic_cast<const V3iAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2, %3").arg(a->value().x).arg(a->value().y).arg(a->value().z);
    }
    if (const auto *a = dynamic_cast<const V3fAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2, %3").arg(number(a->value().x), number(a->value().y), number(a->value().z));
    }
    if (const auto *a = dynamic_cast<const V3dAttribute *>(&attribute)) {
        return QStringLiteral("%1, %2, %3").arg(number(a->value().x), number(a->value().y), number(a->value().z));
    }
    if (const auto *a = dynamic_cast<const Box2iAttribute *>(&attribute)) {
        const auto &b = a->value();
        return boxText(b.min.x, b.min.y, b.max.x, b.max.y);
    }
    if (const auto *a = dynamic_cast<const Box2fAttribute *>(&attribute)) {
        const auto &b = a->value();
        return QStringLiteral("(%1, %2) – (%3, %4)").arg(number(b.min.x), number(b.min.y), number(b.max.x), number(b.max.y));
    }
    if (const auto *a = dynamic_cast<const ChromaticitiesAttribute *>(&attribute)) {
        const Chromaticities &c = a->value();
        return QStringLiteral("R %1 %2 · G %3 %4 · B %5 %6 · W %7 %8")
            .arg(number(c.red.x), number(c.red.y), number(c.green.x), number(c.green.y),
                 number(c.blue.x), number(c.blue.y), number(c.white.x), number(c.white.y));
    }
    if (const auto *a = dynamic_cast<const CompressionAttribute *>(&attribute)) {
        return compressionName(a->value());
    }
    if (const auto *a = dynamic_cast<const LineOrderAttribute *>(&attribute)) {
        switch (a->value()) {
        case INCREASING_Y: return QStringLiteral("Increasing Y");
        case DECREASING_Y: return QStringLiteral("Decreasing Y");
        case RANDOM_Y: return QStringLiteral("Random Y");
        default: return QStringLiteral("%1").arg(static_cast<int>(a->value()));
        }
    }
    if (const auto *a = dynamic_cast<const ChannelListAttribute *>(&attribute)) {
        return channelText(a->value());
    }
    if (const auto *a = dynamic_cast<const TimeCodeAttribute *>(&attribute)) {
        const TimeCode &t = a->value();
        return QStringLiteral("%1:%2:%3%4%5")
            .arg(t.hours(), 2, 10, QLatin1Char('0'))
            .arg(t.minutes(), 2, 10, QLatin1Char('0'))
            .arg(t.seconds(), 2, 10, QLatin1Char('0'))
            .arg(t.dropFrame() ? QLatin1Char(';') : QLatin1Char(':'))
            .arg(t.frame(), 2, 10, QLatin1Char('0'));
    }
    if (const auto *a = dynamic_cast<const KeyCodeAttribute *>(&attribute)) {
        const KeyCode &k = a->value();
        return QStringLiteral("mfc %1, type %2, prefix %3, count %4, perf offset %5")
            .arg(k.filmMfcCode()).arg(k.filmType()).arg(k.prefix()).arg(k.count()).arg(k.perfOffset());
    }
    if (const auto *a = dynamic_cast<const RationalAttribute *>(&attribute)) {
        const Rational &r = a->value();
        return r.d == 0 ? QString::number(r.n)
                        : QStringLiteral("%1/%2 (%3)").arg(r.n).arg(r.d).arg(number(double(r.n) / double(r.d)));
    }
    if (const auto *a = dynamic_cast<const M33fAttribute *>(&attribute)) {
        return matrixText(a->value(), 3);
    }
    if (const auto *a = dynamic_cast<const M44fAttribute *>(&attribute)) {
        return matrixText(a->value(), 4);
    }
    if (const auto *a = dynamic_cast<const M33dAttribute *>(&attribute)) {
        return matrixText(a->value(), 3);
    }
    if (const auto *a = dynamic_cast<const M44dAttribute *>(&attribute)) {
        return matrixText(a->value(), 4);
    }
    if (const auto *a = dynamic_cast<const StringVectorAttribute *>(&attribute)) {
        QStringList values;
        for (const std::string &value : a->value()) {
            values << QString::fromUtf8(value.c_str());
        }
        return values.join(QStringLiteral(", "));
    }
    if (const auto *a = dynamic_cast<const PreviewImageAttribute *>(&attribute)) {
        return QStringLiteral("%1 × %2 preview").arg(a->value().width()).arg(a->value().height());
    }
    if (const auto *a = dynamic_cast<const TileDescriptionAttribute *>(&attribute)) {
        return QStringLiteral("%1 × %2 tiles").arg(a->value().xSize).arg(a->value().ySize);
    }
    if (const auto *a = dynamic_cast<const EnvmapAttribute *>(&attribute)) {
        return a->value() == ENVMAP_LATLONG ? QStringLiteral("Lat-long") : QStringLiteral("Cube");
    }
    return QStringLiteral("(%1)").arg(QString::fromLatin1(attribute.typeName()));
}

void addExrEntries(QList<Entry> &entries, const QString &filePath)
{
    try {
        const QByteArray fileName = filePath.toUtf8();
        Imf::MultiPartInputFile file(fileName.constData());
        const int parts = file.parts();
        for (int part = 0; part < parts; ++part) {
            const Imf::Header &header = file.header(part);
            QString group = QStringLiteral("EXR");
            if (parts > 1) {
                const QString name = header.hasName() ? QString::fromUtf8(header.name().c_str()) : QString();
                group = name.isEmpty() ? QStringLiteral("EXR part %1").arg(part + 1)
                                       : QStringLiteral("EXR part %1 (%2)").arg(part + 1).arg(name);
            }
            for (auto it = header.begin(); it != header.end(); ++it) {
                entries.append({group, QString::fromUtf8(it.name()), attributeText(it.attribute())});
            }
        }
    } catch (const std::exception &exception) {
        entries.append({QStringLiteral("EXR"), QStringLiteral("Error"), QString::fromUtf8(exception.what())});
    }
}

#endif

// ----------------------------------------------------------------------------------------------
// DPX (SMPTE 268M header; "undefined" fields are all ones and are left out)

class DpxHeader
{
public:
    explicit DpxHeader(const QByteArray &bytes)
        : m_bytes(bytes)
    {
        const uchar *p = data();
        m_bigEndian = m_bytes.size() >= 4 && p[0] == 'S' && p[1] == 'D' && p[2] == 'P' && p[3] == 'X';
        m_valid = m_bigEndian || (m_bytes.size() >= 4 && p[0] == 'X' && p[1] == 'P' && p[2] == 'D' && p[3] == 'S');
    }

    bool isValid() const { return m_valid && m_bytes.size() >= 2048; }
    bool isBigEndian() const { return m_bigEndian; }

    quint8 u8(int offset) const { return data()[offset]; }
    quint16 u16(int offset) const
    {
        const uchar *p = data() + offset;
        return m_bigEndian ? quint16((p[0] << 8) | p[1]) : quint16((p[1] << 8) | p[0]);
    }
    quint32 u32(int offset) const
    {
        const uchar *p = data() + offset;
        return m_bigEndian ? (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3])
                           : (quint32(p[3]) << 24) | (quint32(p[2]) << 16) | (quint32(p[1]) << 8) | quint32(p[0]);
    }
    float r32(int offset) const
    {
        const quint32 bits = u32(offset);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    QString text(int offset, int length) const
    {
        const char *p = reinterpret_cast<const char *>(data() + offset);
        int size = 0;
        while (size < length && p[size] != '\0') {
            ++size;
        }
        if (size > 0 && static_cast<uchar>(p[0]) == 0xFF) {
            return QString(); // undefined
        }
        return QString::fromLatin1(p, size).trimmed();
    }

private:
    const uchar *data() const { return reinterpret_cast<const uchar *>(m_bytes.constData()); }

    QByteArray m_bytes;
    bool m_bigEndian = true;
    bool m_valid = false;
};

QString dpxDescriptor(int value)
{
    switch (value) {
    case 0: return QStringLiteral("User defined");
    case 1: return QStringLiteral("Red");
    case 2: return QStringLiteral("Green");
    case 3: return QStringLiteral("Blue");
    case 4: return QStringLiteral("Alpha");
    case 6: return QStringLiteral("Luma (Y)");
    case 7: return QStringLiteral("Color difference");
    case 8: return QStringLiteral("Depth (Z)");
    case 9: return QStringLiteral("Composite video");
    case 50: return QStringLiteral("RGB");
    case 51: return QStringLiteral("RGBA");
    case 52: return QStringLiteral("ABGR");
    case 100: return QStringLiteral("CbYCrY 4:2:2");
    case 101: return QStringLiteral("CbYACrYA 4:2:2:4");
    case 102: return QStringLiteral("CbYCr 4:4:4");
    case 103: return QStringLiteral("CbYCrA 4:4:4:4");
    default: return QStringLiteral("%1").arg(value);
    }
}

QString dpxTransfer(int value)
{
    switch (value) {
    case 0: return QStringLiteral("User defined");
    case 1: return QStringLiteral("Printing density");
    case 2: return QStringLiteral("Linear");
    case 3: return QStringLiteral("Logarithmic");
    case 4: return QStringLiteral("Unspecified video");
    case 5: return QStringLiteral("SMPTE 274M");
    case 6: return QStringLiteral("ITU-R BT.709");
    case 7: return QStringLiteral("ITU-R BT.601 (625 lines)");
    case 8: return QStringLiteral("ITU-R BT.601 (525 lines)");
    case 9: return QStringLiteral("NTSC composite");
    case 10: return QStringLiteral("PAL composite");
    case 11: return QStringLiteral("Z linear");
    case 12: return QStringLiteral("Z homogeneous");
    case 13: return QStringLiteral("ADX");
    case 14: return QStringLiteral("ITU-R BT.2020 NCL");
    case 15: return QStringLiteral("ITU-R BT.2020 CL");
    case 16: return QStringLiteral("IEC 61966-2-4 (xvYCC)");
    case 17: return QStringLiteral("ITU-R BT.2100 PQ NCL");
    case 18: return QStringLiteral("ITU-R BT.2100 PQ ICtCp");
    case 19: return QStringLiteral("ITU-R BT.2100 HLG NCL");
    case 20: return QStringLiteral("ITU-R BT.2100 HLG ICtCp");
    case 255: return QString();
    default: return QStringLiteral("%1").arg(value);
    }
}

QString dpxOrientation(int value)
{
    switch (value) {
    case 0: return QStringLiteral("Left to right, top to bottom");
    case 1: return QStringLiteral("Right to left, top to bottom");
    case 2: return QStringLiteral("Left to right, bottom to top");
    case 3: return QStringLiteral("Right to left, bottom to top");
    case 4: return QStringLiteral("Top to bottom, left to right");
    case 5: return QStringLiteral("Top to bottom, right to left");
    case 6: return QStringLiteral("Bottom to top, left to right");
    case 7: return QStringLiteral("Bottom to top, right to left");
    default: return QStringLiteral("%1").arg(value);
    }
}

QString bcdTimecode(quint32 value)
{
    auto pair = [value](int shift) {
        return QStringLiteral("%1%2").arg((value >> (shift + 4)) & 0xF).arg((value >> shift) & 0xF);
    };
    return QStringLiteral("%1:%2:%3:%4").arg(pair(24), pair(16), pair(8), pair(0));
}

void addDpxEntries(QList<Entry> &entries, const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        entries.append({QStringLiteral("DPX"), QStringLiteral("Error"), file.errorString()});
        return;
    }
    const DpxHeader h(file.read(2048));
    if (!h.isValid()) {
        entries.append({QStringLiteral("DPX"), QStringLiteral("Error"), QStringLiteral("Not a DPX header")});
        return;
    }

    auto add = [&entries](const QString &group, const QString &key, const QString &value) {
        if (!value.isEmpty()) {
            entries.append({group, key, value});
        }
    };
    // Many writers leave unknown counts and offsets at 0 instead of "undefined", so 0 is left out.
    auto addU32 = [&](const QString &group, const QString &key, int offset) {
        const quint32 value = h.u32(offset);
        if (value != 0xFFFFFFFFu && value != 0) {
            add(group, key, QString::number(value));
        }
    };
    // Unknown floats are written as all ones (NaN) or, by many writers, as 0.
    auto addR32 = [&](const QString &group, const QString &key, int offset) {
        const float value = h.r32(offset);
        if (std::isfinite(value) && value != 0.0f) {
            add(group, key, number(value));
        }
    };

    QString group = QStringLiteral("DPX file");
    add(group, QStringLiteral("Version"), h.text(8, 8));
    add(group, QStringLiteral("Byte order"), h.isBigEndian() ? QStringLiteral("Big endian") : QStringLiteral("Little endian"));
    add(group, QStringLiteral("File name"), h.text(36, 100));
    add(group, QStringLiteral("Created"), h.text(136, 24));
    add(group, QStringLiteral("Creator"), h.text(160, 100));
    add(group, QStringLiteral("Project"), h.text(260, 200));
    add(group, QStringLiteral("Copyright"), h.text(460, 200));
    addU32(group, QStringLiteral("Image data offset"), 4);
    addU32(group, QStringLiteral("User data size"), 32); // left out when there is none

    group = QStringLiteral("DPX image");
    add(group, QStringLiteral("Orientation"), dpxOrientation(h.u16(768)));
    const int elementCount = qBound(0, int(h.u16(770)), 8);
    add(group, QStringLiteral("Size"), QStringLiteral("%1 × %2").arg(h.u32(772)).arg(h.u32(776)));
    add(group, QStringLiteral("Elements"), QString::number(elementCount));
    for (int element = 0; element < elementCount; ++element) {
        const int base = 780 + element * 72;
        const QString prefix = elementCount > 1 ? QStringLiteral("Element %1 ").arg(element + 1) : QString();
        add(group, prefix + QStringLiteral("Descriptor"), dpxDescriptor(h.u8(base + 20)));
        add(group, prefix + QStringLiteral("Transfer"), dpxTransfer(h.u8(base + 21)));
        add(group, prefix + QStringLiteral("Colorimetric"), dpxTransfer(h.u8(base + 22)));
        add(group, prefix + QStringLiteral("Bit depth"), QString::number(h.u8(base + 23)));
        const int packing = h.u16(base + 24);
        add(group, prefix + QStringLiteral("Packing"),
            packing == 0 ? QStringLiteral("Packed") : packing == 1 ? QStringLiteral("Filled (method A)")
                         : packing == 2 ? QStringLiteral("Filled (method B)") : QString::number(packing));
        add(group, prefix + QStringLiteral("Encoding"), h.u16(base + 26) == 0 ? QStringLiteral("None") : QStringLiteral("RLE"));
        add(group, prefix + QStringLiteral("Data sign"), h.u32(base) == 1 ? QStringLiteral("Signed") : QStringLiteral("Unsigned"));
        const quint32 lowCode = h.u32(base + 4);
        const quint32 highCode = h.u32(base + 12);
        if (highCode != 0xFFFFFFFFu && highCode != 0) {
            add(group, prefix + QStringLiteral("Reference code values"),
                QStringLiteral("low %1, high %2")
                    .arg(lowCode == 0xFFFFFFFFu ? QStringLiteral("–") : QString::number(lowCode),
                         highCode == 0xFFFFFFFFu ? QStringLiteral("–") : QString::number(highCode)));
        }
        add(group, prefix + QStringLiteral("Description"), h.text(base + 40, 32));
    }

    group = QStringLiteral("DPX source");
    addU32(group, QStringLiteral("X offset"), 1408);
    addU32(group, QStringLiteral("Y offset"), 1412);
    const quint32 originalWidth = h.u32(1424);
    const quint32 originalHeight = h.u32(1428);
    if (originalWidth != 0xFFFFFFFFu && originalHeight != 0xFFFFFFFFu && originalWidth != 0) {
        add(group, QStringLiteral("Original size"), QStringLiteral("%1 × %2").arg(originalWidth).arg(originalHeight));
    }
    add(group, QStringLiteral("Source file"), h.text(1432, 100));
    add(group, QStringLiteral("Source created"), h.text(1532, 24));
    add(group, QStringLiteral("Input device"), h.text(1556, 32));
    add(group, QStringLiteral("Device serial"), h.text(1588, 32));
    const quint32 aspectH = h.u32(1628);
    const quint32 aspectV = h.u32(1632);
    if (aspectH != 0xFFFFFFFFu && aspectV != 0xFFFFFFFFu && aspectH != 0 && aspectV != 0) {
        add(group, QStringLiteral("Pixel aspect"), QStringLiteral("%1:%2").arg(aspectH).arg(aspectV));
    }

    group = QStringLiteral("DPX film");
    add(group, QStringLiteral("Film manufacturer ID"), h.text(1664, 2));
    add(group, QStringLiteral("Film type"), h.text(1666, 2));
    add(group, QStringLiteral("Perf offset"), h.text(1668, 2));
    add(group, QStringLiteral("Prefix"), h.text(1670, 6));
    add(group, QStringLiteral("Count"), h.text(1676, 4));
    add(group, QStringLiteral("Format"), h.text(1680, 32));
    addU32(group, QStringLiteral("Frame position"), 1712);
    addU32(group, QStringLiteral("Sequence length"), 1716);
    addU32(group, QStringLiteral("Held count"), 1720);
    addR32(group, QStringLiteral("Frame rate"), 1724);
    addR32(group, QStringLiteral("Shutter angle"), 1728);
    add(group, QStringLiteral("Frame ID"), h.text(1732, 32));
    add(group, QStringLiteral("Slate"), h.text(1764, 100));

    group = QStringLiteral("DPX TV");
    const quint32 timecode = h.u32(1920);
    if (timecode != 0xFFFFFFFFu) {
        add(group, QStringLiteral("Timecode"), bcdTimecode(timecode));
    }
    const quint32 userBits = h.u32(1924);
    if (userBits != 0xFFFFFFFFu && userBits != 0) {
        add(group, QStringLiteral("User bits"), QStringLiteral("%1").arg(userBits, 8, 16, QLatin1Char('0')));
    }
    if (h.u8(1928) != 0xFF) {
        add(group, QStringLiteral("Interlace"), h.u8(1928) == 0 ? QStringLiteral("No") : QStringLiteral("2:1"));
    }
    addR32(group, QStringLiteral("Frame rate"), 1940);
    addR32(group, QStringLiteral("Time offset"), 1944);
    addR32(group, QStringLiteral("Gamma"), 1948);
    addR32(group, QStringLiteral("Black level"), 1952);
    addR32(group, QStringLiteral("Black gain"), 1956);
    addR32(group, QStringLiteral("Breakpoint"), 1960);
    addR32(group, QStringLiteral("White level"), 1964);
}

// ----------------------------------------------------------------------------------------------
// Everything else: what Qt's image plugins report.

void addQtEntries(QList<Entry> &entries, const QString &filePath)
{
    QImageReader reader(filePath);
    const QString group = QStringLiteral("Image");
    if (!reader.canRead()) {
        entries.append({group, QStringLiteral("Error"), reader.errorString()});
        return;
    }
    entries.append({group, QStringLiteral("Format"), QString::fromLatin1(reader.format()).toUpper()});
    const QSize size = reader.size();
    if (size.isValid()) {
        entries.append({group, QStringLiteral("Size"), QStringLiteral("%1 × %2").arg(size.width()).arg(size.height())});
    }
    if (reader.imageCount() > 1) {
        entries.append({group, QStringLiteral("Images in file"), QString::number(reader.imageCount())});
    }
    const QStringList keys = reader.textKeys();
    for (const QString &key : keys) {
        const QString value = reader.text(key).trimmed();
        if (!value.isEmpty()) {
            entries.append({QStringLiteral("Text"), key, value});
        }
    }
}

} // namespace

QList<Entry> read(const QString &filePath)
{
    QList<Entry> entries;
    const QFileInfo info(filePath);
    if (!info.isFile()) {
        return entries;
    }
    addFileEntries(entries, info);
    if (Dpx::isDpxFile(filePath)) {
        addDpxEntries(entries, filePath);
    } else if (Exr::isExrFile(filePath)) {
#ifdef ONION_HAVE_OPENEXR
        addExrEntries(entries, filePath);
#else
        entries.append({QStringLiteral("EXR"), QStringLiteral("Error"), QStringLiteral("Built without OpenEXR")});
#endif
    } else {
        addQtEntries(entries, filePath);
    }
    return entries;
}

QString toText(const QList<Entry> &entries)
{
    QStringList lines;
    for (const Entry &entry : entries) {
        lines << QStringLiteral("%1\t%2\t%3").arg(entry.group, entry.key, entry.value);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace Metadata
