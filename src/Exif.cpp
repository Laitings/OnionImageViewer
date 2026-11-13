#include "Exif.h"

#include <QStringList>

namespace Exif {
namespace {

enum Type : quint16 {
    Byte = 1,
    Ascii = 2,
    Short = 3,
    Long = 4,
    Rational = 5,
    SignedByte = 6,
    Undefined = 7,
    SignedShort = 8,
    SignedLong = 9,
    SignedRational = 10,
};

int typeSize(quint16 type)
{
    switch (type) {
    case Byte: case Ascii: case SignedByte: case Undefined: return 1;
    case Short: case SignedShort: return 2;
    case Long: case SignedLong: return 4;
    case Rational: case SignedRational: return 8;
    default: return 0;
    }
}

struct Field
{
    quint16 tag = 0;
    quint16 type = 0;
    quint32 count = 0;
    qsizetype offset = -1; // where the values are in the data
};

class Tiff final
{
public:
    explicit Tiff(const QByteArray &data)
        : m_data(data)
    {
    }

    bool open()
    {
        if (m_data.size() < 8) {
            return false;
        }
        if (m_data.startsWith("II")) {
            m_little = true;
        } else if (m_data.startsWith("MM")) {
            m_little = false;
        } else {
            return false;
        }
        return u16(2) == 42;
    }

    quint32 firstIfd() const { return u32(4); }

    bool inRange(qsizetype offset, qsizetype size) const
    {
        return offset >= 0 && size >= 0 && offset <= m_data.size() - size;
    }

    quint16 u16(qsizetype offset) const
    {
        if (!inRange(offset, 2)) {
            return 0;
        }
        const auto *p = reinterpret_cast<const uchar *>(m_data.constData() + offset);
        return m_little ? static_cast<quint16>(p[0] | (p[1] << 8)) : static_cast<quint16>((p[0] << 8) | p[1]);
    }

    quint32 u32(qsizetype offset) const
    {
        if (!inRange(offset, 4)) {
            return 0;
        }
        const auto *p = reinterpret_cast<const uchar *>(m_data.constData() + offset);
        return m_little ? (quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24))
                        : ((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]));
    }

    // The entries of the IFD at offset (empty if it's out of range).
    QList<Field> ifd(quint32 offset) const
    {
        QList<Field> fields;
        const qsizetype start = static_cast<qsizetype>(offset);
        if (offset == 0 || !inRange(start, 2)) {
            return fields;
        }
        const int count = u16(start);
        if (!inRange(start + 2, qsizetype(count) * 12)) {
            return fields;
        }
        for (int i = 0; i < count; ++i) {
            const qsizetype entry = start + 2 + qsizetype(i) * 12;
            Field field;
            field.tag = u16(entry);
            field.type = u16(entry + 2);
            field.count = u32(entry + 4);
            const qint64 bytes = qint64(typeSize(field.type)) * field.count;
            if (bytes <= 0 || bytes > m_data.size()) {
                continue;
            }
            field.offset = bytes <= 4 ? entry + 8 : static_cast<qsizetype>(u32(entry + 8));
            if (inRange(field.offset, static_cast<qsizetype>(bytes))) {
                fields.append(field);
            }
        }
        return fields;
    }

    QString text(const Field &field) const
    {
        QByteArray bytes = m_data.mid(field.offset, static_cast<qsizetype>(field.count));
        const qsizetype end = bytes.indexOf('\0');
        if (end >= 0) {
            bytes.truncate(end);
        }
        return QString::fromUtf8(bytes).trimmed();
    }

    // Value i of a numeric field as a double (rationals divided out).
    double number(const Field &field, quint32 i = 0) const
    {
        if (i >= field.count) {
            return 0.0;
        }
        const qsizetype at = field.offset + qsizetype(i) * typeSize(field.type);
        switch (field.type) {
        case Byte: case Undefined: return static_cast<uchar>(m_data[at]);
        case SignedByte: return static_cast<signed char>(m_data[at]);
        case Short: return u16(at);
        case SignedShort: return static_cast<qint16>(u16(at));
        case Long: return u32(at);
        case SignedLong: return static_cast<qint32>(u32(at));
        case Rational: {
            const quint32 denominator = u32(at + 4);
            return denominator ? double(u32(at)) / denominator : 0.0;
        }
        case SignedRational: {
            const qint32 denominator = static_cast<qint32>(u32(at + 4));
            return denominator ? double(static_cast<qint32>(u32(at))) / denominator : 0.0;
        }
        default: return 0.0;
        }
    }

    bool isNumeric(const Field &field) const { return field.type != Ascii && typeSize(field.type) > 0; }

private:
    QByteArray m_data;
    bool m_little = true;
};

