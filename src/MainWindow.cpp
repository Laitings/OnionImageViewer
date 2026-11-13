#include "MainWindow.h"
#include "BatchRenameDialog.h"

#include "ColorTransform.h"
#include "DngReader.h"
#include "DpxReader.h"
#include "ExrReader.h"
#include "HeifReader.h"
#include "PsdReader.h"
#include "VectorReader.h"
#include "WebpReader.h"
#include "FileItemDelegate.h"
#include "FileOps.h"
#include "FolderTreePanel.h"
#include "ImageLoader.h"
#include "Icons.h"
#include "ImageViewport.h"
#include "Metadata.h"
#include "MetadataPanel.h"
#include "ScopesPanel.h"
#include "OverlayButton.h"
#include "NameFilter.h"
#include "PathBar.h"
#include "PlayerBar.h"
#include "Sequence.h"
#include "SplitPane.h"
#include "UpdateChecker.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProcess>
#include <QUrl>
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QImageReader>
#include <QImageWriter>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QKeyEvent>
#include <QFloat16>
#include <QLineEdit>
#include <QRegularExpression>
#include <QShortcut>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSize>
#include <QStackedWidget>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QStatusBar>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolBar>
#include <QHeaderView>
#include <QLocale>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <windows.h>

#endif

namespace {
enum class BatchScaling { Original, CenterCrop, Fill, Fit, Stretch };

QImage scaleBatchImage(const QImage &image, const QSize &size, BatchScaling scaling)
{
    if (image.isNull() || scaling == BatchScaling::Original || image.size() == size) {
        return image;
    }
    if (scaling == BatchScaling::Stretch) {
        return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QRectF source(image.rect());
    QRectF target{QPointF(), QSizeF(size)};
    if (scaling == BatchScaling::Fit) {
        const QSize fitted = image.size().scaled(size, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        target.setSize(fitted);
        target.moveTopLeft(QPoint((size.width() - fitted.width()) / 2, (size.height() - fitted.height()) / 2));
    } else if (scaling == BatchScaling::CenterCrop) {
        const QSize visible = image.size().boundedTo(size);
        source = QRect((image.width() - visible.width()) / 2, (image.height() - visible.height()) / 2,
                       visible.width(), visible.height());
        target = QRect((size.width() - visible.width()) / 2, (size.height() - visible.height()) / 2,
                       visible.width(), visible.height());
    } else {
        // Sample the center of the source directly, avoiding an oversized intermediate image.
        const qreal scale = qMax(qreal(size.width()) / image.width(), qreal(size.height()) / image.height());
        source.setSize(QSizeF(size) / scale);
        source.moveCenter(QRectF(image.rect()).center());
    }

    QImage output(size, image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (!output.isNull()) {
        output.fill(Qt::black);
        output.setColorSpace(image.colorSpace());
        QPainter painter(&output);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, image, source);
    }
    return output;
}

struct ExportFormat
{
    QString name;
    QString suffix;
    QByteArray writer;

    bool hasQuality() const { return writer == "jpeg" || writer == "webp"; }
};

QList<ExportFormat> exportFormats()
{
    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    const ExportFormat candidates[] = {
        {QStringLiteral("PNG"), QStringLiteral("png"), "png"},
        {QStringLiteral("JPEG"), QStringLiteral("jpg"), "jpeg"},
        {QStringLiteral("TIFF"), QStringLiteral("tif"), "tiff"},
        {QStringLiteral("WebP"), QStringLiteral("webp"), "webp"},
        {QStringLiteral("BMP"), QStringLiteral("bmp"), "bmp"},
    };
    QList<ExportFormat> formats;
    for (const ExportFormat &format : candidates) {
        if (writable.contains(format.writer)) {
            formats.append(format);
        }
    }
    return formats;
}

bool writeExportImage(QImageWriter &writer, const ExportFormat &format, const QImage &image, int quality)
{
    if (format.hasQuality()) {
        writer.setQuality(quality);
    }
    if (format.writer == "tiff") {
        writer.setCompression(1); // LZW
    }
    return writer.write(format.writer == "jpeg" || format.writer == "bmp"
                            ? image.convertToFormat(QImage::Format_RGB888) : image);
}

const QString SettingsMainWindowGeometry = QStringLiteral("mainWindow/geometry");
const QString SettingsMainWindowState = QStringLiteral("mainWindow/state");
const QString SettingsFoldersVisible = QStringLiteral("folders/visible");
const QString SettingsPinnedFolders = QStringLiteral("folders/pinned");
const QString SettingsQuickAccessExpanded = QStringLiteral("folders/quickAccessExpanded");
const QString SettingsThisPcExpanded = QStringLiteral("folders/thisPcExpanded");
const QString SettingsViewTransform = QStringLiteral("view/colorTransform");
const QString SettingsInput = QStringLiteral("view/input");
const QString SettingsLutFile = QStringLiteral("view/lutFile");
const QString SettingsArriLutFolder = QStringLiteral("view/arriLutFolder");
const QString SettingsRecentLuts = QStringLiteral("view/recentLuts");
const QString SettingsRecentImages = QStringLiteral("file/recentImages");
const QString SettingsExportFolder = QStringLiteral("export/folder");
const QString SettingsThumbnailSize = QStringLiteral("grid/thumbnailSize");
const QString SettingsGridView = QStringLiteral("grid/view");
const QString SettingsImagesOnly = QStringLiteral("grid/imagesOnly");
const QString SettingsDetailsColumns = QStringLiteral("grid/detailsColumns");
const QString SettingsExportFormat = QStringLiteral("export/format");
const QString SettingsRenamePattern = QStringLiteral("rename/pattern");
const QString SettingsRenameFind = QStringLiteral("rename/find");
const QString SettingsRenameReplace = QStringLiteral("rename/replace");
const QString SettingsRenameRegex = QStringLiteral("rename/regex");
const QString SettingsRenameMatchCase = QStringLiteral("rename/matchCase");
const QString SettingsRenameNameCase = QStringLiteral("rename/nameCase");
const QString SettingsRenameExtensionCase = QStringLiteral("rename/extensionCase");
const QString SettingsRenameCounterStart = QStringLiteral("rename/counterStart");
const QString SettingsRenameCounterStep = QStringLiteral("rename/counterStep");
const QString SettingsRenameCounterDigits = QStringLiteral("rename/counterDigits");
const QString SettingsRenameRemove = QStringLiteral("rename/remove");
const QString SettingsRenameRemoveFrom = QStringLiteral("rename/removeFrom");
const QString SettingsRenameRemoveTo = QStringLiteral("rename/removeTo");
const QString SettingsRenameRemoveFromEnd = QStringLiteral("rename/removeFromEnd");
constexpr int MaxRecentImages = 10;
const int MaxRecentLuts = 8;
const QString SettingsSplitterState = QStringLiteral("splitter/state");
const QString SettingsSplitOrientation = QStringLiteral("viewer/splitOrientation");
const QString SettingsSplitSync = QStringLiteral("viewer/splitSync");
const QString SettingsPlaybackFps = QStringLiteral("playback/fps");
const QString SettingsPlaybackLoop = QStringLiteral("playback/loop");
const QString SettingsPlaybackRealTime = QStringLiteral("playback/realTime");
const QString SettingsPlayerBarVisible = QStringLiteral("playback/showControls");
const int FilePathRole = Qt::UserRole;
const int IsFolderRole = Qt::UserRole + 1;
const int IsImageRole = Qt::UserRole + 2;
const int ThumbnailLoadedRole = Qt::UserRole + 3;
const int FileSizeRole = Qt::UserRole + 4;
// Thumbnails are made at one of two resolutions: 128 px (the classic size; its cache stays valid)
// or 256 px when the grid shows them bigger than 128. The grid scales them to the chosen size.
constexpr int SmallThumbnailResolution = 128;
constexpr int LargeThumbnailResolution = 256;
constexpr int MinThumbnailDisplaySize = 64;
constexpr int MaxThumbnailDisplaySize = 256;
constexpr int ThumbnailDisplayStep = 16;
constexpr int ListIconSize = 20;

// "EXR image", "File folder", "PDF file": the Type column in Details and Tiles.
QString fileTypeText(const QFileInfo &info, bool image)
{
    if (info.isDir()) {
        return QStringLiteral("File folder");
    }
    const QString suffix = info.suffix().toUpper();
    if (suffix.isEmpty()) {
        return QStringLiteral("File");
    }
    return image ? QStringLiteral("%1 image").arg(suffix) : QStringLiteral("%1 file").arg(suffix);
}

// Sizes as Explorer shows them: whole kilobytes, rounded up.
QString fileSizeText(const QLocale &locale, qint64 bytes)
{
    if (bytes < 1024) {
        return bytes == 1 ? QStringLiteral("1 byte") : QStringLiteral("%1 bytes").arg(bytes);
    }
    return QStringLiteral("%1 KB").arg(locale.toString((bytes + 1023) / 1024));
}

const char *gridViewKey(int view)
{
    switch (view) {
    case 1: return "list";
    case 2: return "details";
    case 3: return "tiles";
    default: return "icons";
    }
}
constexpr int ThumbnailScheduleDelayMs = 60;
constexpr int MaxPendingThumbnails = 32;
constexpr int ThumbnailPriorityVisible = 1;
constexpr int ThumbnailPriorityBackground = 0;
constexpr int ThumbnailRetryDelayMs = 2000;
constexpr int MaxThumbnailRetries = 30; // about a minute of retries
constexpr double DefaultPlaybackFps = 24.0;
constexpr int PlaybackPollMs = 4; // how often playback checks whether the next frame is due
constexpr int PrefetchAhead = 6;

// The frame number in a sequence file name ("DF_0078_0020_v001.1001.exr" -> "1001").
QString frameNumberOf(const QString &filePath)
{
    static const QRegularExpression pattern(QStringLiteral("(\\d+)\\.[^.]+$"));
    const QRegularExpressionMatch match = pattern.match(QFileInfo(filePath).fileName());
    return match.hasMatch() ? match.captured(1) : QString();
}

// A stored value as text: floats with 4 decimals, integers as code values at the file's bit depth.
QString storedPixelText(const QImage &source, const QPoint &pixel)
{
    if (source.isNull() || !source.rect().contains(pixel)) {
        return QString();
    }
    switch (source.format()) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied: {
        const auto *p = reinterpret_cast<const qfloat16 *>(source.constScanLine(pixel.y())) + pixel.x() * 4;
        return QStringLiteral("%1  %2  %3").arg(float(p[0]), 0, 'f', 4).arg(float(p[1]), 0, 'f', 4).arg(float(p[2]), 0, 'f', 4);
    }
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied: {
        const auto *p = reinterpret_cast<const float *>(source.constScanLine(pixel.y())) + pixel.x() * 4;
        return QStringLiteral("%1  %2  %3").arg(p[0], 0, 'f', 4).arg(p[1], 0, 'f', 4).arg(p[2], 0, 'f', 4);
    }
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
        const auto *p = reinterpret_cast<const quint16 *>(source.constScanLine(pixel.y())) + pixel.x() * 4;
        // Show DPX code values at their own bit depth (e.g. 0..1023 for 10-bit).
        static const QRegularExpression bits(QStringLiteral("(\\d+)-bit"));
        const QRegularExpressionMatch match = bits.match(source.text(ColorTransform::sourceFormatTextKey()));
        const int depth = match.hasMatch() ? qBound(1, match.captured(1).toInt(), 16) : 16;
        const int shift = 16 - depth;
        return QStringLiteral("%1  %2  %3  (%4-bit)").arg(p[0] >> shift).arg(p[1] >> shift).arg(p[2] >> shift).arg(depth);
    }
    default: {
        const QRgb rgb = source.pixel(pixel);
        return QStringLiteral("%1  %2  %3").arg(qRed(rgb)).arg(qGreen(rgb)).arg(qBlue(rgb));
    }
    }
}

QStringList supportedImageSuffixes()
{
    QStringList suffixes;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    suffixes.reserve(formats.size());
    for (const QByteArray &format : formats) {
        suffixes.append(QString::fromLatin1(format).toLower());
    }
    suffixes.append(QStringLiteral("dpx"));
    suffixes.append(QStringLiteral("psd"));
    suffixes.append(QStringLiteral("psb"));
    for (const char *vector : {"ai", "eps", "epsf", "epsi", "pdf"}) {
        suffixes.append(QString::fromLatin1(vector));
    }
    if (Exr::isAvailable()) {
        suffixes.append(QStringLiteral("exr"));
    }
    if (Dng::isAvailable()) {
        suffixes.append(QStringLiteral("dng"));
    }
    if (Webp::isAvailable()) {
        suffixes.append(QStringLiteral("webp"));
    }
    if (Heif::isAvailable()) {
        for (const char *heif : {"heic", "heif", "hif"}) {
            suffixes.append(QString::fromLatin1(heif));
        }
    }
    suffixes.removeDuplicates();
    return suffixes;
}

QString thumbnailCacheDirectory()
{
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/thumbnails");
    QDir().mkpath(cacheDirectory);
    return cacheDirectory;
}

QString thumbnailCachePath(const QFileInfo &fileInfo, ColorTransform::View view, ColorTransform::Input input, const QSize &size)
{
    QString cacheKeySource = fileInfo.absoluteFilePath()
        + QStringLiteral("|")
        + QString::number(fileInfo.size())
        + QStringLiteral("|")
        + QString::number(fileInfo.lastModified().toMSecsSinceEpoch());
    if (view != ColorTransform::View::None) {
        cacheKeySource += QStringLiteral("|") + ColorTransform::cacheKey(view);
    }
    // "r3": how EXRs are interpreted changed (camera log tags), so older thumbnails are stale.
    cacheKeySource += QStringLiteral("|in=") + ColorTransform::inputSettingsKey(input) + QStringLiteral("|r3");
    if (size.width() != SmallThumbnailResolution) {
        cacheKeySource += QStringLiteral("|s=%1").arg(size.width());
    }

    const QByteArray cacheKey = QCryptographicHash::hash(cacheKeySource.toUtf8(), QCryptographicHash::Sha256).toHex();
    return thumbnailCacheDirectory() + QStringLiteral("/") + QString::fromLatin1(cacheKey) + QStringLiteral(".png");
}

#ifdef Q_OS_WIN
QImage imageFromHBitmap(HBITMAP bitmap)
{
    BITMAP bitmapInfo = {};
    if (GetObject(bitmap, sizeof(bitmapInfo), &bitmapInfo) == 0 || bitmapInfo.bmWidth <= 0 || bitmapInfo.bmHeight <= 0) {
        return QImage();
    }

    BITMAPINFO dibInfo = {};
    dibInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dibInfo.bmiHeader.biWidth = bitmapInfo.bmWidth;
    dibInfo.bmiHeader.biHeight = -bitmapInfo.bmHeight;
    dibInfo.bmiHeader.biPlanes = 1;
    dibInfo.bmiHeader.biBitCount = 32;
    dibInfo.bmiHeader.biCompression = BI_RGB;

    QImage image(bitmapInfo.bmWidth, bitmapInfo.bmHeight, QImage::Format_ARGB32_Premultiplied);
    HDC deviceContext = GetDC(nullptr);
    const int scanLines = GetDIBits(
        deviceContext,
        bitmap,
        0,
        static_cast<UINT>(bitmapInfo.bmHeight),
        image.bits(),
        &dibInfo,
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, deviceContext);

    if (scanLines == 0) {
        return QImage();
    }

    bool hasAlpha = false;
    for (int y = 0; y < image.height() && !hasAlpha; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) != 0) {
                hasAlpha = true;
                break;
            }
        }
    }

    if (!hasAlpha) {
        image.convertTo(QImage::Format_RGB32);
    }

    return image;
}

// OneDrive (and other cloud sync) "online-only" files are placeholders: reading their contents
// makes Windows download the whole file. Their attributes can be read without downloading.
bool isCloudOnlyFile(const QString &filePath)
{
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(filePath).utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    constexpr DWORD RecallOnOpen = 0x00040000;       // FILE_ATTRIBUTE_RECALL_ON_OPEN
    constexpr DWORD RecallOnDataAccess = 0x00400000; // FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
    return (attributes & (FILE_ATTRIBUTE_OFFLINE | RecallOnOpen | RecallOnDataAccess)) != 0;
}

