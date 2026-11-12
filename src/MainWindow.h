#pragma once

#include "ColorTransform.h"
#include "FrameLoader.h"

#include <QImage>
#include <QMainWindow>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QStringList>
#include <memory>

class QAction;
class QActionGroup;
class FolderTreePanel;
class ImageLoader;
class ImageViewport;
class QIcon;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QToolButton;
class QPushButton;
class PathBar;
class MetadataPanel;
class QDockWidget;
class QKeyEvent;
class QDragEnterEvent;
class QDropEvent;
class QSplitter;
class QStackedWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Opens a folder (in the grid) or an image (in the viewer), e.g. from the command line.
    void openPath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolbar();
    void createCentralLayout();
    void applyDarkPalette();
    void loadSettings();
    void saveSettings();
    void openImage();
    void openImagePath(const QString &filePath);
    void setFolderPanelVisible(bool visible);
    void setViewTransform(ColorTransform::View view);
    void updateViewTransformActions();
    void chooseLutFile();
    void chooseArriLutFolder();
    void findArriLuts();
    void showAbout();
    void updateColorButton(ColorTransform::View view);
    void setInput(ColorTransform::Input input);
    void updateInputActions();
    void useLutFile(const QString &filePath);
    void updateLutActions();
    void updateViewTransformActions(ColorTransform::View view);
    void resetThumbnails();
    void handleFolderSelected(const QString &folderPath);
    void handleGridItemActivated(QListWidgetItem *item);
    void populateFileGrid(const QString &folderPath, bool addToHistory = true);
    void navigateBack();
    void updateBackAction();
    void showGrid();
    void showCurrentImage();
    void showNextImage();
    void showPreviousImage();
    void updateImageNavigation();
    void setImageListFromFolder(const QString &folderPath);
    void scheduleVisibleThumbnails();
    void requestThumbnail(QListWidgetItem *item, int priority);
    void applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail, bool pending);
    bool isImageFile(const QString &filePath) const;

    // Viewer: frames load in the background (FrameLoader); these show and step through them.
    enum CompareMode { CompareOff, CompareNoView, ComparePinned };
    void showFrame(const FrameLoader::Frame &frame, bool requested);
    void stepImage(int delta, bool wrap = false);
    void goToImage(int index);
    void prefetchAround();
    void togglePlayback();
    void stopPlayback();
    void playbackTick();
    bool handleViewerKey(QKeyEvent *event);
    void refreshCurrentDisplay();
    void setCompareMode(CompareMode mode);
    void pinCurrentImage();
    void updateCompare();
    void updateImageInfo();
    void updatePixelReadout(const QPoint &pixel);
    void handlePathActivated(const QString &path);
    bool isViewerVisible() const;

    // Extras: channels, clipping, full screen, clipboard and Explorer.
    void setChannel(int channel, bool toggle);
    void setFullScreenMode(bool on);
    void copyImageToClipboard();
    void copyPathToClipboard(const QString &path);
    void showInExplorer(const QString &path);
    void showGridContextMenu(const QPoint &pos);
    QString currentPath() const;
    void scheduleMetadata();
    void addRecentImage(const QString &filePath);
    void updateRecentImagesMenu();
    void loadMetadata();

    std::shared_ptr<ImageLoader> m_imageLoader;
    FrameLoader *m_frameLoader = nullptr;
    PathBar *m_pathBar = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_pixelLabel = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pinAction = nullptr;
    QActionGroup *m_compareGroup = nullptr;
    QActionGroup *m_channelGroup = nullptr;
    QAction *m_clippingAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_copyImageAction = nullptr;
    QAction *m_copyPathAction = nullptr;
    QAction *m_showInExplorerAction = nullptr;
    QList<QWidget *> m_hiddenForFullScreen;
    MetadataPanel *m_metadataPanel = nullptr;
    QDockWidget *m_metadataDock = nullptr;
    QAction *m_metadataAction = nullptr;
    QTimer m_metadataTimer;
    QThreadPool m_metadataPool;
    int m_metadataGeneration = 0;
    bool m_wasMaximized = false;
    QTimer m_playTimer;
    QImage m_currentDisplayImage;
    QString m_currentImagePath;
    QImage m_pinnedSource;
    QString m_pinnedPath;
    QImage m_pinnedDisplay;
    QString m_pinnedDisplayKey;
    CompareMode m_compareMode = CompareOff;
    CompareMode m_lastCompareMode = CompareNoView;
    QPoint m_hoveredPixel = QPoint(-1, -1);
    int m_displayedIndex = -1;
    int m_stepDirection = 1;
    bool m_freshOpen = false;
    FolderTreePanel *m_folderTreePanel = nullptr;
    QListWidget *m_fileGrid = nullptr;
    ImageViewport *m_imageViewport = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    QSplitter *m_splitter = nullptr;
    QPushButton *m_closeImageButton = nullptr;
    QPushButton *m_previousImageButton = nullptr;
    QPushButton *m_nextImageButton = nullptr;
    QAction *m_toggleFoldersAction = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_openImageAction = nullptr;
    QAction *m_fitToWindowAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QActionGroup *m_viewTransformGroup = nullptr;
    QAction *m_customLutAction = nullptr;
    QAction *m_loadLutAction = nullptr;
    QAction *m_arriLutFolderAction = nullptr;
    QMenu *m_recentLutsMenu = nullptr;
    QMenu *m_recentImagesMenu = nullptr;
    QMenu *m_colorMenu = nullptr;
    QToolButton *m_colorButton = nullptr;
    QActionGroup *m_inputGroup = nullptr;
    ColorTransform::Input m_input = ColorTransform::Input::Auto;
    ColorTransform::View m_view = ColorTransform::View::None;
    QImage m_currentSourceImage;
    QString m_currentFolderPath;
    QStringList m_folderHistory;
    QStringList m_currentImagePaths;
    QSet<QString> m_pendingThumbnailPaths;
    QHash<QString, QListWidgetItem *> m_thumbnailItems;
    QThreadPool m_thumbnailPool;
    QTimer m_thumbnailScheduleTimer;
    QTimer m_thumbnailRetryTimer;
    QHash<QString, int> m_thumbnailRetryCounts;
    bool m_backgroundThumbnailsQueued = false;
    int m_thumbnailGeneration = 0;
    int m_currentImageIndex = -1;
};
