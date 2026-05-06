#include "MainWindow.h"

#include "FolderTreePanel.h"
#include "ImageLoader.h"
#include "ImageViewport.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QFileDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString SettingsMainWindowGeometry = QStringLiteral("mainWindow/geometry");
const QString SettingsMainWindowState = QStringLiteral("mainWindow/state");
const QString SettingsFoldersVisible = QStringLiteral("folders/visible");
const QString SettingsSplitterState = QStringLiteral("splitter/state");
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
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::createActions()
{
    m_toggleFoldersAction = new QAction(QStringLiteral("Folders"), this);
    m_toggleFoldersAction->setCheckable(true);
    connect(m_toggleFoldersAction, &QAction::toggled, this, &MainWindow::setFolderPanelVisible);

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

    m_imageViewport = new ImageViewport(rightPanel);
    rightLayout->addWidget(m_imageViewport, 1);

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

    const ImageLoader::Result result = m_imageLoader->load(filePath);
    if (!result.isValid()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Could Not Open Image"),
            QStringLiteral("The image could not be opened:\n%1").arg(result.error));
        return;
    }

    m_imageViewport->setImage(result.image);
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
    m_folderPathLabel->setText(folderPath);
    statusBar()->showMessage(QStringLiteral("Selected folder: %1").arg(folderPath), 3000);
}