// pending is set when Windows has queued a background fetch for the thumbnail (typical for
// OneDrive online-only files the first time). Asking again a little later returns it.
QImage shellThumbnailForImage(const QString &filePath, SIIGBF extraFlags, const QSize &thumbSize, bool *pending = nullptr)
{
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        return QImage();
    }

    // The shell only accepts native paths; Qt paths use forward slashes.
    Microsoft::WRL::ComPtr<IShellItemImageFactory> imageFactory;
    const HRESULT itemResult = SHCreateItemFromParsingName(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(filePath).utf16()),
        nullptr,
        IID_PPV_ARGS(&imageFactory));

    QImage thumbnail;
    if (SUCCEEDED(itemResult) && imageFactory) {
        HBITMAP bitmap = nullptr;
        SIZE size = {thumbSize.width(), thumbSize.height()};
        const HRESULT thumbnailResult = imageFactory->GetImage(
            size,
            SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY | extraFlags,
            &bitmap);

        if (SUCCEEDED(thumbnailResult) && bitmap) {
            thumbnail = imageFromHBitmap(bitmap);
            DeleteObject(bitmap);
        } else if (thumbnailResult == WTS_E_EXTRACTIONPENDING && pending) {
            *pending = true;
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return thumbnail;
}

#endif

// Write via a temporary file so an interrupted save never leaves a half-written PNG behind.
void saveThumbnailToCache(const QImage &thumbnail, const QString &cachePath)
{
    QSaveFile file(cachePath);
    if (file.open(QIODevice::WriteOnly) && thumbnail.save(&file, "PNG")) {
        file.commit();
    }
}

// Turns a full-precision image (DPX/EXR) into a display thumbnail: shrink it cheaply first so the
// colour transform only touches a small image, then apply the view and scale smoothly.
QImage displayThumbnail(const QImage &source, ColorTransform::View view, ColorTransform::Input input, const QSize &size)
{
    if (source.isNull()) {
        return source;
    }
    // Resolve Auto from the full image: scaling doesn't keep the file's tags.
    input = ColorTransform::resolveInput(input, source);
    view = ColorTransform::resolveView(view, source);
    QImage reduced = source;
    const QSize roughSize = size * 2;
    if (reduced.width() > roughSize.width() || reduced.height() > roughSize.height()) {
        reduced = reduced.scaled(roughSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return ColorTransform::toDisplay(reduced, view, input)
        .scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage readerThumbnailForImage(const QString &filePath, ColorTransform::View view, ColorTransform::Input input, const QSize &size)
{
    if (Dng::isDngFile(filePath)) {
        return displayThumbnail(Dng::readThumbnail(filePath, size), view, input, size);
    }

    if (Exr::isExrFile(filePath)) {
        return displayThumbnail(Exr::read(filePath, nullptr, true), view, input, size);
    }

    if (Dpx::isDpxFile(filePath)) {
        return displayThumbnail(Dpx::read(filePath, nullptr, size), view, input, size);
    }

    if (Psd::isPsdFile(filePath)) {
        return displayThumbnail(Psd::read(filePath, nullptr, size), view, input, size);
    }

    if (Heif::isHeifFile(filePath)) {
        return displayThumbnail(Heif::read(filePath, nullptr, size), view, input, size);
    }

    if (Webp::isWebpFile(filePath)) {
        return displayThumbnail(Webp::read(filePath, nullptr, size), view, input, size);
    }

    if (Vector::isVectorFile(filePath)) {
        return displayThumbnail(Vector::read(filePath, nullptr, size), view, input, size);
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        reader.setScaledSize(imageSize.scaled(size, Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
    return view == ColorTransform::View::None ? image : ColorTransform::toDisplay(image, view);
}

QImage loadThumbnailImage(const QString &filePath, ColorTransform::View view, ColorTransform::Input input, const QSize &size, bool *pending)
{
    *pending = false;
    const QFileInfo fileInfo(filePath);
    const QString cachePath = thumbnailCachePath(fileInfo, view, input, size);

    QImage thumbnail;
    if (thumbnail.load(cachePath)) {
        return thumbnail;
    }

#ifdef Q_OS_WIN
    if (isCloudOnlyFile(filePath)) {
        // Never read the file itself here, or OneDrive downloads it. First try the Windows
        // thumbnail cache, then the sync provider's own thumbnail (OneDrive serves these from
        // the cloud, the same way Explorer shows them). If neither has one, keep the plain icon.
        thumbnail = shellThumbnailForImage(filePath, SIIGBF_INCACHEONLY, size);
        if (thumbnail.isNull()) {
            thumbnail = shellThumbnailForImage(filePath, SIIGBF(0), size, pending);
        }
        if (!thumbnail.isNull()) {
            thumbnail = ColorTransform::toDisplay(thumbnail, view)
                            .scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            saveThumbnailToCache(thumbnail, cachePath);
        }
        return thumbnail;
    }

    thumbnail = shellThumbnailForImage(filePath, SIIGBF(0), size);
    if (!thumbnail.isNull()) {
        thumbnail = ColorTransform::toDisplay(thumbnail, view)
                        .scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
#endif

    if (thumbnail.isNull()) {
        thumbnail = readerThumbnailForImage(filePath, view, input, size);
    }

    if (!thumbnail.isNull()) {
        saveThumbnailToCache(thumbnail, cachePath);
    }

    return thumbnail;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_imageLoader(std::make_shared<QtImageLoader>())
{
    setWindowTitle(QStringLiteral("Onion Image Viewer"));
    resize(1280, 800);

    applyDarkPalette();
    m_frameLoader = new FrameLoader(m_imageLoader, this);
    connect(m_frameLoader, &FrameLoader::frameReady, this, &MainWindow::showFrame);
    connect(m_frameLoader, &FrameLoader::gifFrameReady, this, &MainWindow::showGifFrame);
    m_playTimer.setTimerType(Qt::PreciseTimer);
    m_playTimer.setInterval(PlaybackPollMs);
    connect(&m_playTimer, &QTimer::timeout, this, &MainWindow::playbackTick);
    createActions();
    createMenus();
    createToolbar();
    createCentralLayout();
    loadSettings();
    qApp->installEventFilter(this);
    setAcceptDrops(true);

    // Keep thumbnail work off most cores and at low priority so the UI stays responsive.
    m_thumbnailPool.setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 4, 4));
    m_thumbnailPool.setThreadPriority(QThread::LowPriority);

    // Scrolling and resizing fire many events; only look for visible thumbnails once they settle.
    m_thumbnailScheduleTimer.setSingleShot(true);
    m_thumbnailScheduleTimer.setInterval(ThumbnailScheduleDelayMs);
    connect(&m_thumbnailScheduleTimer, &QTimer::timeout, this, &MainWindow::scheduleVisibleThumbnails);

    m_thumbnailRetryTimer.setSingleShot(true);
    m_thumbnailRetryTimer.setInterval(ThumbnailRetryDelayMs);
    connect(&m_thumbnailRetryTimer, &QTimer::timeout, this, &MainWindow::scheduleVisibleThumbnails);

    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::finished, this,
            [this](UpdateChecker::Status status, const QString &version, const QUrl &url) {
        if (m_updateDialog || status != UpdateChecker::Status::Available) {
            return;
        }
        auto *notice = new QMessageBox(QMessageBox::Information, QStringLiteral("OnionImageViewer Update"),
                                       QStringLiteral("Version %1 is available.").arg(version),
                                       QMessageBox::Close, this);
        notice->setModal(false);
        notice->setAttribute(Qt::WA_DeleteOnClose);
        auto *releasePage = notice->addButton(QStringLiteral("Open Release Page"), QMessageBox::ActionRole);
        connect(releasePage, &QPushButton::clicked, notice, [url]() {
            QDesktopServices::openUrl(url);
        });
        notice->show();
    }, Qt::SingleShotConnection);
    QTimer::singleShot(0, m_updateChecker, &UpdateChecker::check);
}

MainWindow::~MainWindow()
{
    m_batchCanceled = true;
    m_batchPool.waitForDone();
    m_thumbnailPool.clear();
    m_thumbnailPool.waitForDone();
    m_metadataPool.clear();
    m_metadataPool.waitForDone();
    qApp->removeEventFilter(this);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel && m_fileGrid && watched == m_fileGrid->viewport()) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = wheel->angleDelta().y();
            if (delta != 0) {
                stepGridView(delta > 0 ? 1 : -1);
            }
            return true;
        }
        // Plain wheel in the icon and tile views: scroll a fixed distance per notch. Qt's own
        // step there depends on the Qt version and can be a few pixels, which feels very slow
        // with big thumbnails.
        if (!wheel->modifiers() && wheel->angleDelta().y() != 0
            && (m_gridView == GridView::Icons || m_gridView == GridView::Tiles)) {
            const int rowHeight = m_fileGrid->gridSize().height() + (m_gridView == GridView::Icons ? m_fileGrid->spacing() : 0);
            // About one row per notch with the default 3 lines, never less than 150 px.
            const qreal notchPixels = qMax(rowHeight, 150) * QApplication::wheelScrollLines() / 3.0;
            m_wheelRemainder -= wheel->angleDelta().y() / 120.0 * notchPixels;
            const int pixels = int(m_wheelRemainder);
            m_wheelRemainder -= pixels;
            QScrollBar *bar = m_fileGrid->verticalScrollBar();
            if (m_fileGrid->verticalScrollMode() != QAbstractItemView::ScrollPerPixel) {
                m_fileGrid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
            }
            bar->setValue(bar->value() + pixels);
            return true;
        }
    }
    if (m_fileGrid && !m_shiftSelecting && handleGridSelectionEvent(watched, event)) {
        return true;
    }
    if (m_gridSearch && watched == m_gridSearch && event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_gridSearch->clear();
        m_fileGrid->setFocus(Qt::ShortcutFocusReason);
        return true;
    }
    if (event->type() == QEvent::Resize && (watched == this || watched == m_metadataDock || watched == m_contentStack)) {
        QTimer::singleShot(0, this, &MainWindow::alignSizeSlider);
    }
    if ((watched == m_imagePage || watched == m_imageViewport) && event->type() == QEvent::Resize) {
        layoutOverlayButtons();
        if (watched == m_imageViewport) {
            layoutSplitHeader();
        }
    }
    // A click in either pane of the split viewer makes it the active one.
    if (event->type() == QEvent::MouseButtonPress && m_splitPane && !m_splitPane->isHidden()) {
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            QWidget *paneHeader = m_splitPane->header();
            if (widget == m_splitPane || m_splitPane->isAncestorOf(widget)
                || widget == paneHeader || paneHeader->isAncestorOf(widget)) {
                setSplitPaneActive(true);
            } else if (widget == m_imageViewport) {
                setSplitPaneActive(false);
            }
        }
    }
    if (event->type() == QEvent::MouseMove && watched == m_imageViewport && !m_playTimer.isActive()) {
        revealOverlayButtons();
    }

    if (event->type() == QEvent::KeyPress && isActiveWindow() && isViewerVisible()
        && !qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
        if (handleViewerKey(static_cast<QKeyEvent *>(event))) {
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress && isActiveWindow()) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::XButton1 && m_backAction && m_backAction->isEnabled()) {
            navigateBack();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createActions()
{
    m_toggleFoldersAction = new QAction(QStringLiteral("Folders"), this);
    m_toggleFoldersAction->setCheckable(true);
    connect(m_toggleFoldersAction, &QAction::toggled, this, &MainWindow::setFolderPanelVisible);

    m_backAction = new QAction(Icons::icon(Icons::Kind::Back), QStringLiteral("Back"), this);
    m_backAction->setShortcut(QKeySequence::Back);
    m_backAction->setToolTip(QStringLiteral("Back (Alt+Left)"));
    m_backAction->setEnabled(false);
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);

    m_upAction = new QAction(Icons::icon(Icons::Kind::Up), QStringLiteral("Up One Folder"), this);
    m_upAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    m_upAction->setToolTip(QStringLiteral("Up one folder (Alt+Up)"));
    m_upAction->setEnabled(false);
    connect(m_upAction, &QAction::triggered, this, &MainWindow::navigateUp);

    // File grid views, like Explorer's (same shortcuts).
    m_gridViewGroup = new QActionGroup(this);
    m_gridViewGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    const struct {
        const char *text;
        GridView view;
        int size;
        Icons::Kind icon;
    } gridViews[] = {
        {"Extra Large Icons", GridView::Icons, 256, Icons::Kind::ViewIcons},
        {"Large Icons", GridView::Icons, 192, Icons::Kind::ViewIcons},
        {"Medium Icons", GridView::Icons, 128, Icons::Kind::ViewIcons},
        {"Small Icons", GridView::Icons, 64, Icons::Kind::ViewIcons},
        {"List", GridView::List, 0, Icons::Kind::ViewList},
        {"Details", GridView::Details, 0, Icons::Kind::ViewDetails},
        {"Tiles", GridView::Tiles, 0, Icons::Kind::ViewTiles},
    };
    int gridViewNumber = 1;
    for (const auto &entry : gridViews) {
        auto *action = new QAction(Icons::icon(entry.icon), QString::fromLatin1(entry.text), m_gridViewGroup);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | (Qt::Key_0 + gridViewNumber++)));
        action->setData(static_cast<int>(entry.view) * 1000 + entry.size);
        connect(action, &QAction::triggered, this, [this, view = entry.view, size = entry.size]() {
            setGridView(view, size);
        });
    }

    m_openImageAction = new QAction(QStringLiteral("Open Image..."), this);
    m_openImageAction->setShortcut(QKeySequence::Open);
    connect(m_openImageAction, &QAction::triggered, this, &MainWindow::openImage);

    m_fitToWindowAction = new QAction(QStringLiteral("Fit to Window"), this);
    connect(m_fitToWindowAction, &QAction::triggered, this, [this]() {
        activeViewport()->fitToWindow();
        syncSplitViews(activeViewport());
    });

    m_actualSizeAction = new QAction(QStringLiteral("100% Zoom"), this);
    connect(m_actualSizeAction, &QAction::triggered, this, [this]() {
        activeViewport()->zoomActualSize();
        syncSplitViews(activeViewport());
    });

    // View transform: how stored values are mapped to the screen (for log camera footage).
    m_viewTransformGroup = new QActionGroup(this);
    m_viewTransformGroup->setExclusive(true);
    const struct {
        ColorTransform::View view;
        QKeySequence shortcut;
    } transforms[] = {
        {ColorTransform::View::None, QKeySequence(Qt::CTRL | Qt::Key_0)},
        {ColorTransform::View::LogC3ToRec709, QKeySequence(Qt::CTRL | Qt::Key_3)},
        {ColorTransform::View::LogC4ToRec709, QKeySequence(Qt::CTRL | Qt::Key_4)},
        {ColorTransform::View::CustomLut, QKeySequence(Qt::CTRL | Qt::Key_L)},
        {ColorTransform::View::Auto, QKeySequence(Qt::CTRL | Qt::Key_1)},
    };
    for (const auto &entry : transforms) {
        auto *action = new QAction(ColorTransform::displayName(entry.view), m_viewTransformGroup);
        action->setCheckable(true);
        action->setShortcut(entry.shortcut);
        action->setData(static_cast<int>(entry.view));
        connect(action, &QAction::triggered, this, [this, view = entry.view]() {
            setViewTransform(view);
        });
        if (entry.view == ColorTransform::View::CustomLut) {
            m_customLutAction = action;
        }
    }

    // Input: what EXR values are (ACES comps need converting to the camera log first).
    m_inputGroup = new QActionGroup(this);
    m_inputGroup->setExclusive(true);
    for (ColorTransform::Input input : {ColorTransform::Input::Auto, ColorTransform::Input::CameraLog, ColorTransform::Input::Linear,
                                        ColorTransform::Input::Aces2065, ColorTransform::Input::AcesCg}) {
        auto *action = new QAction(ColorTransform::inputDisplayName(input), m_inputGroup);
        action->setCheckable(true);
        action->setData(static_cast<int>(input));
        connect(action, &QAction::triggered, this, [this, input]() {
            setInput(input);
        });
    }

    m_arriLutFolderAction = new QAction(QStringLiteral("Set ARRI LUT Folder…"), this);
    m_arriLutFolderAction->setToolTip(QStringLiteral("Folder with ARRI's LogC3/LogC4 to Rec.709 .cube files, used by the ARRI views"));
    connect(m_arriLutFolderAction, &QAction::triggered, this, &MainWindow::chooseArriLutFolder);

    m_playAction = new QAction(QStringLiteral("Play / Pause"), this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    m_playAction->setShortcutContext(Qt::WidgetShortcut); // handled by the viewer's key filter
    m_playAction->setToolTip(QStringLiteral("Play the image sequence, or the images of the folder (Space)"));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);

    m_playerBarAction = new QAction(QStringLiteral("Player Controls"), this);
    m_playerBarAction->setCheckable(true);
    m_playerBarAction->setChecked(true);
    m_playerBarAction->setToolTip(QStringLiteral("Show the play controls and scrub bar under image sequences"));
    connect(m_playerBarAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(SettingsPlayerBarVisible, on);
        updatePlayerBar();
    });

    m_compareGroup = new QActionGroup(this);
    m_compareGroup->setExclusive(true);
    const struct {
        CompareMode mode;
        const char *text;
    } compareModes[] = {
        {CompareOff, "No Compare"},
        {CompareNoView, "Compare with Original (W)"},
        {ComparePinned, "Compare with Pinned Image"},
    };
    for (const auto &entry : compareModes) {
        auto *action = new QAction(QString::fromLatin1(entry.text), m_compareGroup);
        action->setCheckable(true);
        action->setData(int(entry.mode));
        action->setChecked(entry.mode == CompareOff);
        connect(action, &QAction::triggered, this, [this, mode = entry.mode]() {
            setCompareMode(mode);
        });
    }
    m_pinAction = new QAction(QStringLiteral("Pin Current Image for Compare (P)"), this);
    connect(m_pinAction, &QAction::triggered, this, &MainWindow::pinCurrentImage);

    // Channels: R/G/B/A/L show one channel (or the luminance) as grey; the same key again
    // goes back to RGB.
    m_channelGroup = new QActionGroup(this);
    m_channelGroup->setExclusive(true);
    const struct {
        ImageViewport::Channel channel;
        const char *text;
    } channels[] = {
        {ImageViewport::Channel::Rgb, "RGB"},
        {ImageViewport::Channel::Red, "Red (R)"},
        {ImageViewport::Channel::Green, "Green (G)"},
        {ImageViewport::Channel::Blue, "Blue (B)"},
        {ImageViewport::Channel::Alpha, "Alpha (A)"},
        {ImageViewport::Channel::Luma, "Luminance (L)"},
    };
    for (const auto &entry : channels) {
        auto *action = new QAction(QString::fromLatin1(entry.text), m_channelGroup);
        action->setCheckable(true);
        action->setData(int(entry.channel));
        action->setChecked(entry.channel == ImageViewport::Channel::Rgb);
        connect(action, &QAction::triggered, this, [this, channel = int(entry.channel)]() {
            setChannel(channel, false);
        });
    }

    m_clippingAction = new QAction(QStringLiteral("Show Clipping (C)"), this);
    m_clippingAction->setCheckable(true);
    m_clippingAction->setToolTip(QStringLiteral("Red where the picture is clipped to white, blue where it is crushed to black"));
    connect(m_clippingAction, &QAction::toggled, this, [this](bool on) {
        m_imageViewport->setClippingShown(on);
        if (m_splitPane) {
            m_splitPane->viewport()->setClippingShown(on);
        }
    });

    // Split viewer: a second pane with its own file grid, to put any two images side by side.
    m_splitAction = new QAction(QStringLiteral("Split Viewer (V)"), this);
    m_splitAction->setCheckable(true);
    m_splitAction->setToolTip(QStringLiteral("Show a second image next to this one, picked from its own file grid"));
    connect(m_splitAction, &QAction::toggled, this, &MainWindow::setSplitEnabled);
    m_splitOrientationGroup = new QActionGroup(this);
    m_splitOrientationGroup->setExclusive(true);
    for (const auto &entry : {std::pair<Qt::Orientation, const char *>{Qt::Horizontal, "Side by Side"},
                              std::pair<Qt::Orientation, const char *>{Qt::Vertical, "Top and Bottom"}}) {
        auto *action = new QAction(QString::fromLatin1(entry.second), m_splitOrientationGroup);
        action->setCheckable(true);
        action->setData(int(entry.first));
        action->setChecked(entry.first == Qt::Horizontal);
        connect(action, &QAction::triggered, this, [this, orientation = entry.first]() {
            setSplitOrientation(orientation);
        });
    }
    m_splitSyncAction = new QAction(QStringLiteral("Sync Zoom and Pan"), this);
    m_splitSyncAction->setCheckable(true);
    m_splitSyncAction->setChecked(true);
    m_splitSyncAction->setToolTip(QStringLiteral("Zoom and pan both panes together"));
    connect(m_splitSyncAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(SettingsSplitSync, on);
        if (on && m_splitPane) {
            syncSplitViews(activeViewport());
        }
    });

    m_fullScreenAction = new QAction(QStringLiteral("Full Screen"), this);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_fullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_fullScreenAction, &QAction::toggled, this, &MainWindow::setFullScreenMode);
    addAction(m_fullScreenAction); // keeps F11 working while the menu bar is hidden

    m_exportAction = new QAction(QStringLiteral("Export As…"), this);
    m_exportAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_exportAction->setToolTip(QStringLiteral("Save the picture as shown, with the color view or LUT burned in"));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportImageAs);
    m_batchConvertAction = new QAction(QStringLiteral("Batch Convert…"), this);
    connect(m_batchConvertAction, &QAction::triggered, this, [this]() {
        batchConvert(selectedPaths());
    });
    m_batchRenameAction = new QAction(QStringLiteral("Batch Rename…"), this);
    m_batchRenameAction->setShortcut(QKeySequence(Qt::Key_F2));
    m_batchRenameAction->setToolTip(QStringLiteral("Rename the selected files with a pattern, counter, date, find and replace"));
    connect(m_batchRenameAction, &QAction::triggered, this, [this]() {
        batchRename(selectedPaths());
    });
    m_undoRenameAction = new QAction(QStringLiteral("Undo Rename"), this);
    m_undoRenameAction->setShortcut(QKeySequence::Undo);
    m_undoRenameAction->setEnabled(false);
    connect(m_undoRenameAction, &QAction::triggered, this, [this]() {
        if (m_renameHistory.isEmpty() || m_batchRunning || isViewerVisible()) {
            return;
        }
        QList<BatchRename::Move> back;
        const QList<BatchRename::Move> last = m_renameHistory.constLast();
        for (const BatchRename::Move &move : last) {
            back.append({move.to, move.from});
        }
        runRename(back, true);
    });

    m_copyImageAction = new QAction(QStringLiteral("Copy Image"), this);
    m_copyImageAction->setShortcut(QKeySequence::Copy);
    m_copyImageAction->setToolTip(QStringLiteral("Copy the picture as shown (with the color view) to the clipboard"));
    connect(m_copyImageAction, &QAction::triggered, this, &MainWindow::copyImageToClipboard);

    m_copyPathAction = new QAction(QStringLiteral("Copy Path"), this);
    m_copyPathAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(m_copyPathAction, &QAction::triggered, this, [this]() {
        const QStringList paths = isViewerVisible() ? QStringList() : selectedPaths();
        copyPathToClipboard(paths.isEmpty() ? currentPath() : paths.join(QLatin1Char('\n')));
    });

    // File clipboard, like Explorer: files copied here paste in Explorer and the other way round.
    // Ctrl+C is m_copyImageAction's shortcut; in the file browser it copies the selected files.
    m_cutAction = new QAction(QStringLiteral("Cut"), this);
    m_cutAction->setShortcut(QKeySequence::Cut);
    connect(m_cutAction, &QAction::triggered, this, [this]() {
        copySelectedFiles(true);
    });
    // Shows Ctrl+C without owning it (the shortcut belongs to m_copyImageAction).
    m_copyFilesAction = new QAction(QStringLiteral("Copy\t%1").arg(QKeySequence(QKeySequence::Copy).toString(QKeySequence::NativeText)), this);
    connect(m_copyFilesAction, &QAction::triggered, this, [this]() {
        copySelectedFiles(false);
    });
    m_pasteAction = new QAction(QStringLiteral("Paste"), this);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, this, [this]() {
        if (!isViewerVisible()) {
            pasteFiles(m_currentFolderPath);
        }
    });
    m_selectAllAction = new QAction(QStringLiteral("Select All"), this);
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, this, [this]() {
        if (!isViewerVisible()) {
            m_fileGrid->selectionModel()->select(visibleGridSelection(0, m_fileGrid->count() - 1),
                                                 QItemSelectionModel::ClearAndSelect);
        }
    });
    m_invertSelectionAction = new QAction(QStringLiteral("Invert Selection"), this);
    connect(m_invertSelectionAction, &QAction::triggered, this, [this]() {
        if (isViewerVisible()) {
            return;
        }
        m_fileGrid->selectionModel()->select(visibleGridSelection(0, m_fileGrid->count() - 1),
                                             QItemSelectionModel::Toggle);
    });

#ifdef Q_OS_WIN
    m_showInExplorerAction = new QAction(QStringLiteral("Show in Explorer"), this);
#else
    m_showInExplorerAction = new QAction(QStringLiteral("Show in File Manager"), this);