const Field *findField(const QList<Field> &fields, quint16 tag)
{
    for (const Field &field : fields) {
        if (field.tag == tag) {
            return &field;
        }
    }
    return nullptr;
}

QString number(double value, int decimals = 1)
{
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text;
}

QString exposureTime(double seconds)
{
    if (seconds <= 0.0) {
        return QString();
    }
    if (seconds < 0.5) {
        return QStringLiteral("1/%1 s").arg(qRound(1.0 / seconds));
    }
    return QStringLiteral("%1 s").arg(number(seconds));
}

QString orientationText(int value)
{
    switch (value) {
    case 1: return QStringLiteral("Normal");
    case 2: return QStringLiteral("Mirrored horizontally");
    case 3: return QStringLiteral("Rotated 180°");
    case 4: return QStringLiteral("Mirrored vertically");
    case 5: return QStringLiteral("Mirrored, rotated 90° counter-clockwise");
    case 6: return QStringLiteral("Rotated 90° clockwise");
    case 7: return QStringLiteral("Mirrored, rotated 90° clockwise");
    case 8: return QStringLiteral("Rotated 90° counter-clockwise");
    default: return QString::number(value);
    }
}

QString exposureProgramText(int value)
{
    switch (value) {
    case 1: return QStringLiteral("Manual");
    case 2: return QStringLiteral("Program");
    case 3: return QStringLiteral("Aperture priority");
    case 4: return QStringLiteral("Shutter priority");
    case 5: return QStringLiteral("Creative");
    case 6: return QStringLiteral("Action");
    case 7: return QStringLiteral("Portrait");
    case 8: return QStringLiteral("Landscape");
    default: return QString();
    }
}

QString meteringText(int value)
{
    switch (value) {
    case 1: return QStringLiteral("Average");
    case 2: return QStringLiteral("Center-weighted");
    case 3: return QStringLiteral("Spot");
    case 4: return QStringLiteral("Multi-spot");
    case 5: return QStringLiteral("Pattern");
    case 6: return QStringLiteral("Partial");
    default: return QString();
    }
}

// Degrees, minutes, seconds as decimal degrees with the hemisphere ("59.91273° N").
QString gpsCoordinate(const Tiff &tiff, const Field *value, const Field *reference)
{
    if (!value || value->count < 3 || !tiff.isNumeric(*value)) {
        return QString();
    }
    const double degrees = tiff.number(*value, 0) + tiff.number(*value, 1) / 60.0 + tiff.number(*value, 2) / 3600.0;
    const QString hemisphere = reference ? tiff.text(*reference) : QString();
    return QStringLiteral("%1° %2").arg(QString::number(degrees, 'f', 6), hemisphere).trimmed();
}

} // namespace

