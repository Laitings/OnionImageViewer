#include "VectorReader.h"

#include "ColorTransform.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>

#ifdef ONION_HAVE_QTPDF
#include <QPdfDocument>
#endif

#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 HeaderBytes = 256 * 1024; // DSC comments and EPSI previews live near the start
constexpr double FullDpi = 300.0;
constexpr int MinFullLongSide = 2000;
constexpr int MaxFullLongSide = 6000;
constexpr int GhostscriptTimeoutMs = 60000;

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

enum class Kind { Unknown, Pdf, PostScript, DosEps };

struct FileInfo
{
    Kind kind = Kind::Unknown;
    QByteArray postScript;    // the start of the PostScript part (for DSC comments and EPSI)
    QByteArray tiffPreview;   // DOS EPS TIFF preview
    QRectF boundingBox;       // in points, from %%HiResBoundingBox / %%BoundingBox
};

QRectF parseBoundingBox(const QByteArray &ps)
{
    static const QRegularExpression hiRes(QStringLiteral("%%HiResBoundingBox:\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)"));
    static const QRegularExpression plain(QStringLiteral("%%BoundingBox:\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)"));
    const QString text = QString::fromLatin1(ps);
    for (const QRegularExpression *pattern : {&hiRes, &plain}) {
        const QRegularExpressionMatch m = pattern->match(text);
        if (m.hasMatch()) {
            const double x1 = m.captured(1).toDouble();
            const double y1 = m.captured(2).toDouble();
            const double x2 = m.captured(3).toDouble();
            const double y2 = m.captured(4).toDouble();
            const QRectF box(QPointF(x1, y1), QPointF(x2, y2));
            if (box.width() > 0 && box.height() > 0) {
                return box;
            }
        }
    }
    return QRectF();
}

FileInfo inspect(const QString &filePath, QString *error)
{
    FileInfo info;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return info;
    }
    const QByteArray head = file.read(HeaderBytes);
    if (head.startsWith("%PDF")) {
        info.kind = Kind::Pdf;
        return info;
    }
    if (head.size() >= 30 && quint8(head[0]) == 0xC5 && quint8(head[1]) == 0xD0 && quint8(head[2]) == 0xD3
        && quint8(head[3]) == 0xC6) {
        // DOS EPS binary header: PostScript, WMF and TIFF sections.
        const auto *p = reinterpret_cast<const uchar *>(head.constData());
        const qint64 psStart = qFromLittleEndian<quint32>(p + 4);
        const qint64 psLength = qFromLittleEndian<quint32>(p + 8);
        const qint64 tiffStart = qFromLittleEndian<quint32>(p + 20);
        const qint64 tiffLength = qFromLittleEndian<quint32>(p + 24);
        info.kind = Kind::DosEps;
        if (file.seek(psStart)) {
            info.postScript = file.read(qMin(psLength, HeaderBytes));
        }
        if (tiffStart > 0 && tiffLength > 0 && tiffStart + tiffLength <= file.size() && file.seek(tiffStart)) {
            info.tiffPreview = file.read(tiffLength);
        }
    } else if (head.startsWith("%!")) {
        info.kind = Kind::PostScript;
        info.postScript = head;
    } else {
        fail(error, QStringLiteral("Not a PDF, EPS or PostScript-based Illustrator file"));
        return info;
    }
    info.boundingBox = parseBoundingBox(info.postScript);
    return info;
}