#endif
    m_showInExplorerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_showInExplorerAction, &QAction::triggered, this, [this]() {
        showInExplorer(currentPath());
    });

    // Metadata: a dock on the right (can be moved or floated); remembered with the window state.
    m_metadataPanel = new MetadataPanel(this);
    m_metadataDock = new QDockWidget(QStringLiteral("Metadata"), this);
    m_metadataDock->setObjectName(QStringLiteral("MetadataDock"));
    m_metadataDock->setWidget(m_metadataPanel);
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_metadataDock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
    m_metadataDock->hide();
    m_metadataAction = m_metadataDock->toggleViewAction();
    m_metadataAction->setText(QStringLiteral("Metadata"));
    m_metadataAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_metadataAction->setToolTip(QStringLiteral("Show all the header information of the file (I or Ctrl+I)"));
    addAction(m_metadataAction);
    connect(m_metadataDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            scheduleMetadata();
        }
        QTimer::singleShot(0, this, &MainWindow::alignSizeSlider);
    });
    m_metadataPool.setMaxThreadCount(1);

    // Scopes: histogram and waveform of the viewed image, another dock on the right.
    m_scopesPanel = new ScopesPanel(this);
    m_scopesDock = new QDockWidget(QStringLiteral("Scopes"), this);
    m_scopesDock->setObjectName(QStringLiteral("ScopesDock"));
    m_scopesDock->setWidget(m_scopesPanel);
    m_scopesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_scopesDock->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, m_scopesDock);
    m_scopesDock->hide();
    m_scopesAction = m_scopesDock->toggleViewAction();
    m_scopesAction->setText(QStringLiteral("Scopes"));
    m_scopesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    m_scopesAction->setToolTip(QStringLiteral("Histogram and waveform of the image (S or Ctrl+Shift+H)"));
    addAction(m_scopesAction);
    connect(m_scopesDock, &QDockWidget::visibilityChanged, this, [this]() {
        QTimer::singleShot(0, this, &MainWindow::alignSizeSlider);
    });
    m_metadataTimer.setSingleShot(true);
    m_metadataTimer.setInterval(120); // while scrubbing or playing, wait until it settles
    connect(&m_metadataTimer, &QTimer::timeout, this, &MainWindow::loadMetadata);

    m_loadLutAction = new QAction(QStringLiteral("Load LUT File…"), this);
    m_loadLutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    connect(m_loadLutAction, &QAction::triggered, this, &MainWindow::chooseLutFile);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(m_openImageAction);
    m_recentImagesMenu = fileMenu->addMenu(QStringLiteral("Recent Images"));
    m_recentImagesMenu->setToolTipsVisible(true);
    updateRecentImagesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAction);
    fileMenu->addAction(m_batchConvertAction);
    fileMenu->addAction(m_batchRenameAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_copyImageAction);
    fileMenu->addAction(m_copyPathAction);
    fileMenu->addAction(m_showInExplorerAction);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    editMenu->addAction(m_undoRenameAction);
    editMenu->addSeparator();
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_copyImageAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addSeparator();
    editMenu->addAction(m_selectAllAction);
    editMenu->addAction(m_invertSelectionAction);
    connect(editMenu, &QMenu::aboutToShow, this, &MainWindow::updateEditActions);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    QMenu *gridViewMenu = viewMenu->addMenu(QStringLiteral("Files As"));
    gridViewMenu->addActions(m_gridViewGroup->actions().mid(0, 4));
    gridViewMenu->addSeparator();
    gridViewMenu->addActions(m_gridViewGroup->actions().mid(4));
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleFoldersAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitToWindowAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_playAction);
    viewMenu->addAction(m_playerBarAction);
    viewMenu->addSeparator();
    QMenu *compareMenu = viewMenu->addMenu(QStringLiteral("Compare"));
    compareMenu->addActions(m_compareGroup->actions());
    compareMenu->addSeparator();
    compareMenu->addAction(m_pinAction);
    QMenu *splitMenu = viewMenu->addMenu(QStringLiteral("Split Viewer"));
    splitMenu->addAction(m_splitAction);
    splitMenu->addSeparator();
    splitMenu->addActions(m_splitOrientationGroup->actions());
    splitMenu->addSeparator();
    splitMenu->addAction(m_splitSyncAction);
    viewMenu->addSeparator();
    QMenu *channelMenu = viewMenu->addMenu(QStringLiteral("Channels"));
    channelMenu->addActions(m_channelGroup->actions());
    viewMenu->addAction(m_clippingAction);
    viewMenu->addAction(m_fullScreenAction);
    viewMenu->addAction(m_metadataAction);
    viewMenu->addAction(m_scopesAction);
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Edit Path (Alt+D)"), this, [this]() {
        m_pathBar->startEditing();
    });
    viewMenu->addAction(QStringLiteral("Keyboard Shortcuts"), this, [this]() {
        QMessageBox::information(this, QStringLiteral("Keyboard Shortcuts"), QStringLiteral(
            "Viewer:\n"
            "  Left / Right  previous / next image (hold to scrub)\n"
            "  Shift+Left / Right  10 images back / forward\n"
            "  Home / End  first / last image\n"
            "  Space  play / pause\n"
            "  In an image sequence these step through its frames in frame order, and the\n"
            "    player bar below the image has a scrub bar and the playback speed\n"
            "  W  compare wipe on / off\n"
            "  P  pin this image as the compare image\n"
            "  V  split viewer on / off (a second pane with its own file grid)\n"
            "  Tab  switch the active pane; keys and zoom act on the active pane\n"
            "  In the split pane: Esc / Backspace  its file grid,  Enter  open,\n"
            "    Backspace in the grid  up one folder,  Esc in the grid  back to the image\n"
            "  F  fit to window,  1  100% zoom\n"
            "  R / G / B / A / L  red / green / blue / alpha / luminance (again for RGB)\n"
            "  C  show clipping\n"
            "  I  metadata panel (Ctrl+I anywhere)\n"
            "  S  scopes: histogram and waveform (Ctrl+Shift+H anywhere)\n"
            "  Ctrl+C  copy the picture,  Ctrl+Shift+S  export it with the LUT burned in\n"
            "  Esc  leave full screen, then back to the grid\n\n"
            "Color:\n"
            "  Ctrl+1  Auto (from file tags)\n"
            "  Ctrl+0 / 3 / 4  None / LogC3 / LogC4\n"
            "  Ctrl+L  LUT file,  Ctrl+Shift+L  load a LUT\n\n"
            "File browser (like Explorer):\n"
            "  Ctrl+F  search the current folder,  Esc  clear search\n"
            "  Click, Ctrl+click, Shift+click, or drag a box around files to select them\n"
            "  Shift+arrows extend the selection,  Ctrl+arrows move without selecting,\n"
            "  Ctrl+Space selects or unselects,  Ctrl+A selects all\n"
            "  Ctrl+C / Ctrl+X / Ctrl+V  copy / cut / paste files (also to and from Explorer)\n"
            "  F2  rename the selected files (Batch Rename),  Ctrl+Z  undo the last rename\n\n"
            "Anywhere:\n"
            "  Alt+D or F4  type or paste a path\n"
            "  F11  full screen\n"
            "  Ctrl+Shift+C  copy the path,  Ctrl+E  show in Explorer\n"
            "  Ctrl+O  open an image (or drop files and folders on the window)"));
    });

    // Color has its own top-level menu (also opened from the toolbar's Color button).
    QMenu *colorMenu = menuBar()->addMenu(QStringLiteral("Color"));
    m_colorMenu = colorMenu;
    colorMenu->addActions(m_viewTransformGroup->actions());
    colorMenu->addSeparator();
    colorMenu->addAction(m_loadLutAction);
    colorMenu->addAction(m_arriLutFolderAction);
    m_recentLutsMenu = colorMenu->addMenu(QStringLiteral("Recent LUTs"));
    colorMenu->addSeparator();
    QMenu *inputMenu = colorMenu->addMenu(QStringLiteral("EXR Input"));
    inputMenu->setToolTipsVisible(true);
    inputMenu->addActions(m_inputGroup->actions());

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("Help"));
    helpMenu->addAction(QStringLiteral("Check for Updates"), this, [this]() { showUpdateDialog(false); });
    helpMenu->addAction(QStringLiteral("About OnionImageViewer"), this, [this]() { showUpdateDialog(true); });
}

void MainWindow::showUpdateDialog(bool about)
{
    if (m_updateDialog) {
        m_updateDialog->raise();
        m_updateDialog->activateWindow();
        return;
    }
    auto *box = new QDialog(this);
    m_updateDialog = box;
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(about ? QStringLiteral("About OnionImageViewer") : QStringLiteral("Check for Updates"));
    connect(box, &QDialog::finished, this, [this]() { m_updateDialog.clear(); });
    auto *layout = new QGridLayout(box);
    if (about) {
        auto *icon = new QLabel(box);
        icon->setPixmap(QIcon(QStringLiteral(":/icons/OnionImageViewer.png")).pixmap(QSize(96, 96)));
        layout->addWidget(icon, 0, 0, Qt::AlignTop);

        const QString installedVersion = QCoreApplication::applicationVersion();
        auto *details = new QLabel(QStringLiteral("<h3>OnionImageViewer %1</h3>"
                                                  "<p>Image viewer and batch converter.</p>"
                                                  "<p>Built with Qt %2.</p>")
                                       .arg(installedVersion.toHtmlEscaped(), QString::fromLatin1(qVersion())), box);
        details->setTextFormat(Qt::RichText);
        layout->addWidget(details, 0, 1);
    }

    auto *status = new QLabel(box);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->hide();
    layout->addWidget(status, 1, 0, 1, 2);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, box);
    auto *check = buttons->addButton(QStringLiteral("Check for Updates"), QDialogButtonBox::ActionRole);
    auto *releasePage = buttons->addButton(QStringLiteral("Open Release Page"), QDialogButtonBox::ActionRole);
    releasePage->hide();
    layout->addWidget(buttons, 2, 0, 1, 2);
    connect(buttons, &QDialogButtonBox::rejected, box, &QDialog::reject);

    connect(releasePage, &QPushButton::clicked, box, [releasePage, status]() {
        if (!QDesktopServices::openUrl(releasePage->property("releaseUrl").toUrl())) {
            status->setText(QStringLiteral("Could not open the release page in your browser."));
        }
    });
    const auto showChecking = [status, check, releasePage]() {
        releasePage->hide();
        status->setText(QStringLiteral("Checking for updates…"));
        status->show();
        check->setEnabled(false);
    };
    connect(check, &QPushButton::clicked, box, [this, showChecking]() {
        showChecking();
        m_updateChecker->check();
    });
    connect(m_updateChecker, &UpdateChecker::finished, box,
            [status, check, releasePage](UpdateChecker::Status result, const QString &version, const QUrl &url) {
        check->setEnabled(true);
        status->show();
        switch (result) {
        case UpdateChecker::Status::Available:
            status->setText(QStringLiteral("Version %1 is available.").arg(version));
            releasePage->setProperty("releaseUrl", url);
            releasePage->show();
            break;
        case UpdateChecker::Status::UpToDate:
            status->setText(QStringLiteral("OnionImageViewer is up to date."));
            break;
        case UpdateChecker::Status::NoStableRelease:
            status->setText(QStringLiteral("GitHub has no published stable release yet."));
            break;
        case UpdateChecker::Status::NetworkError:
            status->setText(QStringLiteral("Could not check for updates. Try again later."));
            break;
        case UpdateChecker::Status::InvalidVersion:
            status->setText(QStringLiteral("Could not compare the installed and published versions."));
            break;
        }
    });
    if (about) {
        if (m_updateChecker->isChecking()) {
            showChecking();
        }
        box->open();
    } else {
        showChecking();
        box->show();
        m_updateChecker->check();
    }
}

void MainWindow::createToolbar()
{
    QToolBar *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(20, 20));

    m_toggleFoldersAction->setIcon(Icons::icon(Icons::Kind::FolderPanel));
    m_toggleFoldersAction->setToolTip(QStringLiteral("Show or hide the folder tree"));
    m_openImageAction->setIcon(Icons::icon(Icons::Kind::Open));
    m_openImageAction->setToolTip(QStringLiteral("Open an image (Ctrl+O)"));
    m_fitToWindowAction->setIcon(Icons::icon(Icons::Kind::FitToWindow));
    m_fitToWindowAction->setToolTip(QStringLiteral("Fit to window (F)"));
    m_actualSizeAction->setIcon(Icons::icon(Icons::Kind::ActualSize));
    m_actualSizeAction->setToolTip(QStringLiteral("100% zoom (1)"));
    m_metadataAction->setIcon(Icons::icon(Icons::Kind::Metadata));

    toolbar->addAction(m_toggleFoldersAction);
    toolbar->addSeparator();
    toolbar->addAction(m_openImageAction);
    toolbar->addAction(m_fitToWindowAction);
    toolbar->addAction(m_actualSizeAction);
    toolbar->addSeparator();
    toolbar->addAction(m_metadataAction);
    toolbar->addSeparator();

    // Shows the current view transform; click to pick another (same menu as Color in the menu bar).
    m_colorButton = new QToolButton(toolbar);
    m_colorButton->setMenu(m_colorMenu);
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    m_colorButton->setIcon(Icons::icon(Icons::Kind::Color));
    m_colorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_colorButton->setToolTip(QStringLiteral("View transform / LUT"));
    toolbar->addWidget(m_colorButton);
    updateColorButton(m_view);
}

void MainWindow::updateColorButton(ColorTransform::View view)
{
    if (!m_colorButton) {
        return;
    }
    QString text = ColorTransform::displayName(view);
    if (view == ColorTransform::View::Auto && isViewerVisible() && !m_currentSourceImage.isNull()) {
        text = QStringLiteral("Auto: %1").arg(ColorTransform::displayName(ColorTransform::resolveView(view, m_currentSourceImage)));
    }
    m_colorButton->setText(text);
}

void MainWindow::createCentralLayout()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_folderTreePanel = new FolderTreePanel(m_splitter);
    m_folderTreePanel->setMinimumWidth(180);

    auto *rightPanel = new QWidget(m_splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Back and Up sit left of the path, like in Explorer.
    auto *navigationRow = new QWidget(rightPanel);
    auto *navigationLayout = new QHBoxLayout(navigationRow);
    navigationLayout->setContentsMargins(4, 0, 0, 0);
    navigationLayout->setSpacing(2);
    m_pathBar = new PathBar(navigationRow);
    for (QAction *action : {m_backAction, m_upAction}) {
        auto *button = new QToolButton(navigationRow);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(m_pathBar->minimumHeight() + 4, m_pathBar->minimumHeight());
        button->setFocusPolicy(Qt::NoFocus);
        navigationLayout->addWidget(button);
    }
    navigationLayout->addWidget(m_pathBar, 1);
    m_gridSearch = new QLineEdit(navigationRow);
    m_gridSearch->setPlaceholderText(QStringLiteral("Search this folder"));
    m_gridSearch->setAccessibleName(QStringLiteral("Search this folder"));
    m_gridSearch->addAction(Icons::icon(Icons::Kind::Search), QLineEdit::LeadingPosition);
    m_gridSearch->setClearButtonEnabled(true);
    m_gridSearch->setToolTip(QStringLiteral("Filter by name. Wildcards: *.psd, IMG_00??.dng, *.psd;*.tif"));
    m_gridSearch->setMaximumWidth(240);
    navigationLayout->addWidget(m_gridSearch);
    connect(m_gridSearch, &QLineEdit::textChanged, this, &MainWindow::applyGridFilters);
    m_imagesOnly = new QCheckBox(QStringLiteral("Images only"), navigationRow);
    m_imagesOnly->setToolTip(QStringLiteral("Hide files the viewer does not support"));
    m_imagesOnly->setChecked(QSettings().value(SettingsImagesOnly, false).toBool());
    navigationLayout->addWidget(m_imagesOnly);
    connect(m_imagesOnly, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue(SettingsImagesOnly, checked);
        applyGridFilters();
    });
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (m_gridSearch->isVisible()) {
            m_gridSearch->setFocus(Qt::ShortcutFocusReason);
            m_gridSearch->selectAll();
        }
    });
    // "+" at the end of the address bar opens the split viewer (shown in the viewer while it is off).
    m_openSplitButton = new QToolButton(navigationRow);
    m_openSplitButton->setText(QStringLiteral("+"));
    m_openSplitButton->setAutoRaise(true);
    m_openSplitButton->setFixedSize(m_pathBar->minimumHeight() + 4, m_pathBar->minimumHeight());
    m_openSplitButton->setFocusPolicy(Qt::NoFocus);
    m_openSplitButton->setToolTip(QStringLiteral("Open a split view with its own file grid (V)"));
    QFont plusFont = m_openSplitButton->font();
    plusFont.setPointSizeF(plusFont.pointSizeF() * 1.4);
    m_openSplitButton->setFont(plusFont);
    m_openSplitButton->hide();
    navigationLayout->addWidget(m_openSplitButton);
    connect(m_openSplitButton, &QToolButton::clicked, this, [this]() {
        m_splitAction->setChecked(true);
    });
    // The row holds the main navigation and, side by side, the split pane's own bar.
    m_mainNavigation = navigationRow;
    m_navigationRow = new QWidget(rightPanel);
    auto *rowLayout = new QHBoxLayout(m_navigationRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(navigationRow, 1);
    rightLayout->addWidget(m_navigationRow);
    connect(m_pathBar, &PathBar::pathActivated, this, &MainWindow::handlePathActivated);
    for (const QKeySequence &keys : {QKeySequence(Qt::ALT | Qt::Key_D), QKeySequence(Qt::Key_F4)}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, m_pathBar, &PathBar::startEditing);
    }

    m_contentStack = new QStackedWidget(rightPanel);

    m_fileGrid = new QListWidget(m_contentStack);
    m_fileGrid->setViewMode(QListView::IconMode);
    m_fileGrid->setResizeMode(QListView::Adjust);
    m_fileGrid->setMovement(QListView::Static);
    m_fileGrid->setUniformItemSizes(true);
    m_fileGrid->setIconSize(QSize(m_thumbnailDisplaySize, m_thumbnailDisplaySize));
    m_fileGrid->setGridSize(QSize(qMax(m_thumbnailDisplaySize + 42, 112), m_thumbnailDisplaySize + 40));
    m_fileGrid->setSpacing(10);
    // Explorer-style selection: Ctrl/Shift+click, Shift/Ctrl+arrows, Ctrl+A and a box dragged
    // from empty space.
    m_fileGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileGrid->setSelectionRectVisible(true);
    m_fileGrid->setWordWrap(true);
    m_fileGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileGrid, &QWidget::customContextMenuRequested, this, &MainWindow::showGridContextMenu);
    m_fileGrid->setTextElideMode(Qt::ElideRight);
    m_fileDelegate = new FileItemDelegate(m_fileGrid);
    m_fileGrid->setItemDelegate(m_fileDelegate);

    // Column header for the Details view (the grid itself is a list; the delegate lines the
    // columns up with this header).
    m_detailsHeader = new QHeaderView(Qt::Horizontal, rightPanel);
    auto *headerModel = new QStandardItemModel(0, FileItemDelegate::ColumnCount, m_detailsHeader);
    headerModel->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Date modified"),
                                            QStringLiteral("Type"), QStringLiteral("Size")});
    m_detailsHeader->setModel(headerModel);
    m_detailsHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_detailsHeader->setSectionResizeMode(QHeaderView::Interactive);
    m_detailsHeader->setStretchLastSection(false);
    m_detailsHeader->setSectionsClickable(false);
    m_detailsHeader->setMinimumSectionSize(50);
    m_detailsHeader->setFixedHeight(m_detailsHeader->fontMetrics().height() + 10);
    m_detailsHeader->resizeSection(FileItemDelegate::NameColumn, 380);
    m_detailsHeader->resizeSection(FileItemDelegate::ModifiedColumn, 150);
    m_detailsHeader->resizeSection(FileItemDelegate::TypeColumn, 120);
    m_detailsHeader->resizeSection(FileItemDelegate::SizeColumn, 100);
    m_detailsHeader->hide();
    m_fileDelegate->setHeader(m_detailsHeader);
    connect(m_detailsHeader, &QHeaderView::sectionResized, this, [this]() {
        if (m_gridView == GridView::Details) {
            m_fileGrid->doItemsLayout();
            m_fileGrid->viewport()->update();
        }
    });
    connect(m_fileGrid->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_detailsHeader->setOffset(value - m_fileGrid->frameWidth());
        m_thumbnailScheduleTimer.start();
    });
    m_detailsHeader->setOffset(-m_fileGrid->frameWidth());

    auto *imagePage = new QWidget(m_contentStack);
    m_imagePage = imagePage;
    auto *imageLayout = new QGridLayout(imagePage);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->setSpacing(0);

    // The main viewport, and the split viewer's second pane next to it (hidden until V).
    m_viewerSplitter = new QSplitter(Qt::Horizontal, imagePage);
    m_viewerSplitter->setChildrenCollapsible(false);
    m_viewerSplitter->setHandleWidth(4);
    m_imageViewport = new ImageViewport(m_viewerSplitter);
    m_splitPane = new SplitPane(m_imageLoader, &loadThumbnailImage,
                                [this](const QString &path) { return isImageFile(path); }, m_viewerSplitter);
    m_splitPane->hide();
    m_viewerSplitter->addWidget(m_imageViewport);
    m_viewerSplitter->addWidget(m_splitPane);
    imageLayout->addWidget(m_viewerSplitter, 0, 0);
    imageLayout->setRowStretch(0, 1);

    // Transport bar for image sequences, under both panes; it drives the main pane.
    m_playerBar = new PlayerBar(imagePage);
    m_playerBar->hide();
    imageLayout->addWidget(m_playerBar, 1, 0);
    connect(m_playerBar, &PlayerBar::playToggled, this, &MainWindow::togglePlayback);
    connect(m_playerBar, &PlayerBar::stepRequested, this, [this](int delta) {
        stopPlayback();
        stepImage(delta);
    });
    connect(m_playerBar, &PlayerBar::firstRequested, this, [this]() {
        stopPlayback();
        goToPlaybackPosition(0);
    });
    connect(m_playerBar, &PlayerBar::lastRequested, this, [this]() {
        stopPlayback();
        goToPlaybackPosition(playbackCount() - 1);
    });
    connect(m_playerBar, &PlayerBar::scrubbed, this, [this](int position) {
        stopPlayback();
        goToPlaybackPosition(position);
    });
    connect(m_playerBar, &PlayerBar::fpsChanged, this, &MainWindow::setPlaybackFps);
    connect(m_playerBar, &PlayerBar::loopChanged, this, [this](bool on) {
        m_playLoop = on;
        QSettings().setValue(SettingsPlaybackLoop, on);
    });
    connect(m_playerBar, &PlayerBar::realTimeChanged, this, [this](bool on) {
        m_playRealTime = on;
        QSettings().setValue(SettingsPlaybackRealTime, on);
        restartPlaybackClock();
    });
    connect(m_viewerSplitter, &QSplitter::splitterMoved, this, &MainWindow::layoutSplitHeader);
    connect(m_contentStack, &QStackedWidget::currentChanged, this, &MainWindow::layoutSplitHeader);
    connect(m_splitPane, &SplitPane::closeRequested, this, [this]() {
        m_splitAction->setChecked(false);
    });
    connect(m_splitPane, &SplitPane::contentChanged, this, [this]() {
        updateImageInfo();
        if (isSplitPaneActive()) {
            scheduleMetadata();
            updateScopes();
        }
    });
    connect(m_splitPane, &SplitPane::selectionChanged, this, [this]() {
        if (isSplitPaneActive()) {
            scheduleMetadata();
        }
    });
    connect(m_splitPane, &SplitPane::imageOpened, this, [this]() {
        if (m_splitSyncAction->isChecked()) {
            syncSplitViews(m_imageViewport);
        }
    });
    connect(m_imageViewport, &ImageViewport::viewChangedByUser, this, [this]() {
        syncSplitViews(m_imageViewport);
    });
    connect(m_splitPane->viewport(), &ImageViewport::viewChangedByUser, this, [this]() {
        syncSplitViews(m_splitPane->viewport());
    });
    connect(m_splitPane->viewport(), &ImageViewport::hoveredPixelChanged, this, [this](const QPoint &pixel) {
        showPixelReadout(pixel, m_splitPane->sourceImage(), m_splitPane->displayImage());
        if (isSplitPaneActive()) {
            m_scopesPanel->setHoveredPixel(pixel);
        }
    });

    // Close / previous / next float over the image (placed in layoutOverlayButtons) and only show
    // while the mouse moves over the viewer.
    m_closeImageButton = new OverlayButton(OverlayButton::Icon::Close, imagePage);
    m_previousImageButton = new OverlayButton(OverlayButton::Icon::Previous, imagePage);
    m_nextImageButton = new OverlayButton(OverlayButton::Icon::Next, imagePage);
    m_closeImageButton->setToolTip(QStringLiteral("Back to the folder (Esc)"));
    m_previousImageButton->setToolTip(QStringLiteral("Previous image (Left)"));
    m_nextImageButton->setToolTip(QStringLiteral("Next image (Right)"));
    for (OverlayButton *button : {m_closeImageButton, m_previousImageButton, m_nextImageButton}) {
        button->raise();
    }
    imagePage->installEventFilter(this);

    m_contentStack->addWidget(m_fileGrid);
    m_contentStack->addWidget(imagePage);
    rightLayout->addWidget(m_detailsHeader);
    rightLayout->addWidget(m_contentStack, 1);

    m_splitter->addWidget(m_folderTreePanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setCollapsible(0, true);
    m_splitter->setCollapsible(1, false);
    m_splitter->setSizes({260, 1020});

    setCentralWidget(m_splitter);

    // Status bar: image info on the left, the pixel under the mouse on the right.
    m_infoLabel = new QLabel(this);
    m_pixelLabel = new QLabel(this);
    m_pixelLabel->setStyleSheet(QStringLiteral("font-family: Consolas, 'DejaVu Sans Mono', monospace;"));
    statusBar()->addPermanentWidget(m_infoLabel, 1);
    statusBar()->addPermanentWidget(m_pixelLabel);
    // The displayed color under the mouse, as a small square right of the numbers.
    m_swatchLabel = new QLabel(this);
    m_swatchLabel->setFixedSize(14, 14);
    m_swatchLabel->setToolTip(QStringLiteral("Color under the mouse. Right-click the image to copy it."));
    m_swatchLabel->hide();
    statusBar()->addPermanentWidget(m_swatchLabel);

    // Thumbnail size (grid only; Ctrl+scroll wheel over the grid does the same).
    m_thumbnailSizeSlider = new QSlider(Qt::Horizontal, this);
    m_thumbnailSizeSlider->setRange(MinThumbnailDisplaySize, MaxThumbnailDisplaySize);
    m_thumbnailSizeSlider->setSingleStep(ThumbnailDisplayStep);
    m_thumbnailSizeSlider->setPageStep(ThumbnailDisplayStep * 2);
    m_thumbnailSizeSlider->setFixedWidth(150);
    m_thumbnailSizeSlider->setValue(m_thumbnailDisplaySize);
    m_thumbnailSizeSlider->setToolTip(QStringLiteral("Thumbnail size (Ctrl + scroll wheel; right-click the files for more views)"));
    m_thumbnailSizeSlider->setFocusPolicy(Qt::NoFocus);
    statusBar()->addPermanentWidget(m_thumbnailSizeSlider);
    // Empty space right of the slider, as wide as whatever sits right of the grid (the metadata
    // panel), so the slider always ends at the grid's bottom-right corner.
    m_sliderSpacer = new QWidget(this);
    m_sliderSpacer->setFixedWidth(0);
    statusBar()->addPermanentWidget(m_sliderSpacer);
    connect(m_thumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::setThumbnailDisplaySize);
    connect(m_contentStack, &QStackedWidget::currentChanged, this, &MainWindow::updateGridChrome);
    connect(m_imageViewport, &ImageViewport::hoveredPixelChanged, this, &MainWindow::updatePixelReadout);
    m_imageViewport->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageViewport, &QWidget::customContextMenuRequested, this, &MainWindow::showViewerContextMenu);

    connect(m_folderTreePanel, &FolderTreePanel::folderSelected, this, &MainWindow::handleFolderSelected);
    connect(m_folderTreePanel, &FolderTreePanel::pinnedFoldersChanged, this, [](const QStringList &folders) {
        QSettings().setValue(SettingsPinnedFolders, folders); // saved right away, not only on exit
    });
    connect(m_fileGrid, &QListWidget::itemDoubleClicked, this, &MainWindow::handleGridItemActivated);
    connect(m_fileGrid, &QListWidget::currentItemChanged, this, &MainWindow::scheduleMetadata);
    connect(m_fileGrid, &QListWidget::itemSelectionChanged, this, &MainWindow::updateImageInfo);
    connect(m_fileGrid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (!m_shiftSelecting) {
            m_selectionAnchor = current; // Shift+click and Shift+arrows select from here
        }
    });
    // Keep Cut/Copy/Paste (and their shortcuts) enabled only when they can do something.
    connect(m_fileGrid, &QListWidget::itemSelectionChanged, this, &MainWindow::updateEditActions);
    connect(m_contentStack, &QStackedWidget::currentChanged, this, &MainWindow::updateEditActions);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updateEditActions);
    updateEditActions();
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_fileGrid->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_closeImageButton, &OverlayButton::clicked, this, [this]() {
        stopPlayback();
        showGrid();
    });
    connect(m_previousImageButton, &OverlayButton::clicked, this, &MainWindow::showPreviousImage);
    connect(m_nextImageButton, &OverlayButton::clicked, this, &MainWindow::showNextImage);
}

