#include "MainWindow.h"

#include "ColorTransform.h"
#include "DpxReader.h"
#include "ExrReader.h"
#include "FolderTreePanel.h"
#include "ImageLoader.h"
#include "ImageViewport.h"
#include "Metadata.h"
#include "MetadataPanel.h"
#include "PathBar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProcess>
#include <QUrl>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileIconProvider>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QImageReader>
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
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSize>
#include <QStackedWidget>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QStatusBar>
#include <QSaveFile>
#include <QScrollBar>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <wrl/client.h>
#include <windows.h>

#endif

namespace {
const QString SettingsMainWindowGeometry = QStringLiteral("mainWindow/geometry");
const QString SettingsMainWindowState = QStringLiteral("mainWindow/state");
const QString SettingsFoldersVisible = QStringLiteral("folders/visible");
const QString SettingsViewTransform = QStringLiteral("view/colorTransform");
const QString SettingsInput = QStringLiteral("view/input");
const QString SettingsLutFile = QStringLiteral("view/lutFile");
const QString SettingsArriLutFolder = QStringLiteral("view/arriLutFolder");
const QString SettingsRecentLuts = QStringLiteral("view/recentLuts");
const QString SettingsRecentImages = QStringLiteral("file/recentImages");
constexpr int MaxRecentImages = 10;
const int MaxRecentLuts = 8;
const QString SettingsSplitterState = QStringLiteral("splitter/state");
const int FilePathRole = Qt::UserRole;
const int IsFolderRole = Qt::UserRole + 1;
const int IsImageRole = Qt::UserRole + 2;
const int ThumbnailLoadedRole = Qt::UserRole + 3;
const QSize ThumbnailSize(128, 128);
constexpr int ThumbnailScheduleDelayMs = 60;
constexpr int MaxPendingThumbnails = 32;
constexpr int ThumbnailPriorityVisible = 1;
constexpr int ThumbnailPriorityBackground = 0;
constexpr int ThumbnailRetryDelayMs = 2000;
constexpr int MaxThumbnailRetries = 30; // about a minute of retries
constexpr int PlaybackFps = 24;
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
    if (Exr::isAvailable()) {
        suffixes.append(QStringLiteral("exr"));
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

QString thumbnailCachePath(const QFileInfo &fileInfo, ColorTransform::View view, ColorTransform::Input input)
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
QImage shellThumbnailForImage(const QString &filePath, SIIGBF extraFlags, bool *pending = nullptr)
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
        SIZE size = {ThumbnailSize.width(), ThumbnailSize.height()};
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
QImage displayThumbnail(const QImage &source, ColorTransform::View view, ColorTransform::Input input)
{
    if (source.isNull()) {
        return source;
    }
    // Resolve Auto from the full image: scaling doesn't keep the file's tags.
    input = ColorTransform::resolveInput(input, source);
    view = ColorTransform::resolveView(view, source);
    QImage reduced = source;
    const QSize roughSize = ThumbnailSize * 2;
    if (reduced.width() > roughSize.width() || reduced.height() > roughSize.height()) {
        reduced = reduced.scaled(roughSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return ColorTransform::toDisplay(reduced, view, input)
        .scaled(ThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage readerThumbnailForImage(const QString &filePath, ColorTransform::View view, ColorTransform::Input input)
{
    if (Exr::isExrFile(filePath)) {
        return displayThumbnail(Exr::read(filePath, nullptr, true), view, input);
    }

    if (Dpx::isDpxFile(filePath)) {
        return displayThumbnail(Dpx::read(filePath, nullptr, ThumbnailSize), view, input);
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        reader.setScaledSize(imageSize.scaled(ThumbnailSize, Qt::KeepAspectRatio));
    }

    const QImage image = reader.read();
    return view == ColorTransform::View::None ? image : ColorTransform::toDisplay(image, view);
}

QImage loadThumbnailImage(const QString &filePath, ColorTransform::View view, ColorTransform::Input input, bool *pending)
{
    *pending = false;
    const QFileInfo fileInfo(filePath);
    const QString cachePath = thumbnailCachePath(fileInfo, view, input);

    QImage thumbnail;
    if (thumbnail.load(cachePath)) {
        return thumbnail;
    }

#ifdef Q_OS_WIN
    if (isCloudOnlyFile(filePath)) {
        // Never read the file itself here, or OneDrive downloads it. First try the Windows
        // thumbnail cache, then the sync provider's own thumbnail (OneDrive serves these from
        // the cloud, the same way Explorer shows them). If neither has one, keep the plain icon.
        thumbnail = shellThumbnailForImage(filePath, SIIGBF_INCACHEONLY);
        if (thumbnail.isNull()) {
            thumbnail = shellThumbnailForImage(filePath, SIIGBF(0), pending);
        }
        if (!thumbnail.isNull()) {
            thumbnail = ColorTransform::toDisplay(thumbnail, view)
                            .scaled(ThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            saveThumbnailToCache(thumbnail, cachePath);
        }
        return thumbnail;
    }

    thumbnail = shellThumbnailForImage(filePath, SIIGBF(0));
    if (!thumbnail.isNull()) {
        thumbnail = ColorTransform::toDisplay(thumbnail, view)
                        .scaled(ThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
#endif

    if (thumbnail.isNull()) {
        thumbnail = readerThumbnailForImage(filePath, view, input);
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
    m_playTimer.setInterval(1000 / PlaybackFps);
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
}

MainWindow::~MainWindow()
{
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
    Q_UNUSED(watched)

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

    m_backAction = new QAction(QStringLiteral("Back"), this);
    m_backAction->setShortcut(QKeySequence::Back);
    m_backAction->setEnabled(false);
    connect(m_backAction, &QAction::triggered, this, &MainWindow::navigateBack);

    m_openImageAction = new QAction(QStringLiteral("Open Image..."), this);
    m_openImageAction->setShortcut(QKeySequence::Open);
    connect(m_openImageAction, &QAction::triggered, this, &MainWindow::openImage);

    m_fitToWindowAction = new QAction(QStringLiteral("Fit to Window"), this);
    connect(m_fitToWindowAction, &QAction::triggered, this, [this]() {
        m_imageViewport->fitToWindow();
    });

    m_actualSizeAction = new QAction(QStringLiteral("100% Zoom"), this);
    connect(m_actualSizeAction, &QAction::triggered, this, [this]() {
        m_imageViewport->zoomActualSize();
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
    m_playAction->setToolTip(QStringLiteral("Play through the images of the folder (Space)"));
    connect(m_playAction, &QAction::triggered, this, &MainWindow::togglePlayback);

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
    });

    m_fullScreenAction = new QAction(QStringLiteral("Full Screen"), this);
    m_fullScreenAction->setCheckable(true);
    m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_fullScreenAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_fullScreenAction, &QAction::toggled, this, &MainWindow::setFullScreenMode);
    addAction(m_fullScreenAction); // keeps F11 working while the menu bar is hidden

    m_copyImageAction = new QAction(QStringLiteral("Copy Image"), this);
    m_copyImageAction->setShortcut(QKeySequence::Copy);
    m_copyImageAction->setToolTip(QStringLiteral("Copy the picture as shown (with the color view) to the clipboard"));
    connect(m_copyImageAction, &QAction::triggered, this, &MainWindow::copyImageToClipboard);

    m_copyPathAction = new QAction(QStringLiteral("Copy Path"), this);
    m_copyPathAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    connect(m_copyPathAction, &QAction::triggered, this, [this]() {
        copyPathToClipboard(currentPath());
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
    });
    m_metadataPool.setMaxThreadCount(1);
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
    fileMenu->addAction(m_copyImageAction);
    fileMenu->addAction(m_copyPathAction);
    fileMenu->addAction(m_showInExplorerAction);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(m_backAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleFoldersAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitToWindowAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_playAction);
    viewMenu->addSeparator();
    viewMenu->addActions(m_compareGroup->actions());
    viewMenu->addAction(m_pinAction);
    viewMenu->addSeparator();
    QMenu *channelMenu = viewMenu->addMenu(QStringLiteral("Channels"));
    channelMenu->addActions(m_channelGroup->actions());
    viewMenu->addAction(m_clippingAction);
    viewMenu->addAction(m_fullScreenAction);
    viewMenu->addAction(m_metadataAction);
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
            "  W  compare wipe on / off\n"
            "  P  pin this image as the compare image\n"
            "  F  fit to window,  1  100% zoom\n"
            "  R / G / B / A / L  red / green / blue / alpha / luminance (again for RGB)\n"
            "  C  show clipping\n"
            "  I  metadata panel (Ctrl+I anywhere)\n"
            "  Ctrl+C  copy the picture\n"
            "  Esc  leave full screen, then back to the grid\n\n"
            "Color:\n"
            "  Ctrl+1  Auto (from file tags)\n"
            "  Ctrl+0 / 3 / 4  None / LogC3 / LogC4\n"
            "  Ctrl+L  LUT file,  Ctrl+Shift+L  load a LUT\n\n"
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
    helpMenu->addAction(QStringLiteral("About OnionImageViewer"), this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("About OnionImageViewer"));
    box.setIconPixmap(QIcon(QStringLiteral(":/icons/OnionImageViewer.png")).pixmap(QSize(96, 96)));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<h3>OnionImageViewer</h3>"
                               "<p>Image viewer and batch converter.</p>"
                               "<p>Built with Qt %1.</p>")
                    .arg(QString::fromLatin1(qVersion())));
    box.exec();
}

void MainWindow::createToolbar()
{
    QToolBar *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(m_backAction);
    toolbar->addSeparator();
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
    m_colorButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
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
    m_colorButton->setText(QStringLiteral("Color: %1").arg(text));
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

    m_pathBar = new PathBar(rightPanel);
    rightLayout->addWidget(m_pathBar);
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
    m_fileGrid->setIconSize(ThumbnailSize);
    m_fileGrid->setGridSize(QSize(170, 168));
    m_fileGrid->setSpacing(10);
    m_fileGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileGrid->setWordWrap(true);
    m_fileGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileGrid, &QWidget::customContextMenuRequested, this, &MainWindow::showGridContextMenu);
    m_fileGrid->setTextElideMode(Qt::ElideRight);

    auto *imagePage = new QWidget(m_contentStack);
    auto *imageLayout = new QGridLayout(imagePage);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->setSpacing(0);

    m_imageViewport = new ImageViewport(imagePage);
    m_closeImageButton = new QPushButton(QStringLiteral("X"), imagePage);
    m_previousImageButton = new QPushButton(QStringLiteral("<"), imagePage);
    m_nextImageButton = new QPushButton(QStringLiteral(">"), imagePage);

    m_closeImageButton->setToolTip(QStringLiteral("Close image"));
    m_previousImageButton->setToolTip(QStringLiteral("Previous image"));
    m_nextImageButton->setToolTip(QStringLiteral("Next image"));

    for (QPushButton *button : {m_closeImageButton, m_previousImageButton, m_nextImageButton}) {
        button->setFixedSize(36, 36);
        button->setFocusPolicy(Qt::NoFocus);
    }

    imageLayout->addWidget(m_imageViewport, 0, 0, 3, 3);
    imageLayout->addWidget(m_closeImageButton, 0, 2, Qt::AlignTop | Qt::AlignRight);
    imageLayout->addWidget(m_previousImageButton, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    imageLayout->addWidget(m_nextImageButton, 1, 2, Qt::AlignRight | Qt::AlignVCenter);

    m_contentStack->addWidget(m_fileGrid);
    m_contentStack->addWidget(imagePage);
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
    connect(m_imageViewport, &ImageViewport::hoveredPixelChanged, this, &MainWindow::updatePixelReadout);
    m_imageViewport->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageViewport, &QWidget::customContextMenuRequested, this, &MainWindow::showViewerContextMenu);

    connect(m_folderTreePanel, &FolderTreePanel::folderSelected, this, &MainWindow::handleFolderSelected);
    connect(m_fileGrid, &QListWidget::itemDoubleClicked, this, &MainWindow::handleGridItemActivated);
    connect(m_fileGrid, &QListWidget::currentItemChanged, this, &MainWindow::scheduleMetadata);
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_closeImageButton, &QPushButton::clicked, this, [this]() {
        stopPlayback();
        showGrid();
    });
    connect(m_previousImageButton, &QPushButton::clicked, this, &MainWindow::showPreviousImage);
    connect(m_nextImageButton, &QPushButton::clicked, this, &MainWindow::showNextImage);
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

    const bool foldersVisible = settings.value(SettingsFoldersVisible, false).toBool();
    m_toggleFoldersAction->setChecked(foldersVisible);
    setFolderPanelVisible(foldersVisible);

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
    settings.setValue(SettingsSplitterState, m_splitter->saveState());
}

void MainWindow::openImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open Image"),
        QString(),
        QStringLiteral("Images (*.bmp *.dpx *.exr *.gif *.jpg *.jpeg *.png *.pbm *.pgm *.ppm *.tga *.tif *.tiff *.webp *.xbm *.xpm);;All Files (*.*)"));

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

    addRecentImage(filePath);
    stopPlayback();
    m_freshOpen = true; // fit the first image; keep zoom and position while stepping after that
    m_displayedIndex = -1;
    m_contentStack->setCurrentWidget(m_imageViewport->parentWidget());
    goToImage(index);
}

bool MainWindow::isViewerVisible() const
{
    return m_contentStack && m_imageViewport && m_contentStack->currentWidget() == m_imageViewport->parentWidget();
}

void MainWindow::goToImage(int index)
{
    if (index < 0 || index >= m_currentImagePaths.size()) {
        return;
    }
    if (index != m_currentImageIndex && m_currentImageIndex >= 0) {
        m_stepDirection = index > m_currentImageIndex ? 1 : -1;
    }
    m_currentImageIndex = index;
    updateImageNavigation();
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
    if (wrap) {
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
    const int count = m_currentImagePaths.size();
    const int ahead = m_playTimer.isActive() ? PrefetchAhead * 2 : PrefetchAhead;
    for (int i = 1; i <= ahead; ++i) {
        int index = m_currentImageIndex + i * m_stepDirection;
        if (m_playTimer.isActive()) {
            index = ((index % count) + count) % count;
        }
        if (index >= 0 && index < count) {
            paths.append(m_currentImagePaths.at(index));
        }
    }
    const int behind = m_currentImageIndex - m_stepDirection;
    if (behind >= 0 && behind < count) {
        paths.append(m_currentImagePaths.at(behind));
    }
    m_frameLoader->prefetch(paths);
}

void MainWindow::showFrame(const FrameLoader::Frame &frame, bool requested)
{
    if (!isViewerVisible() && !m_freshOpen) {
        return;
    }
    const int index = m_currentImagePaths.indexOf(frame.path);
    if (index < 0) {
        return;
    }
    if (!requested) {
        // While scrubbing, show frames that arrive on the way to the wanted one.
        const bool between = m_displayedIndex >= 0
            && ((m_stepDirection > 0 && index > m_displayedIndex && index < m_currentImageIndex)
                || (m_stepDirection < 0 && index < m_displayedIndex && index > m_currentImageIndex));
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
    updatePixelReadout(m_hoveredPixel);
}

void MainWindow::togglePlayback()
{
    if (m_playTimer.isActive()) {
        stopPlayback();
        return;
    }
    if (!isViewerVisible() || m_currentImagePaths.size() < 2) {
        return;
    }
    m_stepDirection = 1;
    m_playTimer.start();
    prefetchAround();
    statusBar()->showMessage(QStringLiteral("Playing at up to %1 fps (Space to stop)").arg(PlaybackFps), 3000);
}

void MainWindow::stopPlayback()
{
    if (m_playTimer.isActive()) {
        m_playTimer.stop();
        statusBar()->showMessage(QStringLiteral("Stopped"), 1500);
    }
}

// Plays as fast as frames are ready, up to PlaybackFps: waits for a frame instead of skipping it.
void MainWindow::playbackTick()
{
    const int count = m_currentImagePaths.size();
    if (!isViewerVisible() || count < 2) {
        stopPlayback();
        return;
    }
    if (m_displayedIndex != m_currentImageIndex) {
        return; // the current frame hasn't arrived yet
    }
    const int next = (m_currentImageIndex + 1) % count;
    if (m_frameLoader->isCached(m_currentImagePaths.at(next))) {
        stepImage(1, true);
    } else {
        prefetchAround();
    }
}

bool MainWindow::handleViewerKey(QKeyEvent *event)
{
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool plain = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (!plain) {
        return false;
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
        m_freshOpen = false;
        goToImage(0);
        return true;
    case Qt::Key_End:
        stopPlayback();
        m_freshOpen = false;
        goToImage(m_currentImagePaths.size() - 1);
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
        m_imageViewport->fitToWindow();
        return true;
    case Qt::Key_1:
        m_imageViewport->zoomActualSize();
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
    default:
        return false;
    }
}

// Re-renders the shown image after a view or LUT change, keeping zoom and position.
void MainWindow::refreshCurrentDisplay()
{
    m_frameLoader->setRender(m_view, m_input);
    if (!m_currentSourceImage.isNull()) {
        m_currentDisplayImage = ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input);
        m_imageViewport->setImage(m_currentDisplayImage, true);
        updateCompare();
        updateImageInfo();
        updatePixelReadout(m_hoveredPixel);
    }
    updateColorButton(m_view);
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
    if (!isViewerVisible() || m_currentSourceImage.isNull()) {
        m_infoLabel->setText(m_currentFolderPath.isEmpty()
                                 ? QString()
                                 : QStringLiteral("%1 images").arg(m_currentImagePaths.size()));
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
    if (!isViewerVisible() || pixel.x() < 0 || m_currentSourceImage.isNull()
        || !m_currentDisplayImage.rect().contains(pixel)) {
        m_pixelLabel->clear();
        m_swatchLabel->hide();
        return;
    }
    const QRgb shown = m_currentDisplayImage.pixel(pixel);
    m_swatchLabel->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;")
                                     .arg(QColor(shown).name()));
    m_swatchLabel->show();
    m_pixelLabel->setText(QStringLiteral("x %1  y %2   stored %3   display %4 %5 %6")
                              .arg(pixel.x(), 4)
                              .arg(pixel.y(), 4)
                              .arg(storedPixelText(m_currentSourceImage, pixel))
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
        QList<QWidget *> chrome = {menuBar(), statusBar(), m_pathBar, m_folderTreePanel, m_metadataDock};
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

void MainWindow::copyImageToClipboard()
{
    if (!isViewerVisible() || m_currentDisplayImage.isNull()) {
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

void MainWindow::showGridContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_fileGrid->itemAt(pos);
    const QString path = item ? item->data(FilePathRole).toString() : m_currentFolderPath;
    if (path.isEmpty()) {
        return;
    }
    QMenu menu(this);
    if (item) {
        menu.addAction(QStringLiteral("Open"), this, [this, item]() {
            handleGridItemActivated(item);
        });
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Copy Path"), this, [this, path]() {
        copyPathToClipboard(path);
    });
    menu.addAction(m_showInExplorerAction->text(), this, [this, path]() {
        showInExplorer(path);
    });
    menu.exec(m_fileGrid->viewport()->mapToGlobal(pos));
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

    if (addToHistory && !m_currentFolderPath.isEmpty() && QDir::cleanPath(m_currentFolderPath) != QDir::cleanPath(folderPath)) {
        m_folderHistory.append(m_currentFolderPath);
    }

    m_currentFolderPath = folderPath;
    m_currentImagePaths.clear();
    m_currentImageIndex = -1;
    m_pendingThumbnailPaths.clear();
    m_thumbnailItems.clear();
    m_thumbnailRetryCounts.clear();
    m_thumbnailRetryTimer.stop();
    m_thumbnailPool.clear();
    ++m_thumbnailGeneration;
    m_fileGrid->clear();

    QFileIconProvider iconProvider;
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
        item->setToolTip(filePath);

        if (image) {
            m_currentImagePaths.append(filePath);
            m_thumbnailItems.insert(filePath, item);
        }
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

void MainWindow::updateBackAction()
{
    if (m_backAction) {
        m_backAction->setEnabled(!m_folderHistory.isEmpty());
    }
}

void MainWindow::showGrid()
{
    stopPlayback();
    m_contentStack->setCurrentWidget(m_fileGrid);
    m_pixelLabel->clear();
    m_swatchLabel->hide();
    scheduleMetadata();
    updateImageInfo();
    updateColorButton(m_view);
    // Keep the grid on the image that was being viewed.
    if (m_currentImageIndex >= 0 && m_currentImageIndex < m_currentImagePaths.size()) {
        if (QListWidgetItem *item = m_thumbnailItems.value(m_currentImagePaths.at(m_currentImageIndex))) {
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
        return;
    }

    m_currentImagePaths.clear();
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

    auto needsThumbnail = [this](const QListWidgetItem *item) {
        return item && item->data(IsImageRole).toBool() && !item->data(ThumbnailLoadedRole).toBool();
    };

    // On-screen items first, at high priority so they jump ahead of background work.
    int firstVisibleRow = -1;
    int lastVisibleRow = -1;
    const QRect visibleRect = m_fileGrid->viewport()->rect();
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
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
    m_pendingThumbnailPaths.insert(filePath);

    m_thumbnailPool.start([this, filePath, generation, view, input]() {
        bool pending = false;
        const QImage thumbnail = loadThumbnailImage(filePath, view, input, &pending);
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
