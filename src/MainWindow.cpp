#include "MainWindow.h"

#include "FolderTreePanel.h"
#include "ImageLoader.h"
#include "ImageViewport.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
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
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <windows.h>

#endif

namespace {
const QString SettingsMainWindowGeometry = QStringLiteral("mainWindow/geometry");
const QString SettingsMainWindowState = QStringLiteral("mainWindow/state");
const QString SettingsFoldersVisible = QStringLiteral("folders/visible");
const QString SettingsSplitterState = QStringLiteral("splitter/state");
const int FilePathRole = Qt::UserRole;
const int IsFolderRole = Qt::UserRole + 1;
const int IsImageRole = Qt::UserRole + 2;
const int ThumbnailLoadedRole = Qt::UserRole + 3;
const QSize ThumbnailSize(128, 128);

QStringList supportedImageSuffixes()
{
    QStringList suffixes;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    suffixes.reserve(formats.size());
    for (const QByteArray &format : formats) {
        suffixes.append(QString::fromLatin1(format).toLower());
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

QString thumbnailCachePath(const QFileInfo &fileInfo)
{
    const QString cacheKeySource = fileInfo.absoluteFilePath()
        + QStringLiteral("|")
        + QString::number(fileInfo.size())
        + QStringLiteral("|")
        + QString::number(fileInfo.lastModified().toMSecsSinceEpoch());

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

QImage shellThumbnailForImage(const QString &filePath)
{
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initializeResult);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
        return QImage();
    }

    Microsoft::WRL::ComPtr<IShellItemImageFactory> imageFactory;
    const HRESULT itemResult = SHCreateItemFromParsingName(
        reinterpret_cast<LPCWSTR>(filePath.utf16()),
        nullptr,
        IID_PPV_ARGS(&imageFactory));

    QImage thumbnail;
    if (SUCCEEDED(itemResult) && imageFactory) {
        HBITMAP bitmap = nullptr;
        SIZE size = {ThumbnailSize.width(), ThumbnailSize.height()};
        const HRESULT thumbnailResult = imageFactory->GetImage(
            size,
            SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK,
            &bitmap);

        if (SUCCEEDED(thumbnailResult) && bitmap) {
            thumbnail = imageFromHBitmap(bitmap);
            DeleteObject(bitmap);
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    return thumbnail;
}

#endif

QImage readerThumbnailForImage(const QString &filePath)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    const QSize imageSize = reader.size();
    if (imageSize.isValid()) {
        reader.setScaledSize(imageSize.scaled(ThumbnailSize, Qt::KeepAspectRatio));
    }

    return reader.read();
}

QImage loadThumbnailImage(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    const QString cachePath = thumbnailCachePath(fileInfo);

    QImage thumbnail;
    if (thumbnail.load(cachePath)) {
        return thumbnail;
    }

#ifdef Q_OS_WIN
    thumbnail = shellThumbnailForImage(filePath);
#endif

    if (thumbnail.isNull()) {
        thumbnail = readerThumbnailForImage(filePath);
    }

    if (!thumbnail.isNull()) {
        thumbnail.save(cachePath, "PNG");
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
}

MainWindow::~MainWindow()
{
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
    connect(m_fileGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbnails);
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
        QStringLiteral("Images (*.bmp *.gif *.jpg *.jpeg *.png *.pbm *.pgm *.ppm *.xbm *.xpm);;All Files (*.*)"));

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

    m_imageViewport->setImage(result.image);
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

void MainWindow::setFolderPanelVisible(bool visible)
{
    if (!m_folderTreePanel) {
        return;
    }

    m_folderTreePanel->setVisible(visible);
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
    ++m_thumbnailGeneration;
    m_fileGrid->clear();

    QFileIconProvider iconProvider;
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
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &fileInfo : files) {
        const QString filePath = fileInfo.absoluteFilePath();
        const bool image = isImageFile(filePath);
        const QIcon icon = image ? style()->standardIcon(QStyle::SP_FileIcon) : iconProvider.icon(fileInfo);
        auto *item = new QListWidgetItem(icon, fileInfo.fileName(), m_fileGrid);
        item->setData(FilePathRole, filePath);
        item->setData(IsFolderRole, false);
        item->setData(IsImageRole, image);
        item->setData(ThumbnailLoadedRole, false);
        item->setToolTip(filePath);

        if (image) {
            m_currentImagePaths.append(filePath);
        }
    }

    showGrid();
    updateBackAction();
    QTimer::singleShot(0, this, &MainWindow::scheduleVisibleThumbnails);
    QTimer::singleShot(250, this, &MainWindow::scheduleVisibleThumbnails);
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
        QDir::Files | QDir::Readable,
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
    if (!m_fileGrid || m_contentStack->currentWidget() != m_fileGrid) {
        return;
    }

    int requestedCount = 0;
    const QRect visibleRect = m_fileGrid->viewport()->rect();
    for (int row = 0; row < m_fileGrid->count(); ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
        if (!item || !item->data(IsImageRole).toBool() || item->data(ThumbnailLoadedRole).toBool()) {
            continue;
        }

        if (m_fileGrid->visualItemRect(item).intersects(visibleRect)) {
            requestThumbnail(item);
            ++requestedCount;
        }
    }

    if (requestedCount > 0) {
        return;
    }

    constexpr int InitialThumbnailBatchSize = 80;
    for (int row = 0; row < m_fileGrid->count() && requestedCount < InitialThumbnailBatchSize; ++row) {
        QListWidgetItem *item = m_fileGrid->item(row);
        if (!item || !item->data(IsImageRole).toBool() || item->data(ThumbnailLoadedRole).toBool()) {
            continue;
        }

        requestThumbnail(item);
        ++requestedCount;
    }
}

void MainWindow::requestThumbnail(QListWidgetItem *item)
{
    const QString filePath = item->data(FilePathRole).toString();
    if (filePath.isEmpty() || m_pendingThumbnailPaths.contains(filePath)) {
        return;
    }

    const int generation = m_thumbnailGeneration;
    m_pendingThumbnailPaths.insert(filePath);

    QThreadPool::globalInstance()->start([this, filePath, generation]() {
        const QImage thumbnail = loadThumbnailImage(filePath);
        QMetaObject::invokeMethod(
            this,
            [this, filePath, generation, thumbnail]() {
                applyThumbnail(filePath, generation, thumbnail);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail)
{
    m_pendingThumbnailPaths.remove(filePath);
    if (generation != m_thumbnailGeneration || thumbnail.isNull()) {
        return;
    }

    const QList<QListWidgetItem *> matches = m_fileGrid->findItems(QFileInfo(filePath).fileName(), Qt::MatchExactly);
    for (QListWidgetItem *item : matches) {
        if (item->data(FilePathRole).toString() == filePath) {
            item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
            item->setData(ThumbnailLoadedRole, true);
            break;
        }
    }
}

bool MainWindow::isImageFile(const QString &filePath) const
{
    static const QStringList suffixes = supportedImageSuffixes();
    return suffixes.contains(QFileInfo(filePath).suffix().toLower());
}