void MainWindow::applyDarkPalette()
{
    qApp->setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(37, 37, 38));
    palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    palette.setColor(QPalette::Base, QColor(30, 30, 30));
    palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    palette.setColor(QPalette::ToolTipBase, QColor(230, 230, 230));
    palette.setColor(QPalette::ToolTipText, QColor(30, 30, 30));
    palette.setColor(QPalette::Text, QColor(230, 230, 230));
    palette.setColor(QPalette::Button, QColor(45, 45, 48));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::BrightText, QColor(255, 255, 255));
    palette.setColor(QPalette::Highlight, QColor(0, 122, 204));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(160, 160, 160));

    qApp->setPalette(palette);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    restoreGeometry(settings.value(SettingsMainWindowGeometry).toByteArray());
    if (!restoreState(settings.value(SettingsMainWindowState).toByteArray())) {
        resizeDocks({m_metadataDock}, {380}, Qt::Horizontal);
    }

    const QByteArray splitterState = settings.value(SettingsSplitterState).toByteArray();
    if (!splitterState.isEmpty()) {
        m_splitter->restoreState(splitterState);
    }

    // Until the user pins or unpins something, Quick access shows Explorer's usual folders.
    m_folderTreePanel->setPinnedFolders(settings.contains(SettingsPinnedFolders)
                                            ? settings.value(SettingsPinnedFolders).toStringList()
                                            : FolderTreePanel::defaultPinnedFolders());
    m_folderTreePanel->setQuickAccessExpanded(settings.value(SettingsQuickAccessExpanded, true).toBool());
    m_folderTreePanel->setThisPcExpanded(settings.value(SettingsThisPcExpanded, true).toBool());

    m_playbackFps = settings.value(SettingsPlaybackFps, DefaultPlaybackFps).toDouble();
    if (!(m_playbackFps > 0 && m_playbackFps <= 1000)) {
        m_playbackFps = DefaultPlaybackFps;
    }
    m_playLoop = settings.value(SettingsPlaybackLoop, true).toBool();
    m_playRealTime = settings.value(SettingsPlaybackRealTime, false).toBool();
    {
        const QSignalBlocker blocker(m_playerBar);
        m_playerBar->setFps(m_playbackFps);
        m_playerBar->setLoop(m_playLoop);
        m_playerBar->setRealTime(m_playRealTime);
    }
    m_playerBarAction->setChecked(settings.value(SettingsPlayerBarVisible, true).toBool());

    const bool foldersVisible = settings.value(SettingsFoldersVisible, false).toBool();
    m_toggleFoldersAction->setChecked(foldersVisible);
    setFolderPanelVisible(foldersVisible);

    m_detailsHeader->restoreState(settings.value(SettingsDetailsColumns).toByteArray());
    const QString gridViewKeyValue = settings.value(SettingsGridView).toString();
    GridView gridView = GridView::Icons;
    for (GridView view : {GridView::List, GridView::Details, GridView::Tiles}) {
        if (gridViewKeyValue == QLatin1String(gridViewKey(static_cast<int>(view)))) {
            gridView = view;
        }
    }
    setGridView(gridView, settings.value(SettingsThumbnailSize, 128).toInt());

    findArriLuts();
    const QString lutFile = settings.value(SettingsLutFile).toString();
    if (!lutFile.isEmpty()) {
        QString error;
        if (!ColorTransform::loadLut(lutFile, &error)) {
            statusBar()->showMessage(QStringLiteral("Could not load LUT %1: %2").arg(lutFile, error), 8000);
        }
    }
    m_view = ColorTransform::fromSettingsKey(settings.value(SettingsViewTransform).toString());
    if (m_view == ColorTransform::View::CustomLut && !ColorTransform::hasLut()) {
        m_view = ColorTransform::View::None;
    }
    m_input = ColorTransform::inputFromSettingsKey(settings.value(SettingsInput).toString());
    // The background loader renders the frames, so it has to know the restored view too.
    m_frameLoader->setRender(m_view, m_input);
    m_splitPane->setRender(m_view, m_input);
    setSplitOrientation(settings.value(SettingsSplitOrientation).toString() == QLatin1String("vertical")
                            ? Qt::Vertical : Qt::Horizontal);
    m_splitSyncAction->setChecked(settings.value(SettingsSplitSync, true).toBool());
    updateLutActions();
    updateViewTransformActions();
    updateInputActions();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(SettingsMainWindowGeometry, saveGeometry());
    settings.setValue(SettingsMainWindowState, saveState());
    settings.setValue(SettingsFoldersVisible, m_folderTreePanel->isVisible());
    settings.setValue(SettingsQuickAccessExpanded, m_folderTreePanel->isQuickAccessExpanded());
    settings.setValue(SettingsThisPcExpanded, m_folderTreePanel->isThisPcExpanded());
    settings.setValue(SettingsSplitterState, m_splitter->saveState());
    settings.setValue(SettingsDetailsColumns, m_detailsHeader->saveState());
}

void MainWindow::openImage()
{
    const QString filter = QStringLiteral("Images (*.ai *.bmp%1 *.dpx *.eps *.epsf *.epsi *.exr *.gif *.heic *.heif *.hif *.jpg *.jpeg *.png *.pbm *.pdf *.pgm *.ppm *.psb *.psd *.tga *.tif *.tiff *.webp *.xbm *.xpm);;All Files (*.*)")
                               .arg(Dng::isAvailable() ? QStringLiteral(" *.dng") : QString());
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Image"),
        QString(),
        filter);

    if (filePath.isEmpty()) {
        return;
    }

    openImagePath(filePath);
}

void MainWindow::openImagePath(const QString &filePath)
{
    if (!m_currentImagePaths.contains(filePath)) {
        setImageListFromFolder(QFileInfo(filePath).absolutePath());
    }
    int index = m_currentImagePaths.indexOf(filePath);
    if (index < 0) {
        m_currentImagePaths.append(filePath);
        index = m_currentImagePaths.size() - 1;
    }
    m_sequence.clear(); // the list may have changed: look the sequence up again
    m_sequencePositions.clear();

    addRecentImage(filePath);
    stopPlayback();
    m_freshOpen = true; // fit the first image; keep zoom and position while stepping after that
    m_displayedIndex = -1;
    m_contentStack->setCurrentWidget(m_imagePage);
    goToImage(index);
}

bool MainWindow::isViewerVisible() const
{
    return m_contentStack && m_imagePage && m_contentStack->currentWidget() == m_imagePage;
}

void MainWindow::goToImage(int index)
{
    if (index < 0 || index >= m_currentImagePaths.size()) {
        return;
    }
    m_frameLoader->stopGif();
    const int previousIndex = m_currentImageIndex;
    const int previousPosition = previousIndex >= 0 ? playbackPositionOf(previousIndex) : -1;
    m_currentImageIndex = index;
    updateSequence();
    if (index != previousIndex && previousIndex >= 0) {
        // Within a sequence, the direction follows frame order rather than folder order.
        const int position = playbackPositionOf(index);
        const int before = playbackPositionOf(previousIndex);
        m_stepDirection = (position >= 0 && before >= 0 && before == previousPosition)
                              ? (position > before ? 1 : -1)
                              : (index > previousIndex ? 1 : -1);
    }
    updateImageNavigation();
    updatePlayerBar();
    const QString path = m_currentImagePaths.at(index);
    if (m_displayedIndex >= 0 && !m_frameLoader->isCached(path)) {
        // Show at once which frame is coming, even if the picture takes a moment.
        m_imageViewport->setOverlayText(QStringLiteral("%1  …").arg(frameNumberOf(path).isEmpty()
                                                                       ? QFileInfo(path).fileName()
                                                                       : frameNumberOf(path)));
    }
    m_frameLoader->request(path);
    prefetchAround();
}

void MainWindow::stepImage(int delta, bool wrap)
{
    const int count = m_currentImagePaths.size();
    if (count == 0 || m_currentImageIndex < 0) {
        return;
    }
    int index = m_currentImageIndex + delta;
    if (!m_sequence.isEmpty()) {
        // In a sequence, step through its frames in frame order. At either end, step on to the
        // folder's next image outside the sequence.
        const int frames = int(m_sequence.size());
        const int current = m_sequencePositions.value(m_currentImageIndex);
        int position = current + delta;
        if (wrap) {
            position = ((position % frames) + frames) % frames;
        }
        if (position >= 0 && position < frames) {
            index = m_sequence.at(position);
        } else if (current != (delta > 0 ? frames - 1 : 0)) {
            index = m_sequence.at(qBound(0, position, frames - 1)); // a big step stops at the end first
        } else {
            const int direction = delta > 0 ? 1 : -1;
            index = m_currentImageIndex + direction;
            while (index >= 0 && index < count && m_sequencePositions.contains(index)) {
                index += direction;
            }
            if (index < 0 || index >= count) {
                return;
            }
        }
    } else if (wrap) {
        index = ((index % count) + count) % count;
    } else {
        index = qBound(0, index, count - 1);
    }
    if (index != m_currentImageIndex) {
        m_freshOpen = false;
        goToImage(index);
    }
}

void MainWindow::prefetchAround()
{
    QStringList paths;
    const int count = playbackCount();
    const int current = playbackPositionOf(m_currentImageIndex);
    if (count == 0 || current < 0) {
        return;
    }
    const bool playing = m_playTimer.isActive();
    const int ahead = playing ? PrefetchAhead * 2 : PrefetchAhead;
    for (int i = 1; i <= ahead; ++i) {
        int position = current + i * m_stepDirection;
        if (playing && m_playLoop) {
            position = ((position % count) + count) % count;
        }
        if (position >= 0 && position < count && position != current) {
            paths.append(m_currentImagePaths.at(indexAtPlaybackPosition(position)));
        }
    }
    const int behind = current - m_stepDirection;
    if (behind >= 0 && behind < count) {
        paths.append(m_currentImagePaths.at(indexAtPlaybackPosition(behind)));
    }
    m_frameLoader->prefetch(paths);
}

void MainWindow::showFrame(const FrameLoader::Frame &frame, bool requested)
{
    if (!isViewerVisible()) {
        return;
    }
    const int index = m_currentImagePaths.indexOf(frame.path);
    if (index < 0) {
        return;
    }
    if (!requested) {
        // While scrubbing, show frames that arrive on the way to the wanted one.
        const int position = playbackPositionOf(index);
        const int shown = m_displayedIndex >= 0 ? playbackPositionOf(m_displayedIndex) : -1;
        const int wanted = playbackPositionOf(m_currentImageIndex);
        const bool between = position >= 0 && shown >= 0 && wanted >= 0
            && ((m_stepDirection > 0 && position > shown && position < wanted)
                || (m_stepDirection < 0 && position < shown && position > wanted));
        if (!between || !frame.error.isEmpty()) {
            return;
        }
    }

    if (!frame.error.isEmpty()) {
        if (m_playTimer.isActive()) {
            stopPlayback();
            statusBar()->showMessage(QStringLiteral("Stopped: %1").arg(frame.error), 8000);
        } else {
            QMessageBox::warning(this, QStringLiteral("Could Not Open Image"),
                                 QStringLiteral("The image could not be opened:\n%1\n\n%2")
                                     .arg(QDir::toNativeSeparators(frame.path), frame.error));
        }
        return;
    }

    m_currentSourceImage = frame.source;
    m_currentDisplayImage = frame.display;
    m_currentImagePath = frame.path;
    m_displayedIndex = index;
    if (m_playTimer.isActive()) {
        // Measure the rate actually reached over about the last second.
        const qint64 now = m_playClock.nsecsElapsed();
        m_playShownNs.append(now);
        while (m_playShownNs.size() > 2 && now - m_playShownNs.first() > 1000000000LL) {
            m_playShownNs.removeFirst();
        }
        if (m_playShownNs.size() >= 3 && now - m_measuredShownNs > 250000000LL) {
            m_measuredShownNs = now;
            const double seconds = double(now - m_playShownNs.first()) / 1e9;
            m_playerBar->setMeasuredFps(double(m_playShownNs.size() - 1) / seconds);
        }
    }
    scheduleMetadata();
    m_imageViewport->setImage(frame.display, !m_freshOpen);
    m_freshOpen = false;

    const QString number = frameNumberOf(frame.path);
    m_imageViewport->setOverlayText(number.isEmpty() ? QString() : number);
    if (m_compareMode == CompareNoView || (m_compareMode == ComparePinned && !m_pinnedSource.isNull())) {
        updateCompare();
    }
    updateColorButton(m_view);
    updateImageInfo();
    updateScopes();
    updatePixelReadout(m_hoveredPixel);
    if (requested && FrameLoader::mayBeAnimated(frame.path)) {
        m_frameLoader->startGif(frame.path);
    }
}

void MainWindow::showGifFrame(const QImage &source, const QImage &display)
{
    m_currentSourceImage = source;
    m_currentDisplayImage = display;
    m_imageViewport->setImage(display, true);
    if (m_compareMode == CompareNoView) {
        updateCompare();
    }
    updateScopes();
    updatePixelReadout(m_hoveredPixel);
}

void MainWindow::togglePlayback()
{
    if (m_playTimer.isActive()) {
        stopPlayback();
        return;
    }
    if (!isViewerVisible() || playbackCount() < 2 || m_currentImageIndex < 0) {
        return;
    }
    // Without looping, playing from the last frame starts again from the first.
    if (!m_playLoop && playbackPositionOf(m_currentImageIndex) >= playbackCount() - 1) {
        m_freshOpen = false;
        goToPlaybackPosition(0);
    }
    m_stepDirection = 1;
    m_playShownNs.clear();
    m_measuredShownNs = 0;
    m_playClock.start();
    restartPlaybackClock();
    m_playTimer.start();
    m_playerBar->setPlaying(true);
    for (OverlayButton *button : {m_closeImageButton, m_previousImageButton, m_nextImageButton}) {
        button->conceal(); // keep the picture clear while playing
    }
    prefetchAround();
    statusBar()->showMessage(QStringLiteral("Playing %1 at %2 fps (Space to stop)")
                                 .arg(m_sequence.isEmpty() ? QStringLiteral("the folder") : QStringLiteral("the sequence"),
                                      PlayerBar::fpsText(m_playbackFps)),
                             3000);
}

void MainWindow::stopPlayback()
{
    if (m_playTimer.isActive()) {
        m_playTimer.stop();
        m_playerBar->setPlaying(false);
        statusBar()->showMessage(QStringLiteral("Stopped"), 1500);
    }
}

// Starts timing playback afresh from the frame wanted now, e.g. after a change of rate.
void MainWindow::restartPlaybackClock()
{
    if (!m_playClock.isValid()) {
        return;
    }
    const qint64 now = m_playClock.nsecsElapsed();
    m_playAnchorNs = now;
    m_playAnchorPosition = qMax(0, playbackPositionOf(m_currentImageIndex));
    m_nextFrameDueNs = now + qint64(1e9 / m_playbackFps);
}

void MainWindow::setPlaybackFps(double fps)
{
    if (!(fps > 0)) {
        return;
    }
    m_playbackFps = fps;
    m_playerBar->setFps(fps);
    QSettings().setValue(SettingsPlaybackFps, fps);
    m_playShownNs.clear();
    restartPlaybackClock();
    if (m_playTimer.isActive()) {
        statusBar()->showMessage(QStringLiteral("Playing at %1 fps").arg(PlayerBar::fpsText(fps)), 2000);
    }
}

// Runs every few milliseconds while playing and moves on when the next frame is due. Normally
// every frame is shown: when one isn't loaded yet, playback waits for it (and so runs slower than
// the chosen rate). In real-time mode the frame is picked from the clock instead, so frames that
// don't load in time are skipped and the clip keeps its length.
void MainWindow::playbackTick()
{
    const int count = playbackCount();
    if (!isViewerVisible() || count < 2 || m_currentImageIndex < 0) {
        stopPlayback();
        return;
    }
    const qint64 now = m_playClock.nsecsElapsed();
    const int current = playbackPositionOf(m_currentImageIndex);

    if (m_playRealTime) {
        const qint64 elapsedFrames = qint64(double(now - m_playAnchorNs) * m_playbackFps / 1e9);
        qint64 target = m_playAnchorPosition + elapsedFrames;
        if (target >= count) {
            if (!m_playLoop) {
                if (current != count - 1) {
                    goToPlaybackPosition(count - 1);
                }
                stopPlayback();
                return;
            }
            target %= count;
        }
        if (int(target) != current) {
            m_stepDirection = 1;
            m_freshOpen = false;
            goToPlaybackPosition(int(target));
        }
        return;
    }

    if (m_displayedIndex != m_currentImageIndex || now < m_nextFrameDueNs) {
        return; // the current frame hasn't arrived yet, or it isn't time for the next one
    }
    int next = current + 1;
    if (next >= count) {
        if (!m_playLoop) {
            stopPlayback();
            return;
        }
        next = 0;
    }
    if (m_frameLoader->isCached(m_currentImagePaths.at(indexAtPlaybackPosition(next)))) {
        const qint64 period = qint64(1e9 / m_playbackFps);
        m_nextFrameDueNs += period;
        if (m_nextFrameDueNs < now - period) {
            m_nextFrameDueNs = now + period; // fell behind: don't rush to catch up
        }
        m_stepDirection = 1;
        m_freshOpen = false;
        goToPlaybackPosition(next);
    } else {
        prefetchAround();
    }
}

void MainWindow::updateSequence()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_currentImagePaths.size()) {
        m_sequence.clear();
        m_sequencePositions.clear();
        return;
    }
    if (m_sequencePositions.contains(m_currentImageIndex)) {
        return; // still in the same sequence
    }
    m_sequence = Sequence::framesOf(m_currentImagePaths, m_currentImageIndex);
    m_sequencePositions.clear();
    m_sequencePositions.reserve(m_sequence.size());
    for (int i = 0; i < m_sequence.size(); ++i) {
        m_sequencePositions.insert(m_sequence.at(i), i);
    }
}

int MainWindow::playbackCount() const
{
    return m_sequence.isEmpty() ? int(m_currentImagePaths.size()) : int(m_sequence.size());
}

int MainWindow::playbackPositionOf(int index) const
{
    if (m_sequence.isEmpty()) {
        return index >= 0 && index < m_currentImagePaths.size() ? index : -1;
    }
    return m_sequencePositions.value(index, -1);
}