QString dscValue(const QByteArray &ps, const char *key)
{
    const QByteArray marker = QByteArray("%%") + key + ':';
    const qsizetype at = ps.indexOf(marker);
    if (at < 0) {
        return QString();
    }
    qsizetype end = at + marker.size();
    while (end < ps.size() && ps[end] != '\n' && ps[end] != '\r') {
        ++end;
    }
    QString value = QString::fromLatin1(ps.mid(at + marker.size(), end - at - marker.size())).trimmed();
    if (value.startsWith(QLatin1Char('(')) && value.endsWith(QLatin1Char(')'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

// The pixel size for a page of the given size in points: fit into targetSize, or ~300 dpi.
QSize renderSize(const QSizeF &points, const QSize &targetSize)
{
    if (points.width() <= 0 || points.height() <= 0) {
        return QSize();
    }
    double scale;
    if (targetSize.isValid() && !targetSize.isEmpty()) {
        scale = std::min(targetSize.width() / points.width(), targetSize.height() / points.height());
    } else {
        const double longSide = std::max(points.width(), points.height());
        scale = FullDpi / 72.0;
        scale = std::clamp(scale, MinFullLongSide / longSide, MaxFullLongSide / longSide);
    }
    return QSize(qMax(1, int(std::lround(points.width() * scale))), qMax(1, int(std::lround(points.height() * scale))));
}

#ifdef ONION_HAVE_QTPDF
QImage renderWithQtPdf(const QString &filePath, const QSize &targetSize, QString *error)
{
    QPdfDocument document;
    const QPdfDocument::Error status = document.load(filePath);
    if (status != QPdfDocument::Error::None || document.pageCount() < 1) {
        fail(error, status == QPdfDocument::Error::IncorrectPassword ? QStringLiteral("The PDF is password protected")
                                                                      : QStringLiteral("Qt PDF could not open the file"));
        return QImage();
    }
    const QSize size = renderSize(document.pagePointSize(0), targetSize);
    if (!size.isValid()) {
        fail(error, QStringLiteral("The PDF page has no size"));
        return QImage();
    }
    const QImage page = document.render(0, size);
    if (page.isNull()) {
        fail(error, QStringLiteral("Qt PDF could not render the page"));
        return page;
    }
    return page; // transparent where the artwork is (shown over the checkerboard)
}
#endif

QImage renderWithGhostscript(const QString &filePath, const FileInfo &info, const QSize &targetSize, QString *error)
{
    const QString gs = Vector::ghostscriptPath();
    if (gs.isEmpty()) {
        fail(error, QStringLiteral("Ghostscript is not installed"));
        return QImage();
    }
    double dpi = 72.0;
    if (info.boundingBox.isValid()) {
        const QSize size = renderSize(info.boundingBox.size(), targetSize);
        dpi = 72.0 * size.width() / info.boundingBox.width();
    } else if (!targetSize.isValid()) {
        dpi = FullDpi;
    }
    QStringList args = {QStringLiteral("-q"), QStringLiteral("-dSAFER"), QStringLiteral("-dBATCH"),
                        QStringLiteral("-dNOPAUSE"), QStringLiteral("-sDEVICE=pngalpha"),
                        QStringLiteral("-dTextAlphaBits=4"), QStringLiteral("-dGraphicsAlphaBits=4"),
                        QStringLiteral("-r%1").arg(dpi, 0, 'f', 3)};
    if (info.kind == Kind::Pdf) {
        args << QStringLiteral("-dFirstPage=1") << QStringLiteral("-dLastPage=1");
    } else {
        args << QStringLiteral("-dEPSCrop");
    }
    args << QStringLiteral("-sOutputFile=-") << QStringLiteral("-f") << QDir::toNativeSeparators(filePath);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(gs, args);
    if (!process.waitForStarted(10000)) {
        fail(error, QStringLiteral("Could not start Ghostscript"));
        return QImage();
    }
    process.closeWriteChannel();
    QByteArray png;
    while (process.state() != QProcess::NotRunning) {
        if (!process.waitForReadyRead(GhostscriptTimeoutMs) && process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished();
            fail(error, QStringLiteral("Ghostscript took too long"));
            return QImage();
        }
        png += process.readAllStandardOutput();
    }
    png += process.readAllStandardOutput();
    QImage image;
    if (!image.loadFromData(png, "PNG")) {
        const QString message = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        fail(error, message.isEmpty() ? QStringLiteral("Ghostscript could not render the file")
                                      : QStringLiteral("Ghostscript: %1").arg(message.left(300)));
        return QImage();
    }
    return image;
}

// EPSI preview: "%%BeginPreview: w h depth lines" followed by hex lines starting with "%".
QImage epsiPreview(const QByteArray &ps)
{
    const qsizetype begin = ps.indexOf("%%BeginPreview:");
    if (begin < 0) {
        return QImage();
    }
    qsizetype lineEnd = ps.indexOf('\n', begin);
    if (lineEnd < 0) {
        return QImage();
    }
    const QList<QByteArray> fields = ps.mid(begin + 15, lineEnd - begin - 15).simplified().split(' ');
    if (fields.size() < 3) {
        return QImage();
    }
    const int width = fields[0].toInt();
    const int height = fields[1].toInt();
    const int depth = fields[2].toInt();
    if (width <= 0 || height <= 0 || width > 10000 || height > 10000 || (depth != 1 && depth != 2 && depth != 4 && depth != 8)) {
        return QImage();
    }
    const qsizetype end = ps.indexOf("%%EndPreview", lineEnd);
    QByteArray hex;
    for (qsizetype i = lineEnd + 1; i < (end < 0 ? ps.size() : end); ++i) {
        const char c = ps[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            hex += c;
        }
    }
    const QByteArray data = QByteArray::fromHex(hex);
    const int rowBytes = (width * depth + 7) / 8;
    if (data.size() < qsizetype(rowBytes) * height) {
        return QImage();
    }
    QImage image(width, height, QImage::Format_Grayscale8);
    const int maxValue = (1 << depth) - 1;
    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const uchar *>(data.constData()) + qsizetype(y) * rowBytes;
        uchar *out = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int bit = x * depth;
            const int value = (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxValue;
            // EPSI stores ink: 0 is white, the maximum is black.
            out[x] = uchar(255 - value * 255 / maxValue);
        }
    }
    return image;
}

QImage embeddedPreview(const FileInfo &info)
{
    if (!info.tiffPreview.isEmpty()) {
        QByteArray bytes = info.tiffPreview;
        QBuffer buffer(&bytes);
        QImageReader reader(&buffer, "tiff");
        const QImage image = reader.read();
        if (!image.isNull()) {
            return image;
        }
    }
    return epsiPreview(info.postScript);
}

QString formatName(const QString &filePath, const FileInfo &info)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("ai")) {
        return info.kind == Kind::Pdf ? QStringLiteral("Illustrator (PDF compatible)")
                                      : QStringLiteral("Illustrator (PostScript, legacy)");
    }
    if (info.kind == Kind::Pdf) {
        return QStringLiteral("PDF");
    }
    return info.kind == Kind::DosEps ? QStringLiteral("EPS (with binary preview header)") : QStringLiteral("EPS");
}

} // namespace