QDateTime dateTaken(const QByteArray &tiffData)
{
    QByteArray data = tiffData;
    if (data.startsWith(QByteArray("Exif\0\0", 6))) {
        data.remove(0, 6);
    }
    Tiff opened(data);
    if (!opened.open()) {
        return QDateTime();
    }
    const QList<Field> main = opened.ifd(opened.firstIfd());
    QList<Field> exif;
    if (const Field *pointer = findField(main, 0x8769)) {
        exif = opened.ifd(static_cast<quint32>(opened.number(*pointer)));
    }
    const auto parse = [&](const QList<Field> &fields, quint16 tag) {
        const Field *field = findField(fields, tag);
        if (!field || field->type != Ascii) {
            return QDateTime();
        }
        return QDateTime::fromString(opened.text(*field).left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
    };
    QDateTime when = parse(exif, 0x9003);
    if (!when.isValid()) {
        when = parse(exif, 0x9004);
    }
    if (!when.isValid()) {
        when = parse(main, 0x0132);
    }
    return when;
}

QList<QPair<QString, QString>> describe(const QByteArray &tiffData)
{
    QList<QPair<QString, QString>> facts;
    QByteArray data = tiffData;
    if (data.startsWith(QByteArray("Exif\0\0", 6))) {
        data.remove(0, 6);
    }
    Tiff opened(data);
    if (!opened.open()) {
        return facts;
    }

    const QList<Field> main = opened.ifd(opened.firstIfd());
    QList<Field> exif;
    QList<Field> gps;
    if (const Field *pointer = findField(main, 0x8769)) {
        exif = opened.ifd(static_cast<quint32>(opened.number(*pointer)));
    }
    if (const Field *pointer = findField(main, 0x8825)) {
        gps = opened.ifd(static_cast<quint32>(opened.number(*pointer)));
    }

    const auto addText = [&](const QList<Field> &fields, quint16 tag, const QString &name) {
        if (const Field *field = findField(fields, tag)) {
            const QString value = field->type == Ascii ? opened.text(*field) : QString();
            if (!value.isEmpty()) {
                facts.append({name, value});
            }
        }
    };
    const auto numberOf = [&](const QList<Field> &fields, quint16 tag, double *value) {
        const Field *field = findField(fields, tag);
        if (!field || !opened.isNumeric(*field)) {
            return false;
        }
        *value = opened.number(*field);
        return true;
    };

    addText(main, 0x010F, QStringLiteral("Camera make"));
    addText(main, 0x0110, QStringLiteral("Camera model"));
    addText(exif, 0xA433, QStringLiteral("Lens make"));
    addText(exif, 0xA434, QStringLiteral("Lens"));
    addText(exif, 0x9003, QStringLiteral("Date taken"));
    addText(exif, 0x9011, QStringLiteral("Time zone"));
    addText(main, 0x0132, QStringLiteral("Date modified"));

    double value = 0.0;
    if (numberOf(exif, 0x829A, &value)) {
        const QString text = exposureTime(value);
        if (!text.isEmpty()) {
            facts.append({QStringLiteral("Exposure time"), text});
        }
    }
    if (numberOf(exif, 0x829D, &value) && value > 0.0) {
        facts.append({QStringLiteral("Aperture"), QStringLiteral("f/%1").arg(number(value))});
    }
    if (numberOf(exif, 0x8827, &value) && value > 0.0) {
        facts.append({QStringLiteral("ISO"), QString::number(qRound(value))});
    }
    if (numberOf(exif, 0x9204, &value)) {
        facts.append({QStringLiteral("Exposure compensation"), QStringLiteral("%1%2 EV").arg(value > 0.0 ? QStringLiteral("+") : QString(), number(value, 2))});
    }
    if (numberOf(exif, 0x920A, &value) && value > 0.0) {
        QString text = QStringLiteral("%1 mm").arg(number(value));
        double equivalent = 0.0;
        if (numberOf(exif, 0xA405, &equivalent) && equivalent > 0.0) {
            text += QStringLiteral(" (%1 mm in 35 mm terms)").arg(qRound(equivalent));
        }
        facts.append({QStringLiteral("Focal length"), text});
    }
    if (numberOf(exif, 0x8822, &value)) {
        const QString text = exposureProgramText(qRound(value));
        if (!text.isEmpty()) {
            facts.append({QStringLiteral("Exposure program"), text});
        }
    }
    if (numberOf(exif, 0x9207, &value)) {
        const QString text = meteringText(qRound(value));
        if (!text.isEmpty()) {
            facts.append({QStringLiteral("Metering"), text});
        }
    }
    if (numberOf(exif, 0x9209, &value)) {
        facts.append({QStringLiteral("Flash"), (qRound(value) & 1) ? QStringLiteral("Fired") : QStringLiteral("Did not fire")});
    }
    if (numberOf(exif, 0xA403, &value)) {
        facts.append({QStringLiteral("White balance"), qRound(value) == 1 ? QStringLiteral("Manual") : QStringLiteral("Auto")});
    }
    if (numberOf(main, 0x0112, &value)) {
        facts.append({QStringLiteral("Orientation"), orientationText(qRound(value))});
    }
    double pixelWidth = 0.0;
    double pixelHeight = 0.0;
    if (numberOf(exif, 0xA002, &pixelWidth) && numberOf(exif, 0xA003, &pixelHeight) && pixelWidth > 0.0 && pixelHeight > 0.0) {
        facts.append({QStringLiteral("Pixel size"), QStringLiteral("%1 × %2").arg(qRound(pixelWidth)).arg(qRound(pixelHeight))});
    }
    addText(main, 0x0131, QStringLiteral("Software"));
    addText(main, 0x013B, QStringLiteral("Artist"));
    addText(main, 0x8298, QStringLiteral("Copyright"));
    addText(main, 0x010E, QStringLiteral("Description"));

    const QString latitude = gpsCoordinate(opened, findField(gps, 2), findField(gps, 1));
    const QString longitude = gpsCoordinate(opened, findField(gps, 4), findField(gps, 3));
    if (!latitude.isEmpty() && !longitude.isEmpty()) {
        facts.append({QStringLiteral("GPS position"), QStringLiteral("%1, %2").arg(latitude, longitude)});
    }
    if (numberOf(gps, 6, &value)) {
        double reference = 0.0;
        const bool below = numberOf(gps, 5, &reference) && qRound(reference) == 1;
        facts.append({QStringLiteral("GPS altitude"), QStringLiteral("%1%2 m").arg(below ? QStringLiteral("-") : QString(), number(value))});
    }
    return facts;
}

} // namespace Exif