int MainWindow::indexAtPlaybackPosition(int position) const
{
    return m_sequence.isEmpty() ? position : m_sequence.at(position);
}

void MainWindow::goToPlaybackPosition(int position)
{
    const int count = playbackCount();
    if (count == 0) {
        return;
    }
    m_freshOpen = false;
    goToImage(indexAtPlaybackPosition(qBound(0, position, count - 1)));
}

void MainWindow::updatePlayerBar()
{
    if (!m_playerBar || !m_playerBarAction) {
        return;
    }
    const bool show = m_playerBarAction->isChecked() && m_sequence.size() >= 2 && m_currentImageIndex >= 0;
    if (show) {
        const QString name = Sequence::displayName(m_currentImagePaths, m_sequence);
        const QString shownKey = name + QLatin1Char('|') + QString::number(m_sequence.size());
        if (m_playerBar->property("sequenceName").toString() != shownKey) {
            m_playerBar->setProperty("sequenceName", shownKey);
            m_playerBar->setSequence(name, int(m_sequence.size()));
        }
        const QString path = m_currentImagePaths.at(m_currentImageIndex);
        m_playerBar->setPosition(m_sequencePositions.value(m_currentImageIndex),
                                 Sequence::split(QFileInfo(path).fileName()).frame);
    } else {
        m_playerBar->setProperty("sequenceName", QString());
    }
    if (m_playerBar->isVisible() != show) {
        m_playerBar->setVisible(show);
    }
}

bool MainWindow::handleViewerKey(QKeyEvent *event)
{
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (!plain) {
        return false;
    }
    if (event->key() == Qt::Key_V && !shift) {
        if (!event->isAutoRepeat()) {
            m_splitAction->toggle();
        }
        return true;
    }
    if (event->key() == Qt::Key_Tab && m_splitAction->isChecked()) {
        setSplitPaneActive(!isSplitPaneActive());
        return true;
    }
    if (isSplitPaneActive()) {
        if (m_splitPane->handleKey(event)) {
            updateImageInfo();
            return true;
        }
        // The pane's grid gets its own keys (arrows, typing to find a file).
        if (m_splitPane->isBrowsing()) {
            return false;
        }
    }
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Left: {
        stopPlayback();
        const int step = (shift ? 10 : 1) * (event->key() == Qt::Key_Right ? 1 : -1);
        stepImage(step);
        return true;
    }
    case Qt::Key_Home:
        stopPlayback();
        goToPlaybackPosition(0); // the sequence's first frame, or the folder's first image
        return true;
    case Qt::Key_End:
        stopPlayback();
        goToPlaybackPosition(playbackCount() - 1);
        return true;
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            togglePlayback();
        }
        return true;
    case Qt::Key_Escape:
        if (m_playTimer.isActive()) {
            stopPlayback();
        } else if (m_fullScreenAction->isChecked()) {
            m_fullScreenAction->setChecked(false);
        } else {
            showGrid();
        }
        return true;
    case Qt::Key_W:
        if (!event->isAutoRepeat()) {
            setCompareMode(m_compareMode == CompareOff ? m_lastCompareMode : CompareOff);
        }
        return true;
    case Qt::Key_P:
        if (!event->isAutoRepeat()) {
            pinCurrentImage();
        }
        return true;
    case Qt::Key_F:
        activeViewport()->fitToWindow();
        syncSplitViews(activeViewport());
        return true;
    case Qt::Key_1:
        activeViewport()->zoomActualSize();
        syncSplitViews(activeViewport());
        return true;
    case Qt::Key_R:
        setChannel(int(ImageViewport::Channel::Red), true);
        return true;
    case Qt::Key_G:
        setChannel(int(ImageViewport::Channel::Green), true);
        return true;
    case Qt::Key_B:
        setChannel(int(ImageViewport::Channel::Blue), true);
        return true;
    case Qt::Key_A:
        setChannel(int(ImageViewport::Channel::Alpha), true);
        return true;
    case Qt::Key_L:
        setChannel(int(ImageViewport::Channel::Luma), true);
        return true;
    case Qt::Key_C:
        if (!event->isAutoRepeat()) {
            m_clippingAction->toggle();
        }
        return true;
    case Qt::Key_I:
        if (!event->isAutoRepeat()) {
            m_metadataAction->trigger();
        }
        return true;
    case Qt::Key_S:
        if (!event->isAutoRepeat()) {
            m_scopesAction->trigger();
        }
        return true;
    default:
        return false;
    }
}

// Re-renders the shown image after a view or LUT change, keeping zoom and position.
void MainWindow::refreshCurrentDisplay()
{
    m_frameLoader->setRender(m_view, m_input);
    m_splitPane->setRender(m_view, m_input);
    if (!m_currentSourceImage.isNull()) {
        m_currentDisplayImage = ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input);
        m_imageViewport->setImage(m_currentDisplayImage, true);
        updateCompare();
        updateImageInfo();
        updateScopes();
        updatePixelReadout(m_hoveredPixel);
    }
    updateColorButton(m_view);
}

// Hands the viewed image to the scopes panel (which only computes while it is shown).
void MainWindow::updateScopes()
{
    // In the split viewer the scopes show the active pane's image.
    const bool split = isSplitPaneActive();
    const QImage &source = split ? m_splitPane->sourceImage() : m_currentSourceImage;
    const QImage &display = split ? m_splitPane->displayImage() : m_currentDisplayImage;
    if (!isViewerVisible() || source.isNull() || (split && m_splitPane->isBrowsing())) {
        m_scopesPanel->clear();
        return;
    }
    const ColorTransform::View shown = ColorTransform::resolveView(m_view, source);
    QString viewName = ColorTransform::displayName(shown);
    if (shown == ColorTransform::View::CustomLut && ColorTransform::hasLut()) {
        viewName = QFileInfo(ColorTransform::lutFilePath()).fileName();
    }
    m_scopesPanel->setImages(source, display, m_input, viewName);
}

void MainWindow::setCompareMode(CompareMode mode)
{
    if (mode == ComparePinned && m_pinnedSource.isNull()) {
        pinCurrentImage();
        return;
    }
    m_compareMode = mode;
    if (mode != CompareOff) {
        m_lastCompareMode = mode;
    }
    for (QAction *action : m_compareGroup->actions()) {
        action->setChecked(action->data().toInt() == int(mode));
    }
    updateCompare();
}

void MainWindow::pinCurrentImage()
{
    if (m_currentSourceImage.isNull()) {
        statusBar()->showMessage(QStringLiteral("Open an image first, then pin it (P)"), 4000);
        return;
    }
    m_pinnedSource = m_currentSourceImage;
    m_pinnedPath = m_currentImagePath;
    m_pinnedDisplay = QImage();
    statusBar()->showMessage(QStringLiteral("Pinned %1 for compare: go to another image to wipe between them")
                                 .arg(QFileInfo(m_pinnedPath).fileName()), 5000);
    setCompareMode(ComparePinned);
}

void MainWindow::updateCompare()
{
    if (m_compareMode == CompareOff || m_currentSourceImage.isNull()) {
        m_imageViewport->setWipeEnabled(false);
        return;
    }
    const ColorTransform::View shown = ColorTransform::resolveView(m_view, m_currentSourceImage);
    if (m_compareMode == CompareNoView) {
        m_imageViewport->setCompareImage(ColorTransform::toDisplay(m_currentSourceImage, ColorTransform::View::None, m_input),
                                         QStringLiteral("Original (no color view)"),
                                         ColorTransform::displayName(shown));
    } else {
        // The pinned image only needs rendering again when the view changes, not on every frame.
        const QString key = ColorTransform::cacheKey(m_view) + QString::number(int(m_input));
        if (m_pinnedDisplay.isNull() || m_pinnedDisplayKey != key) {
            m_pinnedDisplay = ColorTransform::toDisplay(m_pinnedSource, m_view, m_input);
            m_pinnedDisplayKey = key;
        }
        m_imageViewport->setCompareImage(m_pinnedDisplay,
                                         QStringLiteral("A: %1").arg(QFileInfo(m_pinnedPath).completeBaseName()),
                                         QStringLiteral("B: %1").arg(QFileInfo(m_currentImagePath).completeBaseName()));
    }
    m_imageViewport->setWipeEnabled(true);
}

void MainWindow::updateImageInfo()
{
    if (isViewerVisible() && isSplitPaneActive()) {
        m_infoLabel->setText(m_splitPane->infoText());
        return;
    }
    if (!isViewerVisible() || m_currentSourceImage.isNull()) {
        if (m_currentFolderPath.isEmpty()) {
            m_infoLabel->clear();
            return;
        }
        QString text = !isViewerVisible() && (m_imagesOnly->isChecked() || !m_gridSearch->text().trimmed().isEmpty())
            ? QStringLiteral("%1 of %2 items").arg(m_visibleGridItems).arg(m_fileGrid->count())
            : QStringLiteral("%1 images").arg(m_currentImagePaths.size());
        const QList<QListWidgetItem *> selected = m_fileGrid->selectedItems();
        if (!selected.isEmpty()) {
            qint64 bytes = 0;
            for (const QListWidgetItem *item : selected) {
                bytes += item->data(FileSizeRole).toLongLong();
            }
            text += selected.size() == 1 ? QStringLiteral("   ·   1 item selected")
                                         : QStringLiteral("   ·   %1 items selected").arg(selected.size());
            if (bytes > 0) {
                text += QStringLiteral("  ") + QLocale().formattedDataSize(bytes);
            }
        }
        m_infoLabel->setText(text);
        return;
    }
    QStringList parts;
    parts.append(QFileInfo(m_currentImagePath).fileName());
    parts.append(QStringLiteral("%1 × %2").arg(m_currentSourceImage.width()).arg(m_currentSourceImage.height()));
    const QString source = ColorTransform::describeSource(m_currentSourceImage, m_input);
    if (!source.isEmpty()) {
        parts.append(source);
    }
    parts.append(QStringLiteral("%1 / %2").arg(m_currentImageIndex + 1).arg(m_currentImagePaths.size()));
    m_infoLabel->setText(parts.join(QStringLiteral("   ·   ")));
}

void MainWindow::updatePixelReadout(const QPoint &pixel)
{
    m_hoveredPixel = pixel;
    if (!isSplitPaneActive()) {
        m_scopesPanel->setHoveredPixel(isViewerVisible() ? pixel : QPoint(-1, -1));
    }
    showPixelReadout(pixel, m_currentSourceImage, m_currentDisplayImage);
}

void MainWindow::showPixelReadout(const QPoint &pixel, const QImage &source, const QImage &display)
{
    if (!isViewerVisible() || pixel.x() < 0 || source.isNull() || !display.rect().contains(pixel)) {
        m_pixelLabel->clear();
        m_swatchLabel->hide();
        return;
    }
    const QRgb shown = display.pixel(pixel);
    m_swatchLabel->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;")
                                     .arg(QColor(shown).name()));
    m_swatchLabel->show();
    m_pixelLabel->setText(QStringLiteral("x %1  y %2   stored %3   display %4 %5 %6")
                              .arg(pixel.x(), 4)
                              .arg(pixel.y(), 4)
                              .arg(storedPixelText(source, pixel))
                              .arg(qRed(shown), 3)
                              .arg(qGreen(shown), 3)
                              .arg(qBlue(shown), 3));
}

// Right-click in the viewer: pick the color under the mouse (as shown, with the color view) in
// the formats other programs take, plus the usual image actions.
void MainWindow::showViewerContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    const QPoint pixel = m_imageViewport->imagePixelAt(QPointF(pos));
    if (pixel.x() >= 0 && m_currentDisplayImage.rect().contains(pixel)) {
        const QColor color(m_currentDisplayImage.pixel(pixel));
        QPixmap swatch(16, 16);
        swatch.fill(color);
        const QString hex = color.name(QColor::HexRgb).toUpper();
        QAction *pick = menu.addAction(QIcon(swatch), QStringLiteral("Pick Color\t%1").arg(hex));
        pick->setToolTip(QStringLiteral("Copy the color as hex, for Photoshop, Resolve, web colors and so on"));
        connect(pick, &QAction::triggered, this, [this, hex, color]() {
            copyColorText(hex, color);
        });

        QMenu *formats = menu.addMenu(QStringLiteral("Copy Color As"));
        const QString rgb255 = QStringLiteral("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
        const QString rgb01 = QStringLiteral("%1, %2, %3")
                                  .arg(color.redF(), 0, 'f', 4)
                                  .arg(color.greenF(), 0, 'f', 4)
                                  .arg(color.blueF(), 0, 'f', 4);
        const QString stored = storedPixelText(m_currentSourceImage, pixel).simplified();
        const struct {
            QString label;
            QString text;
        } entries[] = {
            {QStringLiteral("Hex"), hex},
            {QStringLiteral("Hex without #"), hex.mid(1)},
            {QStringLiteral("RGB 0–255"), rgb255},
            {QStringLiteral("RGB 0–1"), rgb01},
            {QStringLiteral("Stored value"), stored},
        };
        for (const auto &entry : entries) {
            if (entry.text.isEmpty()) {
                continue;
            }
            const QString text = entry.text;
            formats->addAction(QStringLiteral("%1\t%2").arg(entry.label, text), this, [this, text, color]() {
                copyColorText(text, color);
            });
        }
        menu.addSeparator();
    }
    menu.addAction(m_exportAction);
    menu.addAction(m_copyImageAction);
    menu.addAction(m_copyPathAction);
    menu.addAction(m_showInExplorerAction);
    menu.addSeparator();
    menu.addAction(m_fitToWindowAction);
    menu.addAction(m_actualSizeAction);
    menu.addAction(m_pinAction);
    menu.addAction(m_metadataAction);
    menu.exec(m_imageViewport->mapToGlobal(pos));
}

void MainWindow::copyColorText(const QString &text, const QColor &color)
{
    // Plain text for most programs, plus the color itself for those that take it (e.g. Qt apps).
    auto *mime = new QMimeData;
    mime->setText(text);
    mime->setColorData(color);
    QApplication::clipboard()->setMimeData(mime);
    statusBar()->showMessage(QStringLiteral("Copied %1").arg(text), 3000);
}

void MainWindow::handlePathActivated(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        populateFileGrid(info.absoluteFilePath());
        return;
    }
    if (info.isFile() && isImageFile(info.absoluteFilePath())) {
        if (QDir::cleanPath(info.absolutePath()) != QDir::cleanPath(m_currentFolderPath)) {
            populateFileGrid(info.absolutePath());
        }
        openImagePath(info.absoluteFilePath());
        return;
    }
    statusBar()->showMessage(QStringLiteral("Not found: %1").arg(QDir::toNativeSeparators(path)), 5000);
    m_pathBar->setPath(m_currentFolderPath);
}

void MainWindow::openPath(const QString &path)
{
    handlePathActivated(path);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            event->acceptProposedAction();
            activateWindow();
            openPath(url.toLocalFile());
            return;
        }
    }
    QMainWindow::dropEvent(event);
}

void MainWindow::setChannel(int channel, bool toggle)
{
    auto wanted = static_cast<ImageViewport::Channel>(channel);
    if (toggle && m_imageViewport->channel() == wanted) {
        wanted = ImageViewport::Channel::Rgb;
    }
    m_imageViewport->setChannel(wanted);
    m_splitPane->viewport()->setChannel(wanted);
    for (QAction *action : m_channelGroup->actions()) {
        action->setChecked(action->data().toInt() == int(wanted));
    }
}

void MainWindow::setFullScreenMode(bool on)
{
    if (on == isFullScreen()) {
        return;
    }
    if (on) {
        m_wasMaximized = isMaximized();
        m_hiddenForFullScreen.clear();
        QList<QWidget *> chrome = {menuBar(), statusBar(), m_pathBar, m_folderTreePanel, m_metadataDock, m_scopesDock};
        for (QToolBar *toolbar : findChildren<QToolBar *>()) {
            chrome.append(toolbar);
        }
        for (QWidget *widget : chrome) {
            if (widget && widget->isVisible()) {
                widget->hide();
                m_hiddenForFullScreen.append(widget);
            }
        }
        showFullScreen();
    } else {
        for (QWidget *widget : std::as_const(m_hiddenForFullScreen)) {
            widget->show();
        }
        m_hiddenForFullScreen.clear();
        if (m_wasMaximized) {
            showMaximized();
        } else {
            showNormal();
        }
    }
    if (isViewerVisible()) {
        m_imageViewport->setFocus();
    }
}

QString MainWindow::currentPath() const
{
    if (isViewerVisible() && isSplitPaneActive()) {
        return m_splitPane->selectedPath();
    }
    if (isViewerVisible() && !m_currentImagePath.isEmpty()) {
        return m_currentImagePath;
    }
    if (const QListWidgetItem *item = m_fileGrid->currentItem(); item && item->isSelected()) {
        return item->data(FilePathRole).toString();
    }
    return m_currentFolderPath;
}

void MainWindow::scheduleMetadata()
{
    if (m_metadataDock && m_metadataDock->isVisible()) {
        m_metadataTimer.start();
    }
}

// Reads the header on a background thread (it may be on a slow network share).
void MainWindow::loadMetadata()
{
    const QString path = currentPath();
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        ++m_metadataGeneration;
        m_metadataPanel->showMessage(QStringLiteral("Select an image to see its metadata."));
        return;
    }
    if (path == m_metadataPanel->filePath()) {
        return;
    }
    const int generation = ++m_metadataGeneration;
    m_metadataPool.clear(); // only the newest request matters
    m_metadataPool.start([this, path, generation]() {
        const QList<Metadata::Entry> entries = Metadata::read(path);
        QMetaObject::invokeMethod(this, [this, path, generation, entries]() {
            if (generation == m_metadataGeneration) {
                m_metadataPanel->showEntries(path, entries);
            }
        }, Qt::QueuedConnection);
    });
}