namespace Vector {

bool isVectorFile(const QString &filePath)
{
    static const QStringList suffixes = {QStringLiteral("ai"), QStringLiteral("eps"), QStringLiteral("epsf"),
                                         QStringLiteral("epsi"), QStringLiteral("pdf")};
    return suffixes.contains(QFileInfo(filePath).suffix().toLower());
}

bool hasQtPdf()
{
#ifdef ONION_HAVE_QTPDF
    return true;
#else
    return false;
#endif
}

QString ghostscriptPath()
{
    static const QString path = [] {
#ifdef Q_OS_WIN
        for (const QString &name : {QStringLiteral("gswin64c"), QStringLiteral("gswin32c")}) {
            const QString found = QStandardPaths::findExecutable(name);
            if (!found.isEmpty()) {
                return found;
            }
        }
        // Default install folders: C:\Program Files\gs\gs10.05.1\bin\gswin64c.exe; newest wins.
        for (const QString &root : {QStringLiteral("C:/Program Files/gs"), QStringLiteral("C:/Program Files (x86)/gs")}) {
            QStringList versions = QDir(root).entryList({QStringLiteral("gs*")}, QDir::Dirs, QDir::Name);
            std::reverse(versions.begin(), versions.end());
            for (const QString &version : versions) {
                for (const QString &exe : {QStringLiteral("gswin64c.exe"), QStringLiteral("gswin32c.exe")}) {
                    const QString candidate = root + QLatin1Char('/') + version + QStringLiteral("/bin/") + exe;
                    if (QFileInfo::exists(candidate)) {
                        return candidate;
                    }
                }
            }
        }
        return QString();
#else
        return QStandardPaths::findExecutable(QStringLiteral("gs"));
#endif
    }();
    return path;
}

QImage read(const QString &filePath, QString *error, const QSize &targetSize)
{
    const FileInfo info = inspect(filePath, error);
    if (info.kind == Kind::Unknown) {
        return QImage();
    }

    QImage image;
    QString renderError;
    if (info.kind == Kind::Pdf) {
#ifdef ONION_HAVE_QTPDF
        image = renderWithQtPdf(filePath, targetSize, &renderError);
#endif
        if (image.isNull() && !ghostscriptPath().isEmpty()) {
            image = renderWithGhostscript(filePath, info, targetSize, &renderError);
        }
        if (image.isNull() && renderError.isEmpty()) {
            renderError = QStringLiteral("Showing PDF and Illustrator files needs the Qt PDF module (or Ghostscript)");
        }
    } else {
        if (!ghostscriptPath().isEmpty()) {
            image = renderWithGhostscript(filePath, info, targetSize, &renderError);
        }
        if (image.isNull()) {
            image = embeddedPreview(info);
            if (!image.isNull() && !targetSize.isValid()) {
                image.setText(QStringLiteral("Description"), QStringLiteral("Embedded preview"));
            }
        }
        if (image.isNull() && renderError.isEmpty()) {
            renderError = QStringLiteral("This EPS has no preview; install Ghostscript to render it");
        }
    }
    if (image.isNull()) {
        fail(error, renderError);
        return image;
    }
    image.setText(ColorTransform::sourceFormatTextKey(), formatName(filePath, info));
    return image;
}

QList<QPair<QString, QString>> describe(const QString &filePath)
{
    QList<QPair<QString, QString>> entries;
    QString error;
    const FileInfo info = inspect(filePath, &error);
    if (info.kind == Kind::Unknown) {
        entries.append({QStringLiteral("Error"), error});
        return entries;
    }
    entries.append({QStringLiteral("Format"), formatName(filePath, info)});

    auto pageSizeText = [](const QSizeF &points) {
        return QStringLiteral("%1 × %2 pt (%3 × %4 mm)")
            .arg(points.width(), 0, 'f', 1).arg(points.height(), 0, 'f', 1)
            .arg(points.width() * 25.4 / 72.0, 0, 'f', 1).arg(points.height() * 25.4 / 72.0, 0, 'f', 1);
    };

    QString renderer;
    if (info.kind == Kind::Pdf) {
#ifdef ONION_HAVE_QTPDF
        QPdfDocument document;
        if (document.load(filePath) == QPdfDocument::Error::None && document.pageCount() > 0) {
            entries.append({QStringLiteral("Page size"), pageSizeText(document.pagePointSize(0))});
            entries.append({QStringLiteral("Pages"), QString::number(document.pageCount())});
            const QList<QPair<QString, QPdfDocument::MetaDataField>> fields = {
                {QStringLiteral("Title"), QPdfDocument::MetaDataField::Title},
                {QStringLiteral("Author"), QPdfDocument::MetaDataField::Author},
                {QStringLiteral("Creator"), QPdfDocument::MetaDataField::Creator},
                {QStringLiteral("Producer"), QPdfDocument::MetaDataField::Producer},
                {QStringLiteral("Created"), QPdfDocument::MetaDataField::CreationDate},
                {QStringLiteral("Modified"), QPdfDocument::MetaDataField::ModificationDate},
            };
            for (const auto &field : fields) {
                const QVariant value = document.metaData(field.second);
                const QString text = value.typeId() == QMetaType::QDateTime
                    ? value.toDateTime().toString(Qt::ISODate) : value.toString().trimmed();
                if (!text.isEmpty()) {
                    entries.append({field.first, text});
                }
            }
        }
        renderer = QStringLiteral("Qt PDF");
#else
        renderer = ghostscriptPath().isEmpty() ? QStringLiteral("None (needs Qt PDF or Ghostscript)")
                                               : QStringLiteral("Ghostscript");
#endif
    } else {
        if (info.boundingBox.isValid()) {
            entries.append({QStringLiteral("Bounding box"), pageSizeText(info.boundingBox.size())});
        }
        const std::pair<const char *, const char *> keys[] = {
            {"Title", "Title"}, {"Creator", "Creator"}, {"For", "For"}, {"Created", "CreationDate"},
            {"Language level", "LanguageLevel"}};
        for (const auto &key : keys) {
            const QString value = dscValue(info.postScript, key.second);
            if (!value.isEmpty()) {
                entries.append({QString::fromLatin1(key.first), value});
            }
        }
        const QImage preview = embeddedPreview(info);
        entries.append({QStringLiteral("Embedded preview"),
                        preview.isNull() ? QStringLiteral("None")
                                         : QStringLiteral("%1 × %2 %3").arg(preview.width()).arg(preview.height())
                                               .arg(info.tiffPreview.isEmpty() ? QStringLiteral("(EPSI)") : QStringLiteral("(TIFF)"))});
        renderer = ghostscriptPath().isEmpty() ? QStringLiteral("Embedded preview (install Ghostscript for full quality)")
                                               : QStringLiteral("Ghostscript");
    }
    if (renderer == QStringLiteral("Ghostscript")) {
        renderer += QStringLiteral(" (%1)").arg(QDir::toNativeSeparators(ghostscriptPath()));
    }
    entries.append({QStringLiteral("Rendered with"), renderer});
    return entries;
}

} // namespace Vector
