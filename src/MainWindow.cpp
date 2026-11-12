#include "MainWindow.h"

#include "ColorTransform.h"
#include "DpxReader.h"
#include "ExrReader.h"
#include "FolderTreePanel.h"
#include "ImageLoader.h"
#include "ImageViewport.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
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
    , m_imageLoader(std::make_unique<QtImageLoader>())
{
    setWindowTitle(QStringLiteral("Onion Image Viewer"));
    resize(1280, 800);

    applyDarkPalette();
    createActions();
    createMenus();
    createToolbar();
    createCentralLayout();
    loadSettings();
    qApp->installEventFilter(this);

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

    m_loadLutAction = new QAction(QStringLiteral("Load LUT File…"), this);
    m_loadLutAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    connect(m_loadLutAction, &QAction::triggered, this, &MainWindow::chooseLutFile);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(m_openImageAction);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(m_backAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleFoldersAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitToWindowAction);
    viewMenu->addAction(m_actualSizeAction);

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
    if (m_colorButton) {
        m_colorButton->setText(QStringLiteral("Color: %1").arg(ColorTransform::displayName(view)));
    }
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

    m_folderPathLabel = new QLabel(QStringLiteral("No folder selected"), this);
    statusBar()->addPermanentWidget(m_folderPathLabel, 1);

    connect(m_folderTreePanel, &FolderTreePanel::folderSelected, this, &MainWindow::handleFolderSelected);
    connect(m_fileGrid, &QListWidget::itemDoubleClicked, this, &MainWindow::handleGridItemActivated);
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        m_thumbnailScheduleTimer.start();
    });
    connect(m_closeImageButton, &QPushButton::clicked, this, &MainWindow::showGrid);
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
    restoreState(settings.value(SettingsMainWindowState).toByteArray());

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

    const ImageLoader::Result result = m_imageLoader->load(filePath);
    if (!result.isValid()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Could Not Open Image"),
            QStringLiteral("The image could not be opened:\n%1").arg(result.error));
        return;
    }

    m_currentSourceImage = result.image;
    m_imageViewport->setImage(ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input));
    if (m_currentImagePaths.isEmpty()) {
        m_currentImagePaths.append(filePath);
        m_currentImageIndex = 0;
    } else {
        const int imageIndex = m_currentImagePaths.indexOf(filePath);
        if (imageIndex < 0) {
            m_currentImagePaths.append(filePath);
            m_currentImageIndex = m_currentImagePaths.size() - 1;
        } else {
            m_currentImageIndex = imageIndex;
        }
    }

    m_contentStack->setCurrentWidget(m_imageViewport->parentWidget());
    updateImageNavigation();
    statusBar()->showMessage(filePath, 5000);
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

    if (!m_currentSourceImage.isNull()) {
        m_imageViewport->setImage(ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input), true);
    }
    resetThumbnails();
}

void MainWindow::setInput(ColorTransform::Input input)
{
    m_input = input;
    updateInputActions();
    QSettings().setValue(SettingsInput, ColorTransform::inputSettingsKey(input));
    statusBar()->showMessage(QStringLiteral("EXR input: %1").arg(ColorTransform::inputDisplayName(input)), 3000);
    if (!m_currentSourceImage.isNull()) {
        m_imageViewport->setImage(ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input), true);
    }
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
    if (!m_currentSourceImage.isNull()) {
        m_imageViewport->setImage(ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input), true);
    }
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
        if (!m_currentSourceImage.isNull()) {
            m_imageViewport->setImage(ColorTransform::toDisplay(m_currentSourceImage, m_view, m_input), true);
        }
        resetThumbnails();
    } else {
        setViewTransform(ColorTransform::View::CustomLut);
    }
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
    m_folderPathLabel->setText(folderPath);
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
        m_folderPathLabel->setText(path);
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

    showGrid();
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
    m_folderPathLabel->setText(folderPath);
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
    m_contentStack->setCurrentWidget(m_fileGrid);
}

void MainWindow::showCurrentImage()
{
    if (m_currentImageIndex < 0 || m_currentImageIndex >= m_currentImagePaths.size()) {
        return;
    }

    openImagePath(m_currentImagePaths.at(m_currentImageIndex));
}

void MainWindow::showNextImage()
{
    if (m_currentImageIndex + 1 >= m_currentImagePaths.size()) {
        return;
    }

    ++m_currentImageIndex;
    showCurrentImage();
}

void MainWindow::showPreviousImage()
{
    if (m_currentImageIndex <= 0) {
        return;
    }

    --m_currentImageIndex;
    showCurrentImage();
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