// Saves the picture as it is shown (color view or LUT applied, 8 bits per channel) in a common
// format. Channel isolation and the clipping overlay are viewing aids and are not exported.
void MainWindow::exportImageAs()
{
    if (!isViewerVisible() || m_currentDisplayImage.isNull()) {
        statusBar()->showMessage(QStringLiteral("Open an image first, then export it"), 4000);
        return;
    }
    const QImage display = m_currentDisplayImage;

    const QList<ExportFormat> formats = exportFormats();
    QStringList filters;
    for (const ExportFormat &format : formats) {
        filters << QStringLiteral("%1 (*.%2)").arg(format.name, format.suffix);
    }

    // Name: the image's name, plus what was burned in ("_LogC4-Rec709", or the LUT's name).
    QSettings settings;
    const ColorTransform::View shown = ColorTransform::resolveView(m_view, m_currentSourceImage);
    QString tag;
    switch (shown) {
    case ColorTransform::View::LogC3ToRec709:
        tag = QStringLiteral("_LogC3-Rec709");
        break;
    case ColorTransform::View::LogC4ToRec709:
        tag = QStringLiteral("_LogC4-Rec709");
        break;
    case ColorTransform::View::CustomLut:
        tag = QLatin1Char('_') + QFileInfo(ColorTransform::lutFilePath()).completeBaseName();
        break;
    default:
        break;
    }
    const QFileInfo source(m_currentImagePath);
    QString folder = settings.value(SettingsExportFolder).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        folder = source.absolutePath();
    }
    const QString lastFormat = settings.value(SettingsExportFormat, QStringLiteral("png")).toString();
    QString selectedFilter = filters.value(0);
    QString suffix = formats.value(0).suffix;
    for (int i = 0; i < formats.size(); ++i) {
        if (formats.at(i).suffix == lastFormat) {
            selectedFilter = filters.at(i);
            suffix = lastFormat;
        }
    }
    const QString suggested = QDir(folder).filePath(source.completeBaseName() + tag + QLatin1Char('.') + suffix);

    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export As"), suggested,
                                                filters.join(QStringLiteral(";;")), &selectedFilter);
    if (path.isEmpty()) {
        return;
    }

    // The format follows the chosen filter; add its extension if the name has none of ours.
    const ExportFormat *format = &formats.first();
    for (int i = 0; i < formats.size(); ++i) {
        if (filters.at(i) == selectedFilter) {
            format = &formats.at(i);
        }
    }
    const QString typedSuffix = QFileInfo(path).suffix().toLower();
    for (const ExportFormat &candidate : formats) {
        if (typedSuffix == candidate.suffix || (candidate.suffix == QStringLiteral("jpg") && typedSuffix == QStringLiteral("jpeg"))
            || (candidate.suffix == QStringLiteral("tif") && typedSuffix == QStringLiteral("tiff"))) {
            format = &candidate;
        }
    }
    if (typedSuffix.isEmpty() || QFileInfo(path).suffix().compare(format->suffix, Qt::CaseInsensitive) != 0) {
        const bool known = std::any_of(formats.cbegin(), formats.cend(), [&](const ExportFormat &f) {
            return typedSuffix == f.suffix || typedSuffix == QStringLiteral("jpeg") || typedSuffix == QStringLiteral("tiff");
        });
        if (!known) {
            path += QLatin1Char('.') + format->suffix;
        }
    }

    QImageWriter writer(path, format->writer);
    if (!writeExportImage(writer, *format, display, 95)) {
        QMessageBox::warning(this, QStringLiteral("Export As"),
                             QStringLiteral("Could not save %1:\n%2").arg(QDir::toNativeSeparators(path), writer.errorString()));
        return;
    }
    settings.setValue(SettingsExportFolder, QFileInfo(path).absolutePath());
    settings.setValue(SettingsExportFormat, format->suffix);
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::batchConvert(const QStringList &paths)
{
    if (m_batchRunning || isViewerVisible()) {
        return;
    }
    QStringList sources;
    for (const QString &path : paths) {
        if (isImageFile(path) && QFileInfo(path).isFile()) {
            sources.append(path);
        }
    }
    if (sources.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select images in the file grid to convert."), 4000);
        return;
    }
    const QList<ExportFormat> formats = exportFormats();
    if (formats.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("No export formats are available."), 4000);
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Batch Convert"));
    auto *form = new QFormLayout(&dialog);
    form->addRow(new QLabel(QStringLiteral("Convert %1 selected image(s)").arg(sources.size()), &dialog));
    auto *formatBox = new QComboBox(&dialog);
    for (const ExportFormat &format : formats) {
        formatBox->addItem(format.name);
    }
    form->addRow(QStringLiteral("Format"), formatBox);
    auto *quality = new QSpinBox(&dialog);
    quality->setRange(1, 100);
    quality->setValue(95);
    form->addRow(QStringLiteral("Quality"), quality);
    auto *scalingBox = new QComboBox(&dialog);
    scalingBox->addItem(QStringLiteral("Original size"), int(BatchScaling::Original));
    scalingBox->addItem(QStringLiteral("Center crop with no resizing"), int(BatchScaling::CenterCrop));
    scalingBox->addItem(QStringLiteral("Scale full frame with crop"), int(BatchScaling::Fill));
    scalingBox->addItem(QStringLiteral("Scale entire image to fit"), int(BatchScaling::Fit));
    scalingBox->addItem(QStringLiteral("Stretch frame to all corners"), int(BatchScaling::Stretch));
    form->addRow(QStringLiteral("Scaling"), scalingBox);
    auto *width = new QSpinBox(&dialog);
    auto *height = new QSpinBox(&dialog);
    for (auto *dimension : {width, height}) {
        dimension->setRange(1, 65535);
        dimension->setSuffix(QStringLiteral(" px"));
        dimension->setValue(1920);
        dimension->setEnabled(false);
        connect(scalingBox, &QComboBox::currentIndexChanged, dimension, [scalingBox, dimension]() {
            dimension->setEnabled(scalingBox->currentData().toInt() != int(BatchScaling::Original));
        });
    }
    height->setValue(1080);
    form->addRow(QStringLiteral("Output width"), width);
    form->addRow(QStringLiteral("Output height"), height);
    auto *folder = new QLineEdit(QDir::toNativeSeparators(m_currentFolderPath), &dialog);
    folder->setReadOnly(true);
    auto *browse = new QPushButton(QStringLiteral("Browse…"), &dialog);
    auto *destination = new QHBoxLayout;
    destination->addWidget(folder, 1);
    destination->addWidget(browse);
    form->addRow(QStringLiteral("Output folder"), destination);
    connect(browse, &QPushButton::clicked, &dialog, [&]() {
        const QString selected = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Output Folder"), folder->text());
        if (!selected.isEmpty()) {
            folder->setText(QDir::toNativeSeparators(selected));
        }
    });
    const auto updateQuality = [&]() {
        form->setRowVisible(quality, formats.at(formatBox->currentIndex()).hasQuality());
    };
    connect(formatBox, &QComboBox::currentIndexChanged, &dialog, updateQuality);
    updateQuality();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Convert"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const ExportFormat format = formats.at(formatBox->currentIndex());
    const QSize outputSize(width->value(), height->value());
    const auto scaling = static_cast<BatchScaling>(scalingBox->currentData().toInt());
    const QString target = QDir::fromNativeSeparators(folder->text());
    const int qualityValue = quality->value();
    auto *progress = new QProgressDialog(QStringLiteral("Converting images…"), QStringLiteral("Cancel"),
                                         0, int(sources.size()), this);
    progress->setWindowTitle(QStringLiteral("Batch Convert"));
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    connect(progress, &QProgressDialog::canceled, this, [this]() { m_batchCanceled = true; });
    progress->setValue(0);
    m_batchCanceled = false;
    m_batchRunning = true;
    updateEditActions();
    const auto loader = m_imageLoader;
    const auto input = m_input;
    m_batchPool.start([this, progress, sources, format, outputSize, scaling, target, qualityValue, loader, input]() {
        QStringList errors;
        int converted = 0;
        int attempted = 0;
        const QDir output(target);
        for (const QString &source : sources) {
            if (m_batchCanceled) {
                break;
            }
            const QFileInfo info(source);
            const auto result = loader->load(source);
            QString error = result.error;
            if (result.isValid()) {
                const QImage image = scaleBatchImage(
                    ColorTransform::toDisplay(result.image, ColorTransform::View::None, input), outputSize, scaling);
                QTemporaryFile temporary(output.filePath(QStringLiteral(".onion-convert-XXXXXX")));
                if (temporary.open()) {
                    QImageWriter writer(&temporary, format.writer);
                    if (writeExportImage(writer, format, image, qualityValue) && temporary.flush()) {
                        const QString base = info.completeBaseName();
                        const QString name = base + QLatin1Char('.') + format.suffix;
                        QString path = output.filePath(name);
                        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
                            path = output.filePath(FileOps::keepBothName(target, name, false));
                        }
                        // Rename only after encoding, and never replace an existing file.
                        temporary.close();
                        if (!m_batchCanceled && temporary.rename(path)) {
                            temporary.setAutoRemove(false);
                            ++converted;
                        } else if (!m_batchCanceled) {
                            error = temporary.errorString();
                        }
                    } else {
                        error = writer.error() == QImageWriter::UnknownError ? temporary.errorString() : writer.errorString();
                    }
                } else {
                    error = temporary.errorString();
                }
            } else if (error.isEmpty()) {
                error = QStringLiteral("Could not read the image.");
            }
            if (!error.isEmpty()) {
                errors.append(QStringLiteral("%1 — %2").arg(info.fileName(), error));
            }
            ++attempted;
            QMetaObject::invokeMethod(this, [this, progress, attempted]() {
                if (!m_batchCanceled) {
                    progress->setValue(attempted);
                }
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, progress, converted, attempted, total = sources.size(), errors, target]() {
            progress->deleteLater();
            m_batchRunning = false;
            if (converted > 0 && !isViewerVisible() && QDir(m_currentFolderPath) == QDir(target)) {
                populateFileGrid(m_currentFolderPath, false);
            }
            updateEditActions();
            const QString summary = QStringLiteral("Converted %1 of %2 images.").arg(converted).arg(total);
            if (!errors.isEmpty()) {
                QMessageBox message(QMessageBox::Warning, QStringLiteral("Batch Convert"), summary, QMessageBox::Ok, this);
                message.setDetailedText(errors.join(QLatin1Char('\n')));
                message.exec();
            } else {
                statusBar()->showMessage(attempted < total ? QStringLiteral("Canceled. %1").arg(summary) : summary, 8000);
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::batchRename(const QStringList &paths)
{
    if (m_batchRunning || m_fileJob || isViewerVisible()) {
        return;
    }
    if (paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select files in the file grid to rename."), 4000);
        return;
    }
    QSettings settings;
    BatchRename::Options options;
    options.pattern = settings.value(SettingsRenamePattern, options.pattern).toString();
    options.find = settings.value(SettingsRenameFind).toString();
    options.replace = settings.value(SettingsRenameReplace).toString();
    options.regex = settings.value(SettingsRenameRegex, false).toBool();
    options.matchCase = settings.value(SettingsRenameMatchCase, false).toBool();
    options.nameCase = static_cast<BatchRename::CaseChange>(settings.value(SettingsRenameNameCase, 0).toInt());
    options.extensionCase = static_cast<BatchRename::CaseChange>(settings.value(SettingsRenameExtensionCase, 0).toInt());
    options.counterStart = settings.value(SettingsRenameCounterStart, options.counterStart).toInt();
    options.counterStep = settings.value(SettingsRenameCounterStep, options.counterStep).toInt();
    options.counterPadding = settings.value(SettingsRenameCounterDigits, options.counterPadding).toInt();
    options.removeEnabled = settings.value(SettingsRenameRemove, false).toBool();
    options.removeFrom = settings.value(SettingsRenameRemoveFrom, options.removeFrom).toInt();
    options.removeTo = settings.value(SettingsRenameRemoveTo, options.removeTo).toInt();
    options.removeFromEnd = settings.value(SettingsRenameRemoveFromEnd, false).toBool();

    BatchRenameDialog dialog(paths, options, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    options = dialog.options();
    settings.setValue(SettingsRenamePattern, options.pattern);
    settings.setValue(SettingsRenameFind, options.find);
    settings.setValue(SettingsRenameReplace, options.replace);
    settings.setValue(SettingsRenameRegex, options.regex);
    settings.setValue(SettingsRenameMatchCase, options.matchCase);
    settings.setValue(SettingsRenameNameCase, int(options.nameCase));
    settings.setValue(SettingsRenameExtensionCase, int(options.extensionCase));
    settings.setValue(SettingsRenameCounterStart, options.counterStart);
    settings.setValue(SettingsRenameCounterStep, options.counterStep);
    settings.setValue(SettingsRenameCounterDigits, options.counterPadding);
    settings.setValue(SettingsRenameRemove, options.removeEnabled);
    settings.setValue(SettingsRenameRemoveFrom, options.removeFrom);
    settings.setValue(SettingsRenameRemoveTo, options.removeTo);
    settings.setValue(SettingsRenameRemoveFromEnd, options.removeFromEnd);
    const QList<BatchRename::Move> moves = dialog.moves();
    if (!moves.isEmpty()) {
        runRename(moves, false);
    }
}

// Renames on a background thread (thousands of frames on a network share take a while), then
// refreshes the grid with the renamed files selected. A failed batch is rolled back as a whole.
void MainWindow::runRename(const QList<BatchRename::Move> &moves, bool undo)
{
    const QString title = undo ? QStringLiteral("Undo Rename") : QStringLiteral("Batch Rename");
    auto *progress = new QProgressDialog(undo ? QStringLiteral("Putting the old names back…") : QStringLiteral("Renaming…"),
                                         QString(), 0, 0, this);
    progress->setWindowTitle(title);
    progress->setCancelButton(nullptr);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumDuration(400);
    progress->setValue(0);
    m_batchRunning = true;
    updateEditActions();
    QPointer<QProgressDialog> guard(progress);
    m_batchPool.start([this, moves, undo, title, guard]() {
        const BatchRename::Outcome outcome = BatchRename::apply(moves, [guard](int done, int total) {
            QMetaObject::invokeMethod(qApp, [guard, done, total]() {
                if (guard) {
                    guard->setMaximum(total);
                    guard->setValue(done);
                }
            }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(this, [this, outcome, undo, title, guard]() {
            if (guard) {
                guard->deleteLater();
            }
            m_batchRunning = false;
            if (!outcome.done.isEmpty()) {
                if (undo) {
                    m_renameHistory.removeLast();
                } else {
                    m_renameHistory.append(outcome.done);
                }
            }
            QSet<QString> renamed;
            for (const BatchRename::Move &move : outcome.done) {
                renamed.insert(QDir::cleanPath(move.to).toLower());
            }
            if (!isViewerVisible() && !m_currentFolderPath.isEmpty()) {
                const int scroll = m_fileGrid->verticalScrollBar()->value();
                populateFileGrid(m_currentFolderPath, false);
                m_fileGrid->verticalScrollBar()->setValue(scroll);
                QListWidgetItem *first = nullptr;
                for (int row = 0; row < m_fileGrid->count() && !renamed.isEmpty(); ++row) {
                    QListWidgetItem *item = m_fileGrid->item(row);
                    if (item->isHidden() || !renamed.contains(QDir::cleanPath(item->data(FilePathRole).toString()).toLower())) {
                        continue;
                    }
                    if (!first) {
                        first = item;
                        m_fileGrid->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
                    } else {
                        item->setSelected(true);
                    }
                }
                if (first) {
                    m_fileGrid->scrollToItem(first);
                }
            }
            updateEditActions();
            if (outcome.errors.isEmpty()) {
                const int count = int(outcome.done.size());
                statusBar()->showMessage(undo ? QStringLiteral("Put back the old names of %1 item(s)").arg(count)
                                              : QStringLiteral("Renamed %1 item(s). Ctrl+Z undoes it.").arg(count),
                                         6000);
                return;
            }
            QStringList shown = outcome.errors.mid(0, 15);
            if (outcome.errors.size() > shown.size()) {
                shown.append(QStringLiteral("…and %1 more").arg(outcome.errors.size() - shown.size()));
            }
            QMessageBox::warning(this, title,
                                 (outcome.rolledBack ? QStringLiteral("Nothing was renamed: the names were put back after this problem.\n\n")
                                                     : QString())
                                     + shown.join(QLatin1Char('\n')));
        }, Qt::QueuedConnection);
    });
}

void MainWindow::copyImageToClipboard()
{
    if (!isViewerVisible()) {
        copySelectedFiles(false); // Ctrl+C in the file browser copies the files
        return;
    }
    if (m_currentDisplayImage.isNull()) {
        copyPathToClipboard(currentPath());
        return;
    }
    QApplication::clipboard()->setImage(m_currentDisplayImage);
    statusBar()->showMessage(QStringLiteral("Copied %1 (as shown) to the clipboard")
                                 .arg(QFileInfo(m_currentImagePath).fileName()), 3000);
}

void MainWindow::copyPathToClipboard(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QString native = QDir::toNativeSeparators(path);
    QApplication::clipboard()->setText(native);
    statusBar()->showMessage(QStringLiteral("Copied %1").arg(native), 3000);
}

void MainWindow::showInExplorer(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
#ifdef Q_OS_WIN
    // Opens the folder with the file selected.
    if (info.exists()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())});
        return;
    }
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
}

void MainWindow::addPinAction(QMenu *menu, const QString &folderPath)
{
    if (folderPath.isEmpty()) {
        return;
    }
    if (m_folderTreePanel->isPinned(folderPath)) {
        menu->addAction(Icons::icon(Icons::Kind::Unpin), QStringLiteral("Unpin from Quick access"), this, [this, folderPath]() {
            m_folderTreePanel->unpinFolder(folderPath);
        });
    } else {
        menu->addAction(Icons::icon(Icons::Kind::Pin), QStringLiteral("Pin to Quick access"), this, [this, folderPath]() {
            m_folderTreePanel->pinFolder(folderPath);
        });
    }
}

void MainWindow::showGridContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_fileGrid->itemAt(pos);
    const QString path = item ? item->data(FilePathRole).toString() : m_currentFolderPath;
    if (path.isEmpty()) {
        return;
    }
    // Right-clicking a file that is part of the selection acts on the whole selection.
    QStringList paths;
    if (item) {
        paths = item->isSelected() ? selectedPaths() : QStringList{path};
    }
    updateEditActions();
    QMenu menu(this);
    if (item) {
        if (item->data(IsFolderRole).toBool() || item->data(IsImageRole).toBool()) {
            menu.addAction(QStringLiteral("Open"), this, [this, item]() {
                handleGridItemActivated(item);
            });
        } else {
            menu.addAction(QStringLiteral("Open in Default App"), this, [this, path]() {
                if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
                    QMessageBox::warning(this, QStringLiteral("Could Not Open File"),
                                         QStringLiteral("Windows could not open %1 in its default app.")
                                             .arg(QDir::toNativeSeparators(path)));
                }
            });
        }
        menu.addSeparator();
        if (item->data(IsImageRole).toBool()) {
            auto *convert = menu.addAction(QStringLiteral("Batch Convert…"), this, [this, paths]() {
                batchConvert(paths);
            });
            convert->setEnabled(!m_batchRunning);
        }
        auto *rename = menu.addAction(paths.size() > 1 ? QStringLiteral("Batch Rename…") : QStringLiteral("Rename…"), this, [this, paths]() {
            batchRename(paths);
        });
        rename->setShortcut(m_batchRenameAction->shortcut());
        rename->setEnabled(!m_batchRunning && !m_fileJob);
        menu.addAction(m_cutAction);
        menu.addAction(m_copyFilesAction);
        if (item->data(IsFolderRole).toBool()) {
            QAction *pasteInto = menu.addAction(QStringLiteral("Paste into Folder"), this, [this, path]() {
                pasteFiles(path);
            });
            pasteInto->setEnabled(m_pasteAction->isEnabled());
            addPinAction(&menu, path);
        }
        menu.addSeparator();
    } else {
        QMenu *viewMenu = menu.addMenu(Icons::icon(Icons::Kind::ViewIcons), QStringLiteral("View"));
        viewMenu->addActions(m_gridViewGroup->actions().mid(0, 4));
        viewMenu->addSeparator();
        viewMenu->addActions(m_gridViewGroup->actions().mid(4));
        menu.addSeparator();
        menu.addAction(m_pasteAction);
        menu.addSeparator();
        menu.addAction(m_selectAllAction);
        menu.addAction(m_invertSelectionAction);
        menu.addSeparator();
        addPinAction(&menu, m_currentFolderPath);
        menu.addSeparator();
    }
    menu.addAction(paths.size() > 1 ? QStringLiteral("Copy Paths") : QStringLiteral("Copy Path"), this, [this, path, paths]() {
        copyPathToClipboard(paths.size() > 1 ? paths.join(QLatin1Char('\n')) : path);
    });
    menu.addAction(m_showInExplorerAction->text(), this, [this, path]() {
        showInExplorer(path);
    });
    menu.exec(m_fileGrid->viewport()->mapToGlobal(pos));
}

QItemSelection MainWindow::visibleGridSelection(int first, int last) const
{
    QItemSelection selection;
    int start = first;
    for (int row = first; row <= last; ++row) {
        if (m_fileGrid->item(row)->isHidden()) {
            if (start < row) {
                selection.select(m_fileGrid->model()->index(start, 0), m_fileGrid->model()->index(row - 1, 0));
            }
            start = row + 1;
        }
    }
    if (start <= last) {
        selection.select(m_fileGrid->model()->index(start, 0), m_fileGrid->model()->index(last, 0));
    }
    return selection;
}

// Shift-selection the way Explorer does it: Shift+click and Shift+arrows select the run of files
// from the anchor (the last file clicked or moved to without Shift) to here, in list order, and
// drop the rest of the selection; with Ctrl as well, the run is added to it. Qt on its own keeps
// earlier Ctrl+clicked files and, in the icon views, selects a rectangle instead of a run.
bool MainWindow::handleGridSelectionEvent(QObject *watched, QEvent *event)
{
    QItemSelectionModel *selection = m_fileGrid->selectionModel();
    auto selectRun = [this, selection](const QModelIndex &to, bool add) {
        QModelIndex from = m_selectionAnchor;
        if (!from.isValid() || m_fileGrid->item(from.row())->isHidden()) {
            from = to;
        }
        const QItemSelection run = visibleGridSelection(qMin(from.row(), to.row()), qMax(from.row(), to.row()));
        if (add) {
            selection->select(run, QItemSelectionModel::Select);
        } else {
            selection->select(run, QItemSelectionModel::ClearAndSelect);
        }
    };

    // The view never saw the Shift+press, so keep the drag and release that follow from it too
    // (it would start a box selection from an old position).
    if (m_shiftPressTaken && watched == m_fileGrid->viewport()
        && (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease)) {
        if (event->type() == QEvent::MouseButtonRelease) {
            m_shiftPressTaken = false;
        }
        return true;
    }

    if (watched == m_fileGrid->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton || !mouse->modifiers().testFlag(Qt::ShiftModifier)) {
            return false; // Qt handles it; the anchor follows the current item
        }
        const QModelIndex index = m_fileGrid->indexAt(mouse->position().toPoint());
        if (!index.isValid()) {
            return false;
        }
        m_shiftSelecting = true;
        selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        selectRun(index, mouse->modifiers().testFlag(Qt::ControlModifier));
        m_shiftSelecting = false;
        m_shiftPressTaken = true;
        m_fileGrid->setFocus(Qt::MouseFocusReason);
        return true;
    }

    if (watched == m_fileGrid && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (!key->modifiers().testFlag(Qt::ShiftModifier)) {
            return false;
        }
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            break;
        default:
            return false;
        }
        if (!m_selectionAnchor.isValid()) {
            m_selectionAnchor = m_fileGrid->currentIndex();
        }
        // Let the view move the current item, then put the run in place of Qt's selection.
        m_shiftSelecting = true;
        QCoreApplication::sendEvent(m_fileGrid, event);
        const QModelIndex current = m_fileGrid->currentIndex();
        if (current.isValid()) {
            selectRun(current, key->modifiers().testFlag(Qt::ControlModifier));
        }
        m_shiftSelecting = false;
        return true;
    }
    return false;
}

QStringList MainWindow::selectedPaths() const
{
    // In grid order, not in the order they were clicked.
    QStringList paths;
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        const QListWidgetItem *item = m_fileGrid->item(row);
        if (!item->isHidden() && item->isSelected()) {
            paths.append(item->data(FilePathRole).toString());
        }
    }
    return paths;
}

void MainWindow::copySelectedFiles(bool cut)
{
    if (isViewerVisible()) {
        if (cut) {
            return;
        }
        copyImageToClipboard();
        return;
    }
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select files to %1 first").arg(cut ? QStringLiteral("cut") : QStringLiteral("copy")), 3000);
        return;
    }
    QApplication::clipboard()->setMimeData(FileOps::makeMimeData(paths, cut));
    const QString what = paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QStringLiteral("%1 items").arg(paths.size());
    statusBar()->showMessage(cut ? QStringLiteral("Cut %1: paste to move").arg(what) : QStringLiteral("Copied %1").arg(what), 4000);
    updateEditActions();
}

void MainWindow::updateEditActions()
{
    if (!m_fileGrid || !m_cutAction) {
        return;
    }
    const bool grid = !isViewerVisible();
    QStringList paths;
    bool cut = false;
    const bool canPaste = FileOps::readMimeData(QApplication::clipboard()->mimeData(), &paths, &cut);
    const bool hasSelection = grid && !m_fileGrid->selectedItems().isEmpty();
    const auto selected = m_fileGrid->selectedItems();
    m_batchConvertAction->setEnabled(grid && !m_batchRunning
        && std::any_of(selected.cbegin(), selected.cend(), [](const QListWidgetItem *item) {
            return !item->isHidden() && item->data(IsImageRole).toBool();
        }));
    m_batchRenameAction->setEnabled(hasSelection && !m_batchRunning && !m_fileJob);
    m_undoRenameAction->setEnabled(grid && !m_renameHistory.isEmpty() && !m_batchRunning && !m_fileJob);
    m_cutAction->setEnabled(hasSelection);
    m_copyFilesAction->setEnabled(hasSelection);
    m_pasteAction->setEnabled(grid && canPaste && !m_currentFolderPath.isEmpty() && !m_fileJob);
    m_selectAllAction->setEnabled(grid);
    m_invertSelectionAction->setEnabled(grid);
    m_copyImageAction->setText(grid ? QStringLiteral("Copy") : QStringLiteral("Copy Image"));
    m_copyImageAction->setEnabled(!grid || hasSelection);
}

// Pastes the files on the clipboard into targetFolder: copies them, or moves them when they were
// cut (here or in Explorer). Runs in the background with a progress dialog.
void MainWindow::pasteFiles(const QString &targetFolder)
{
    if (targetFolder.isEmpty()) {
        return;
    }
    if (m_fileJob) {
        statusBar()->showMessage(QStringLiteral("Still busy with the last paste"), 3000);
        return;
    }
    QStringList sources;
    bool move = false;
    if (!FileOps::readMimeData(QApplication::clipboard()->mimeData(), &sources, &move)) {
        statusBar()->showMessage(QStringLiteral("There are no files on the clipboard to paste"), 3000);
        return;
    }

    const QDir target(targetFolder);
    QList<FileOps::Item> items;
    QList<int> conflicts;
    QStringList problems;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        if (!info.exists()) {
            problems.append(QStringLiteral("%1 no longer exists").arg(QDir::toNativeSeparators(source)));
            continue;
        }
        const bool isDir = info.isDir();
        const bool sameFolder = FileOps::isSameOrInside(info.absolutePath(), targetFolder)
                                && FileOps::isSameOrInside(targetFolder, info.absolutePath());
        if (isDir && FileOps::isSameOrInside(targetFolder, source)) {
            problems.append(QStringLiteral("%1 can't go inside itself").arg(info.fileName()));
            continue;
        }
        FileOps::Item item;
        item.source = info.absoluteFilePath();
        if (sameFolder) {
            if (move) {
                continue; // cut and pasted where it already is: nothing to do
            }
            item.target = target.filePath(FileOps::copyName(targetFolder, info.fileName(), isDir));
        } else {
            item.target = target.filePath(info.fileName());
            if (QFileInfo::exists(item.target)) {
                conflicts.append(items.size());
            }
        }
        items.append(item);
    }

    if (!conflicts.isEmpty()) {
        QStringList names;
        for (int index : conflicts) {
            if (names.size() == 10) {
                names.append(QStringLiteral("…"));
                break;
            }
            names.append(QFileInfo(items.at(index).target).fileName());
        }
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(move ? QStringLiteral("Move") : QStringLiteral("Paste"));
        box.setText(conflicts.size() == 1
                        ? QStringLiteral("%1 already exists in %2.").arg(names.first(), QDir::toNativeSeparators(targetFolder))
                        : QStringLiteral("%1 items already exist in %2.").arg(conflicts.size()).arg(QDir::toNativeSeparators(targetFolder)));
        box.setInformativeText(QStringLiteral("Replace them, skip them, or keep both (the pasted ones get a number)?\n"
                                              "Folders with the same name are merged.\n\n")
                               + names.join(QLatin1Char('\n')));
        QPushButton *replace = box.addButton(QStringLiteral("Replace"), QMessageBox::AcceptRole);
        QPushButton *skip = box.addButton(QStringLiteral("Skip"), QMessageBox::AcceptRole);
        QPushButton *keepBoth = box.addButton(QStringLiteral("Keep Both"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(skip);
        box.exec();
        if (box.clickedButton() == replace) {
            for (int index : conflicts) {
                items[index].replace = true;
            }
        } else if (box.clickedButton() == keepBoth) {
            for (int index : conflicts) {
                const QFileInfo info(items.at(index).source);
                items[index].target = target.filePath(FileOps::keepBothName(targetFolder, info.fileName(), info.isDir()));
            }
        } else if (box.clickedButton() == skip) {
            for (int i = conflicts.size() - 1; i >= 0; --i) {
                items.removeAt(conflicts.at(i));
            }
        } else {
            return;
        }
    }

    if (!problems.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Paste"), problems.join(QLatin1Char('\n')));
    }
    if (items.isEmpty()) {
        return;
    }

    m_fileJobTarget = targetFolder;
    m_fileJobSourceFolders.clear();
    for (const FileOps::Item &item : items) {
        m_fileJobSourceFolders.append(QFileInfo(item.source).absolutePath());
    }
    m_fileJob = new FileOps::Job(items, move, this);
    connect(m_fileJob, &FileOps::Job::finished, this, &MainWindow::finishFileJob);

    const QString verb = move ? QStringLiteral("Moving") : QStringLiteral("Copying");
    m_fileJobProgress = new QProgressDialog(this);
    m_fileJobProgress->setWindowTitle(move ? QStringLiteral("Move") : QStringLiteral("Copy"));
    m_fileJobProgress->setLabelText(QStringLiteral("%1 to %2…").arg(verb, QDir::toNativeSeparators(targetFolder)));
    m_fileJobProgress->setRange(0, 0);
    m_fileJobProgress->setMinimumDuration(400);
    m_fileJobProgress->setAutoClose(false);
    m_fileJobProgress->setAutoReset(false);
    m_fileJobProgress->setMinimumWidth(460);
    connect(m_fileJobProgress, &QProgressDialog::canceled, m_fileJob, &FileOps::Job::cancel);

    m_fileJobTimer.setInterval(100);
    disconnect(&m_fileJobTimer, nullptr, this, nullptr);
    connect(&m_fileJobTimer, &QTimer::timeout, this, [this, verb, targetFolder]() {
        if (!m_fileJob || !m_fileJobProgress) {
            return;
        }
        const QLocale locale;
        if (m_fileJob->counting()) {
            m_fileJobProgress->setLabelText(QStringLiteral("Counting files… %1").arg(m_fileJob->totalFiles()));
            return;
        }
        const qint64 total = qMax<qint64>(1, m_fileJob->totalBytes());
        m_fileJobProgress->setRange(0, 1000);
        m_fileJobProgress->setValue(int(qMin<qint64>(1000, m_fileJob->doneBytes() * 1000 / total)));
        m_fileJobProgress->setLabelText(QStringLiteral("%1 %2 of %3 files (%4 of %5)\nto %6")
                                            .arg(verb)
                                            .arg(qMin(m_fileJob->doneFiles() + 1, m_fileJob->totalFiles()))
                                            .arg(m_fileJob->totalFiles())
                                            .arg(locale.formattedDataSize(m_fileJob->doneBytes()),
                                                 locale.formattedDataSize(m_fileJob->totalBytes()),
                                                 QDir::toNativeSeparators(targetFolder)));
    });
    m_fileJobTimer.start();
    updateEditActions();
    m_fileJob->start();
}

void MainWindow::finishFileJob()
{
    m_fileJobTimer.stop();
    FileOps::Job *job = m_fileJob;
    m_fileJob = nullptr;
    if (m_fileJobProgress) {
        m_fileJobProgress->deleteLater();
        m_fileJobProgress = nullptr;
    }
    if (!job) {
        return;
    }
    const QStringList created = job->created();
    const QStringList errors = job->errors();
    const bool canceled = job->isCanceled();
    const bool move = job->move();
    job->deleteLater();

    // Explorer empties the clipboard once cut files have been moved.
    if (move && !canceled && errors.isEmpty()) {
        QApplication::clipboard()->clear();
    }

    // Show the result: refresh the folder on screen if files went into or out of it.
    const QString current = QDir::cleanPath(m_currentFolderPath);
    auto same = [](const QString &a, const QString &b) {
        return FileOps::isSameOrInside(a, b) && FileOps::isSameOrInside(b, a);
    };
    bool touched = same(current, m_fileJobTarget);
    for (const QString &folder : m_fileJobSourceFolders) {
        touched = touched || (move && same(current, folder));
    }
    if (touched && !isViewerVisible()) {
        const int scroll = m_fileGrid->verticalScrollBar()->value();
        populateFileGrid(m_currentFolderPath, false);
        m_fileGrid->verticalScrollBar()->setValue(scroll);
        if (same(current, m_fileJobTarget)) {
            QListWidgetItem *first = nullptr;
            for (int row = 0; row < m_fileGrid->count(); ++row) {
                QListWidgetItem *item = m_fileGrid->item(row);
                if (item->isHidden()) {
                    continue;
                }
                const QString path = item->data(FilePathRole).toString();
                for (const QString &made : created) {
                    if (same(path, made)) {
                        if (!first) {
                            first = item;
                            m_fileGrid->setCurrentItem(item, QItemSelectionModel::ClearAndSelect);
                        } else {
                            item->setSelected(true);
                        }
                        break;
                    }
                }
            }
            if (first) {
                m_fileGrid->scrollToItem(first);
            }
        }
    }

    const QString verb = move ? QStringLiteral("Moved") : QStringLiteral("Pasted");
    if (canceled) {
        statusBar()->showMessage(QStringLiteral("Canceled"), 4000);
    } else {
        statusBar()->showMessage(created.size() == 1 ? QStringLiteral("%1 %2").arg(verb, QFileInfo(created.first()).fileName())
                                                     : QStringLiteral("%1 %2 items").arg(verb).arg(created.size()),
                                 4000);
    }
    if (!errors.isEmpty()) {
        QStringList shown = errors.mid(0, 15);
        if (errors.size() > shown.size()) {
            shown.append(QStringLiteral("…and %1 more").arg(errors.size() - shown.size()));
        }
        QMessageBox::warning(this, move ? QStringLiteral("Move") : QStringLiteral("Paste"),
                             QStringLiteral("Some files could not be %1:\n\n%2")
                                 .arg(move ? QStringLiteral("moved") : QStringLiteral("copied"), shown.join(QLatin1Char('\n'))));
    }
    updateEditActions();
}

void MainWindow::setViewTransform(ColorTransform::View view)
{
    if (view == ColorTransform::View::CustomLut && !ColorTransform::hasLut()) {
        updateViewTransformActions(m_view);
        chooseLutFile();
        return;
    }
    updateViewTransformActions(view);
    if (view == m_view) {
        return;
    }

    m_view = view;
    QSettings().setValue(SettingsViewTransform, ColorTransform::settingsKey(view));
    statusBar()->showMessage(QStringLiteral("Color: %1").arg(ColorTransform::displayName(view)), 3000);

    refreshCurrentDisplay();
    resetThumbnails();
}

void MainWindow::setInput(ColorTransform::Input input)
{
    m_input = input;
    updateInputActions();
    QSettings().setValue(SettingsInput, ColorTransform::inputSettingsKey(input));
    statusBar()->showMessage(QStringLiteral("EXR input: %1").arg(ColorTransform::inputDisplayName(input)), 3000);
    refreshCurrentDisplay();
    resetThumbnails();
}

void MainWindow::updateInputActions()
{
    for (QAction *action : m_inputGroup->actions()) {
        action->setChecked(action->data().toInt() == static_cast<int>(m_input));
    }
}

// ARRI's own LUTs for the LogC3/LogC4 views: a "luts" folder next to the program (filled at build
// time, see ONION_ARRI_LUT_DIR in CMakeLists.txt) wins over the folder picked in the menu.
void MainWindow::findArriLuts()
{
    const QStringList folders = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/luts"),
        QSettings().value(SettingsArriLutFolder).toString(),
    };
    ColorTransform::findArriLuts(folders);
}

void MainWindow::chooseArriLutFolder()
{
    const QString current = QSettings().value(SettingsArriLutFolder).toString();
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("ARRI LUT Folder"), current);
    if (folder.isEmpty()) {
        return;
    }
    QSettings().setValue(SettingsArriLutFolder, folder);
    findArriLuts();
    const QString logC3 = ColorTransform::arriLutFilePath(ColorTransform::View::LogC3ToRec709);
    const QString logC4 = ColorTransform::arriLutFilePath(ColorTransform::View::LogC4ToRec709);
    auto describe = [](const QString &path) {
        return path.isEmpty() ? QStringLiteral("not found (using the approximation)") : QDir::toNativeSeparators(path);
    };
    QMessageBox::information(this, QStringLiteral("ARRI LUTs"),
                             QStringLiteral("LogC3: %1\nLogC4: %2").arg(describe(logC3), describe(logC4)));
    updateLutActions();
    m_frameLoader->clear();
    refreshCurrentDisplay();
    resetThumbnails();
}

void MainWindow::chooseLutFile()
{
    QString directory = QFileInfo(ColorTransform::lutFilePath()).absolutePath();
    if (ColorTransform::lutFilePath().isEmpty()) {
        directory = m_currentFolderPath;
    }
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load LUT"), directory, QStringLiteral("Cube LUTs (*.cube);;All files (*)"));
    if (!filePath.isEmpty()) {
        useLutFile(filePath);
    }
}

// Loads a .cube file and switches the view to it.
void MainWindow::useLutFile(const QString &filePath)
{
    QString error;
    if (!ColorTransform::loadLut(filePath, &error)) {
        QMessageBox::warning(this, QStringLiteral("Load LUT"),
                             QStringLiteral("Could not load %1:\n%2").arg(QDir::toNativeSeparators(filePath), error));
        return;
    }

    QSettings settings;
    settings.setValue(SettingsLutFile, filePath);
    QStringList recent = settings.value(SettingsRecentLuts).toStringList();
    recent.removeAll(filePath);
    recent.prepend(filePath);
    while (recent.size() > MaxRecentLuts) {
        recent.removeLast();
    }
    settings.setValue(SettingsRecentLuts, recent);
    updateLutActions();

    if (m_view == ColorTransform::View::CustomLut) {
        // Same view, new LUT: re-render explicitly.
        statusBar()->showMessage(QStringLiteral("Color: %1").arg(ColorTransform::displayName(m_view)), 3000);
        m_frameLoader->clear();
        refreshCurrentDisplay();
        resetThumbnails();
    } else {
        setViewTransform(ColorTransform::View::CustomLut);
    }
}

// Remembers an image the user opened (not every frame stepped through). A sequence takes one
// place: opening another frame of it replaces the earlier entry.
void MainWindow::addRecentImage(const QString &filePath)
{
    auto sequenceKey = [](const QString &path) {
        const QFileInfo info(path);
        const QString number = frameNumberOf(path);
        QString name = info.fileName();
        if (!number.isEmpty()) {
            name.remove(name.lastIndexOf(number), number.size());
        }
        return QDir::cleanPath(info.absolutePath()).toLower() + QLatin1Char('/') + name.toLower();
    };
    const QString path = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    const QString key = sequenceKey(path);

    QSettings settings;
    QStringList recent = settings.value(SettingsRecentImages).toStringList();
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const QString &entry) { return sequenceKey(entry) == key; }),
                 recent.end());
    recent.prepend(path);
    while (recent.size() > MaxRecentImages) {
        recent.removeLast();
    }
    settings.setValue(SettingsRecentImages, recent);
    updateRecentImagesMenu();
}

void MainWindow::updateRecentImagesMenu()
{
    if (!m_recentImagesMenu) {
        return;
    }
    m_recentImagesMenu->clear();
    const QStringList recent = QSettings().value(SettingsRecentImages).toStringList();
    int number = 1;
    for (const QString &filePath : recent) {
        // File name, with its folder on the right. Nothing here touches the disk: the files may
        // be on network drives that are slow or offline.
        const QFileInfo info(filePath);
        const QString folder = QFileInfo(info.absolutePath()).fileName();
        QString text = QStringLiteral("&%1  %2").arg(number % 10).arg(info.fileName().replace(QLatin1Char('&'), QStringLiteral("&&")));
        if (!folder.isEmpty()) {
            text += QLatin1Char('\t') + folder;
        }
        QAction *action = m_recentImagesMenu->addAction(text);
        action->setToolTip(QDir::toNativeSeparators(filePath));
        connect(action, &QAction::triggered, this, [this, filePath]() {
            if (!QFileInfo::exists(filePath)) {
                QMessageBox::warning(this, QStringLiteral("Recent Images"),
                                     QStringLiteral("%1 can't be found. It may have been moved, or its drive isn't connected.")
                                         .arg(QDir::toNativeSeparators(filePath)));
                return;
            }
            handlePathActivated(filePath);
        });
        ++number;
    }
    if (!recent.isEmpty()) {
        m_recentImagesMenu->addSeparator();
        m_recentImagesMenu->addAction(QStringLiteral("Clear Recent Images"), this, [this]() {
            QSettings().remove(SettingsRecentImages);
            updateRecentImagesMenu();
        });
    }
    m_recentImagesMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::updateLutActions()
{
    for (QAction *action : m_viewTransformGroup->actions()) {
        action->setText(ColorTransform::displayName(static_cast<ColorTransform::View>(action->data().toInt())));
    }
    updateColorButton(m_view);

    m_recentLutsMenu->clear();
    const QStringList recent = QSettings().value(SettingsRecentLuts).toStringList();
    for (const QString &filePath : recent) {
        QAction *action = m_recentLutsMenu->addAction(QFileInfo(filePath).completeBaseName());
        action->setToolTip(QDir::toNativeSeparators(filePath));
        connect(action, &QAction::triggered, this, [this, filePath]() {
            useLutFile(filePath);
        });
    }
    m_recentLutsMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::updateViewTransformActions()
{
    updateViewTransformActions(m_view);
}

void MainWindow::updateViewTransformActions(ColorTransform::View view)
{
    updateColorButton(view);
    for (QAction *action : m_viewTransformGroup->actions()) {
        action->setChecked(action->data().toInt() == static_cast<int>(view));
    }
}

QSize MainWindow::thumbnailResolution() const
{
    const int resolution = m_gridView != GridView::Icons || m_thumbnailDisplaySize <= SmallThumbnailResolution
        ? SmallThumbnailResolution
        : LargeThumbnailResolution;
    return QSize(resolution, resolution);
}

void MainWindow::setThumbnailDisplaySize(int size)
{
    size = qBound(MinThumbnailDisplaySize, size, MaxThumbnailDisplaySize);
    size = (size + ThumbnailDisplayStep / 2) / ThumbnailDisplayStep * ThumbnailDisplayStep;
    if (size == m_thumbnailDisplaySize && m_gridView == GridView::Icons) {
        return;
    }
    setGridView(GridView::Icons, size);
    statusBar()->showMessage(QStringLiteral("Thumbnails %1 px").arg(size), 1500);
}

void MainWindow::setGridView(GridView view, int iconSize)
{
    const QSize oldResolution = thumbnailResolution();
    m_gridView = view;
    if (view == GridView::Icons && iconSize > 0) {
        iconSize = qBound(MinThumbnailDisplaySize, iconSize, MaxThumbnailDisplaySize);
        m_thumbnailDisplaySize = (iconSize + ThumbnailDisplayStep / 2) / ThumbnailDisplayStep * ThumbnailDisplayStep;
    }
    if (m_thumbnailSizeSlider && m_thumbnailSizeSlider->value() != m_thumbnailDisplaySize) {
        const QSignalBlocker blocker(m_thumbnailSizeSlider);
        m_thumbnailSizeSlider->setValue(m_thumbnailDisplaySize);
    }

    applyGridView();

    QSettings settings;
    settings.setValue(SettingsGridView, QLatin1String(gridViewKey(static_cast<int>(view))));
    settings.setValue(SettingsThumbnailSize, m_thumbnailDisplaySize);

    if (thumbnailResolution() != oldResolution) {
        resetThumbnails(); // bigger than 128 px: make sharper ones (cached separately)
    } else {
        m_thumbnailScheduleTimer.start(); // more (or fewer) fit on screen now
    }
}

void MainWindow::applyGridView()
{
    // Keep the selected item in view while the grid reflows.
    QListWidgetItem *anchor = m_fileGrid->currentItem();
    const int size = m_thumbnailDisplaySize;

    switch (m_gridView) {
    case GridView::Icons:
        m_fileDelegate->setLayout(FileItemDelegate::Layout::Standard);
        m_fileGrid->setViewMode(QListView::IconMode);
        // Set these explicitly: setViewMode() leaves alone what another view set before.
        m_fileGrid->setMovement(QListView::Static);
        m_fileGrid->setFlow(QListView::LeftToRight);
        m_fileGrid->setWrapping(true);
        m_fileGrid->setIconSize(QSize(size, size));
        m_fileGrid->setGridSize(QSize(qMax(size + 42, 112), size + 40));
        m_fileDelegate->setStandardItemSize(m_fileGrid->gridSize() - QSize(8, 8));
        m_fileGrid->setSpacing(10);
        m_fileGrid->setWordWrap(true);
        break;
    case GridView::List:
        m_fileDelegate->setLayout(FileItemDelegate::Layout::Standard);
        m_fileDelegate->setStandardItemSize(QSize());
        m_fileGrid->setViewMode(QListView::ListMode);
        m_fileGrid->setFlow(QListView::TopToBottom);
        m_fileGrid->setWrapping(true);
        m_fileGrid->setIconSize(QSize(ListIconSize, ListIconSize));
        m_fileGrid->setGridSize(QSize(280, ListIconSize + 8));
        m_fileGrid->setSpacing(0);
        m_fileGrid->setWordWrap(false);
        break;
    case GridView::Details:
        m_fileDelegate->setLayout(FileItemDelegate::Layout::Details);
        m_fileGrid->setViewMode(QListView::ListMode);
        m_fileGrid->setFlow(QListView::TopToBottom);
        m_fileGrid->setWrapping(false);
        m_fileGrid->setIconSize(QSize(FileItemDelegate::DetailsIconSize, FileItemDelegate::DetailsIconSize));
        m_fileGrid->setGridSize(QSize());
        m_fileGrid->setSpacing(0);
        m_fileGrid->setWordWrap(false);
        break;
    case GridView::Tiles:
        m_fileDelegate->setLayout(FileItemDelegate::Layout::Tiles);
        m_fileGrid->setViewMode(QListView::ListMode);
        m_fileGrid->setFlow(QListView::LeftToRight);
        m_fileGrid->setWrapping(true);
        m_fileGrid->setIconSize(QSize(FileItemDelegate::TileIconSize, FileItemDelegate::TileIconSize));
        m_fileGrid->setGridSize(QSize(FileItemDelegate::TileWidth + 8, FileItemDelegate::TileHeight + 8));
        m_fileGrid->setSpacing(0);
        m_fileGrid->setWordWrap(false);
        break;
    }
    m_fileGrid->setResizeMode(QListView::Adjust);
    m_fileGrid->setUniformItemSizes(true);
    m_fileGrid->setSelectionRectVisible(true); // ListMode turns the box off by default
    m_fileGrid->doItemsLayout();
    m_fileGrid->viewport()->update();
    m_detailsHeader->setOffset(m_fileGrid->horizontalScrollBar()->value() - m_fileGrid->frameWidth());
    if (anchor) {
        m_fileGrid->scrollToItem(anchor);
    }
    updateGridViewActions();
    updateGridChrome();
}

void MainWindow::stepGridView(int direction)
{
    // From big to small: icon sizes, then List, Details, Tiles (the order Explorer uses).
    if (m_gridView == GridView::Icons) {
        const int size = m_thumbnailDisplaySize + direction * ThumbnailDisplayStep;
        if (size < MinThumbnailDisplaySize) {
            setGridView(GridView::List);
        } else {
            setThumbnailDisplaySize(size);
        }
        return;
    }
    const QList<GridView> order = {GridView::Tiles, GridView::Details, GridView::List};
    const int next = order.indexOf(m_gridView) + direction;
    if (next >= order.size()) {
        setGridView(GridView::Icons, MinThumbnailDisplaySize);
    } else if (next >= 0) {
        setGridView(order.at(next));
    }
}

void MainWindow::updateGridViewActions()
{
    if (!m_gridViewGroup) {
        return;
    }
    const int current = static_cast<int>(m_gridView) * 1000 + (m_gridView == GridView::Icons ? m_thumbnailDisplaySize : 0);
    for (QAction *action : m_gridViewGroup->actions()) {
        action->setChecked(action->data().toInt() == current);
    }
}

void MainWindow::alignSizeSlider()
{
    if (!m_sliderSpacer || !m_thumbnailSizeSlider || !m_thumbnailSizeSlider->isVisible()) {
        return;
    }
    constexpr int Inset = 6;
    QStatusBar *bar = statusBar();
    // Room the status bar keeps right of its last widget (size grip, margins). Measured once while
    // the spacer is empty and laid out; it doesn't change after that.
    if (m_statusBarReserve < 0) {
        if (m_sliderSpacer->width() != 0 || m_sliderSpacer->x() <= 0) {
            return;
        }
        m_statusBarReserve = bar->width() - m_sliderSpacer->x();
    }
    const int reserve = m_statusBarReserve;
    const int gridRight = bar->mapFrom(this, m_contentStack->mapTo(this, QPoint(m_contentStack->width(), 0))).x();
    const int width = qBound(0, bar->width() - reserve - (gridRight - Inset), bar->width() / 2);
    if (width != m_sliderSpacer->width()) {
        m_sliderSpacer->setFixedWidth(width);
    }
}

// The size slider and the Details header belong to the grid, so they hide while an image is open.
void MainWindow::updateGridChrome()
{
    const bool gridShown = m_contentStack && m_contentStack->currentWidget() == m_fileGrid;
    m_gridSearch->setVisible(gridShown);
    m_imagesOnly->setVisible(gridShown);
    if (m_thumbnailSizeSlider) {
        m_thumbnailSizeSlider->setVisible(gridShown);
        if (m_sliderSpacer) {
            m_sliderSpacer->setVisible(gridShown);
        }
        QTimer::singleShot(0, this, &MainWindow::alignSizeSlider);
    }
    if (m_detailsHeader) {
        m_detailsHeader->setVisible(gridShown && m_gridView == GridView::Details);
    }
}

// Throws away the current thumbnails and makes them again (for example after a view change).
void MainWindow::resetThumbnails()
{
    m_pendingThumbnailPaths.clear();
    m_thumbnailRetryCounts.clear();
    m_thumbnailRetryTimer.stop();
    m_thumbnailPool.clear();
    ++m_thumbnailGeneration;

    const QIcon placeholder = style()->standardIcon(QStyle::SP_FileIcon);
    for (QListWidgetItem *item : std::as_const(m_thumbnailItems)) {
        item->setData(ThumbnailLoadedRole, false);
        item->setIcon(placeholder);
    }
    QTimer::singleShot(0, this, &MainWindow::scheduleVisibleThumbnails);
}

void MainWindow::setFolderPanelVisible(bool visible)
{
    if (!m_folderTreePanel) {
        return;
    }

    m_folderTreePanel->setVisible(visible);
    if (visible && !m_currentFolderPath.isEmpty()) {
        m_folderTreePanel->showFolder(m_currentFolderPath);
    }
    if (m_toggleFoldersAction->isChecked() != visible) {
        m_toggleFoldersAction->setChecked(visible);
    }
}

void MainWindow::handleFolderSelected(const QString &folderPath)
{
    populateFileGrid(folderPath);
    statusBar()->showMessage(QStringLiteral("Selected folder: %1").arg(folderPath), 3000);
}

void MainWindow::handleGridItemActivated(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    const QString path = item->data(FilePathRole).toString();
    if (item->data(IsFolderRole).toBool()) {
        populateFileGrid(path);
        statusBar()->showMessage(QStringLiteral("Selected folder: %1").arg(path), 3000);
        return;
    }

    if (isImageFile(path)) {
        openImagePath(path);
    }
}

void MainWindow::populateFileGrid(const QString &folderPath, bool addToHistory)
{
    const QDir folder(folderPath);
    if (!folder.exists()) {
        return;
    }

    const bool folderChanged = QDir::cleanPath(m_currentFolderPath) != QDir::cleanPath(folderPath);
    if (addToHistory && !m_currentFolderPath.isEmpty() && folderChanged) {
        m_folderHistory.append(m_currentFolderPath);
    }

    m_currentFolderPath = folderPath;
    m_currentImagePaths.clear();
    m_currentImageIndex = -1;
    m_sequence.clear();
    m_sequencePositions.clear();
    m_pendingThumbnailPaths.clear();
    m_thumbnailItems.clear();
    m_thumbnailRetryCounts.clear();
    m_thumbnailRetryTimer.stop();
    m_thumbnailPool.clear();
    ++m_thumbnailGeneration;
    m_fileGrid->clear();
    if (folderChanged) {
        m_gridSearch->clear();
    }

    QFileIconProvider iconProvider;
    const QLocale locale;
    // Look the placeholder icon up once: each standardIcon() call is slow (~17 ms with the
    // Windows 11 style), which made a 5000-file folder take over a minute to open.
    const QIcon imagePlaceholderIcon = style()->standardIcon(QStyle::SP_FileIcon);
    const QFileInfoList folders = folder.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name | QDir::DirsFirst | QDir::IgnoreCase);

    for (const QFileInfo &folderInfo : folders) {
        auto *item = new QListWidgetItem(iconProvider.icon(QFileIconProvider::Folder), folderInfo.fileName(), m_fileGrid);
        item->setData(FilePathRole, folderInfo.absoluteFilePath());
        item->setData(IsFolderRole, true);
        item->setData(IsImageRole, false);
        item->setData(FileItemDelegate::ModifiedTextRole, locale.toString(folderInfo.lastModified(), QLocale::ShortFormat));
        item->setData(FileItemDelegate::TypeTextRole, fileTypeText(folderInfo, false));
        item->setToolTip(folderInfo.absoluteFilePath());
    }

    const QFileInfoList files = folder.entryInfoList(
        QDir::Files,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fileInfo : files) {
        const QString filePath = fileInfo.absoluteFilePath();
        const bool image = isImageFile(filePath);
        const QIcon icon = image ? imagePlaceholderIcon : iconProvider.icon(fileInfo);
        auto *item = new QListWidgetItem(icon, fileInfo.fileName(), m_fileGrid);
        item->setData(FilePathRole, filePath);
        item->setData(IsFolderRole, false);
        item->setData(IsImageRole, image);
        item->setData(ThumbnailLoadedRole, false);
        item->setData(FileItemDelegate::ModifiedTextRole, locale.toString(fileInfo.lastModified(), QLocale::ShortFormat));
        item->setData(FileItemDelegate::TypeTextRole, fileTypeText(fileInfo, image));
        item->setData(FileItemDelegate::SizeTextRole, fileSizeText(locale, fileInfo.size()));
        item->setData(FileSizeRole, fileInfo.size());
        item->setToolTip(filePath);

        if (image) {
            m_currentImagePaths.append(filePath);
            m_thumbnailItems.insert(filePath, item);
        }
    }

    if (m_imagesOnly->isChecked() || !m_gridSearch->text().isEmpty()) {
        applyGridFilters();
    }
    stopPlayback();
    showGrid();
    m_pathBar->setPath(folderPath);
    updateBackAction();
    if (m_folderTreePanel && m_folderTreePanel->isVisible()) {
        m_folderTreePanel->showFolder(folderPath);
    }
    QTimer::singleShot(0, this, &MainWindow::scheduleVisibleThumbnails);
    m_thumbnailScheduleTimer.start(250);
}

void MainWindow::applyGridFilters()
{
    const NameFilter nameFilter(m_gridSearch->text());
    const bool imagesOnly = m_imagesOnly->isChecked();
    m_visibleGridItems = 0;
    QItemSelection hiddenSelection;
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
        const bool hidden = !nameFilter.matches(item->text())
            || (imagesOnly && !item->data(IsFolderRole).toBool() && !item->data(IsImageRole).toBool());
        if (!hidden) {
            ++m_visibleGridItems;
        }
        if (item->isHidden() != hidden) {
            item->setHidden(hidden);
        }
    }
    // Hiding a row preserves its Qt selection. Deselect hidden rows before file actions run.
    auto *selection = m_fileGrid->selectionModel();
    for (const QModelIndex &index : selection->selectedIndexes()) {
        if (m_fileGrid->item(index.row())->isHidden()) {
            hiddenSelection.select(index, index);
        }
    }
    selection->select(hiddenSelection, QItemSelectionModel::Deselect);
    if (const auto *current = m_fileGrid->currentItem(); current && current->isHidden()) {
        selection->clearCurrentIndex();
    }
    updateImageInfo();
    m_thumbnailScheduleTimer.start();
}

void MainWindow::navigateBack()
{
    if (m_folderHistory.isEmpty()) {
        updateBackAction();
        return;
    }

    const QString folderPath = m_folderHistory.takeLast();
    populateFileGrid(folderPath, false);
    statusBar()->showMessage(QStringLiteral("Selected folder: %1").arg(folderPath), 3000);
    updateBackAction();
}

void MainWindow::navigateUp()
{
    QDir parent(m_currentFolderPath);
    if (m_currentFolderPath.isEmpty() || !parent.cdUp()) {
        return;
    }
    const QString child = QDir::cleanPath(m_currentFolderPath);
    populateFileGrid(parent.absolutePath());
    // Select the folder we came from, like Explorer.
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
        if (QDir::cleanPath(item->data(FilePathRole).toString()) == child) {
            m_fileGrid->setCurrentItem(item);
            m_fileGrid->scrollToItem(item);
            break;
        }
    }
}

void MainWindow::updateBackAction()
{
    if (m_backAction) {
        m_backAction->setEnabled(!m_folderHistory.isEmpty());
    }
    if (m_upAction) {
        QDir parent(m_currentFolderPath);
        m_upAction->setEnabled(!m_currentFolderPath.isEmpty() && parent.cdUp());
    }
}

void MainWindow::showGrid()
{
    stopPlayback();
    m_frameLoader->stopGif();
    m_contentStack->setCurrentWidget(m_fileGrid);
    m_pixelLabel->clear();
    m_swatchLabel->hide();
    m_scopesPanel->clear();
    scheduleMetadata();
    updateImageInfo();
    updateColorButton(m_view);
    // Keep the grid on the image that was being viewed.
    if (m_currentImageIndex >= 0 && m_currentImageIndex < m_currentImagePaths.size()) {
        if (QListWidgetItem *item = m_thumbnailItems.value(m_currentImagePaths.at(m_currentImageIndex));
            item && !item->isHidden()) {
            m_fileGrid->setCurrentItem(item);
            m_fileGrid->scrollToItem(item);
        }
    }
}

void MainWindow::showCurrentImage()
{
    goToImage(m_currentImageIndex);
}

void MainWindow::showNextImage()
{
    stopPlayback();
    stepImage(1);
}

void MainWindow::showPreviousImage()
{
    stopPlayback();
    stepImage(-1);
}

void MainWindow::layoutOverlayButtons()
{
    if (!m_imagePage) {
        return;
    }
    const int margin = 12;
    // Over the main viewport only, so they stay clear of the split pane.
    const QRect area(m_imageViewport->mapTo(m_imagePage, QPoint(0, 0)), m_imageViewport->size());
    m_closeImageButton->move(area.right() - margin - m_closeImageButton->width() + 1, area.top() + margin);
    m_previousImageButton->move(area.left() + margin, area.center().y() - m_previousImageButton->height() / 2);
    m_nextImageButton->move(area.right() - margin - m_nextImageButton->width() + 1,
                            area.center().y() - m_nextImageButton->height() / 2);
}

void MainWindow::revealOverlayButtons()
{
    for (OverlayButton *button : {m_closeImageButton, m_previousImageButton, m_nextImageButton}) {
        button->reveal();
    }
}

void MainWindow::updateImageNavigation()
{
    m_previousImageButton->setEnabled(m_currentImageIndex > 0);
    m_nextImageButton->setEnabled(m_currentImageIndex >= 0 && m_currentImageIndex + 1 < m_currentImagePaths.size());
}

void MainWindow::setImageListFromFolder(const QString &folderPath)
{
    const QDir folder(folderPath);
    if (!folder.exists()) {
        m_currentImagePaths.clear();
        m_currentImageIndex = -1;
        m_sequence.clear();
        m_sequencePositions.clear();
        return;
    }

    m_currentImagePaths.clear();
    m_sequence.clear();
    m_sequencePositions.clear();
    const QFileInfoList files = folder.entryInfoList(
        QDir::Files,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fileInfo : files) {
        if (isImageFile(fileInfo.absoluteFilePath())) {
            m_currentImagePaths.append(fileInfo.absoluteFilePath());
        }
    }

    m_currentImageIndex = -1;
}

void MainWindow::scheduleVisibleThumbnails()
{
    m_backgroundThumbnailsQueued = false;
    // Keeps running while an image is open too, so the grid is ready when you go back.
    if (!m_fileGrid || m_fileGrid->count() == 0) {
        return;
    }

    auto needsThumbnail = [](const QListWidgetItem *item) {
        return !item->isHidden() && item->data(IsImageRole).toBool() && !item->data(ThumbnailLoadedRole).toBool();
    };

    // On-screen items first, at high priority so they jump ahead of background work.
    int firstVisibleRow = -1;
    int lastVisibleRow = -1;
    const QRect visibleRect = m_fileGrid->viewport()->rect();
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
        if (item->isHidden()) {
            continue;
        }
        if (!m_fileGrid->visualItemRect(item).intersects(visibleRect)) {
            if (firstVisibleRow >= 0) {
                break; // rows are laid out in order, so the visible ones are contiguous
            }
            continue;
        }

        if (firstVisibleRow < 0) {
            firstVisibleRow = row;
        }
        lastVisibleRow = row;
        if (needsThumbnail(item)) {
            requestThumbnail(item, ThumbnailPriorityVisible);
        }
    }

    // Then keep working through the rest of the folder in the background: the rows just below
    // the view first, then the ones above it. Only a limited number are queued at a time, so a
    // scroll never has to wait behind a long backlog.
    if (firstVisibleRow < 0) {
        firstVisibleRow = 0;
        lastVisibleRow = -1;
    }

    auto queueBackground = [&](int row) {
        if (m_pendingThumbnailPaths.size() >= MaxPendingThumbnails) {
            return false;
        }
        QListWidgetItem *item = m_fileGrid->item(row);
        // Items waiting on a Windows background fetch are retried only while visible.
        if (needsThumbnail(item) && !m_thumbnailRetryCounts.contains(item->data(FilePathRole).toString())) {
            requestThumbnail(item, ThumbnailPriorityBackground);
        }
        return true;
    };

    for (int row = lastVisibleRow + 1; row < m_fileGrid->count(); ++row) {
        if (!queueBackground(row)) {
            return;
        }
    }
    for (int row = firstVisibleRow - 1; row >= 0; --row) {
        if (!queueBackground(row)) {
            return;
        }
    }
}

void MainWindow::requestThumbnail(QListWidgetItem *item, int priority)
{
    const QString filePath = item->data(FilePathRole).toString();
    if (filePath.isEmpty() || m_pendingThumbnailPaths.contains(filePath)) {
        return;
    }

    const int generation = m_thumbnailGeneration;
    const ColorTransform::View view = m_view;
    const ColorTransform::Input input = m_input;
    const QSize size = thumbnailResolution();
    m_pendingThumbnailPaths.insert(filePath);

    m_thumbnailPool.start([this, filePath, generation, view, input, size]() {
        bool pending = false;
        const QImage thumbnail = loadThumbnailImage(filePath, view, input, size, &pending);
        QMetaObject::invokeMethod(
            this,
            [this, filePath, generation, thumbnail, pending]() {
                applyThumbnail(filePath, generation, thumbnail, pending);
            },
            Qt::QueuedConnection);
    }, priority);
}

void MainWindow::applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail, bool pending)
{
    if (generation != m_thumbnailGeneration) {
        return;
    }

    m_pendingThumbnailPaths.remove(filePath);

    // Top up the background queue once it has drained halfway.
    if (!m_backgroundThumbnailsQueued && m_pendingThumbnailPaths.size() < MaxPendingThumbnails / 2) {
        m_backgroundThumbnailsQueued = true;
        QTimer::singleShot(0, this, &MainWindow::scheduleVisibleThumbnails);
    }

    QListWidgetItem *item = m_thumbnailItems.value(filePath, nullptr);
    if (!item) {
        return;
    }

    if (pending && thumbnail.isNull()) {
        // Windows is fetching this thumbnail in the background (OneDrive online-only files).
        // Leave the item unloaded and look again shortly, a limited number of times.
        int &attempts = m_thumbnailRetryCounts[filePath];
        if (++attempts <= MaxThumbnailRetries) {
            if (!m_thumbnailRetryTimer.isActive()) {
                m_thumbnailRetryTimer.start();
            }
            return;
        }
    }

    // Mark the item as done even when no thumbnail could be made, so it is not requested again
    // on every scroll or resize.
    m_thumbnailRetryCounts.remove(filePath);
    item->setData(ThumbnailLoadedRole, true);
    if (!thumbnail.isNull()) {
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    }
}

bool MainWindow::isImageFile(const QString &filePath) const
{
    static const QStringList suffixes = supportedImageSuffixes();
    return suffixes.contains(QFileInfo(filePath).suffix().toLower());
}

void MainWindow::setSplitEnabled(bool enabled)
{
    if (enabled != m_splitPane->isHidden()) {
        return;
    }
    if (!enabled) {
        setSplitPaneActive(false);
        m_splitPane->hide();
        m_imageViewport->setHighlighted(false);
        layoutSplitHeader();
        updateImageInfo();
        return;
    }
    // Start in the folder of the image being viewed, with that image selected.
    if (m_splitPane->folderPath().isEmpty()) {
        const QString folder = m_currentImagePath.isEmpty() ? m_currentFolderPath
                                                            : QFileInfo(m_currentImagePath).absolutePath();
        m_splitPane->browse(folder, m_currentImagePath);
    }
    m_splitPane->show();
    const bool horizontal = m_viewerSplitter->orientation() == Qt::Horizontal;
    const int total = horizontal ? m_viewerSplitter->width() : m_viewerSplitter->height();
    m_viewerSplitter->setSizes({total / 2, total - total / 2});
    if (!isViewerVisible() && !m_currentImagePath.isEmpty()) {
        m_contentStack->setCurrentWidget(m_imagePage);
    }
    setSplitPaneActive(true);
    QTimer::singleShot(0, this, &MainWindow::layoutOverlayButtons);
    layoutSplitHeader();
}

void MainWindow::setSplitOrientation(Qt::Orientation orientation)
{
    for (QAction *action : m_splitOrientationGroup->actions()) {
        action->setChecked(action->data().toInt() == int(orientation));
    }
    QSettings().setValue(SettingsSplitOrientation,
                         orientation == Qt::Vertical ? QStringLiteral("vertical") : QStringLiteral("horizontal"));
    if (m_viewerSplitter->orientation() == orientation) {
        return;
    }
    m_viewerSplitter->setOrientation(orientation);
    if (!m_splitPane->isHidden()) {
        const int total = orientation == Qt::Horizontal ? m_viewerSplitter->width() : m_viewerSplitter->height();
        m_viewerSplitter->setSizes({total / 2, total - total / 2});
    }
    QTimer::singleShot(0, this, &MainWindow::layoutOverlayButtons);
    layoutSplitHeader();
}

bool MainWindow::isSplitPaneActive() const
{
    return m_splitPaneActive && m_splitPane && !m_splitPane->isHidden();
}

void MainWindow::setSplitPaneActive(bool active)
{
    const bool split = !m_splitPane->isHidden();
    active = active && split;
    m_splitPaneActive = active;
    m_splitPane->setActive(active);
    m_imageViewport->setHighlighted(split && !active);
    if (active) {
        m_splitPane->focusContent();
    } else if (split && QApplication::focusWidget() && m_splitPane->isAncestorOf(QApplication::focusWidget())) {
        m_imageViewport->setFocus(Qt::OtherFocusReason);
    }
    updateImageInfo();
    // Metadata and scopes follow the active pane.
    scheduleMetadata();
    updateScopes();
}

ImageViewport *MainWindow::activeViewport() const
{
    return isSplitPaneActive() ? m_splitPane->viewport() : m_imageViewport;
}

void MainWindow::syncSplitViews(ImageViewport *from)
{
    if (!m_splitSyncAction->isChecked() || m_splitPane->isHidden() || !from) {
        return;
    }
    ImageViewport *to = from == m_imageViewport ? m_splitPane->viewport() : m_imageViewport;
    to->matchView(*from);
}

void MainWindow::layoutSplitHeader()
{
    if (!m_splitPane || !m_navigationRow) {
        return;
    }
    m_openSplitButton->setVisible(isViewerVisible() && m_splitPane->isHidden());
    QWidget *header = m_splitPane->header();
    const bool inRow = !m_splitPane->isHidden() && isViewerVisible()
        && m_viewerSplitter->orientation() == Qt::Horizontal;
    if (!inRow) {
        if (header->parentWidget() != m_splitPane) {
            m_splitPane->reclaimHeader();
        }
        m_mainNavigation->setMinimumWidth(0);
        m_mainNavigation->setMaximumWidth(QWIDGETSIZE_MAX);
        return;
    }
    if (header->parentWidget() != m_navigationRow) {
        m_navigationRow->layout()->addWidget(header);
        m_splitPane->setHeaderInline(true);
        header->show();
    }
    // The main navigation ends where the split pane starts, so each bar sits over its own pane.
    const int paneLeft = m_splitPane->mapTo(this, QPoint(0, 0)).x() - m_navigationRow->mapTo(this, QPoint(0, 0)).x();
    m_mainNavigation->setFixedWidth(qMax(0, paneLeft));
}
