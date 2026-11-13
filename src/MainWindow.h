#pragma once

#include "BatchRename.h"
#include "ColorTransform.h"
#include "FrameLoader.h"

#include <QImage>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QStringList>
#include <memory>
#include <atomic>

class QAction;
class QCheckBox;
class QDialog;
class UpdateChecker;
class QActionGroup;
class FolderTreePanel;
class ImageLoader;
class ImageViewport;
class QColor;
class QIcon;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QToolButton;
class OverlayButton;
class PathBar;
class MetadataPanel;
class ScopesPanel;
class QDockWidget;
class QKeyEvent;
class QDragEnterEvent;
class QDropEvent;
class QSlider;
class QHeaderView;
class QItemSelection;
class FileItemDelegate;
class QSplitter;
class QProgressDialog;
namespace FileOps { class Job; }
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
    // Shows About or opens the standalone update check, with shared result handling.
    void showUpdateDialog(bool about);
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
    // Applies the name and file type filters without accessing the filesystem.
    void applyGridFilters();
    void navigateBack();
    void navigateUp();
    void updateBackAction();
    void showGrid();
    void showCurrentImage();
    void showNextImage();
    void showPreviousImage();
    void updateImageNavigation();
    void layoutOverlayButtons();
    void revealOverlayButtons();
    void setImageListFromFolder(const QString &folderPath);
    void scheduleVisibleThumbnails();
    void setThumbnailDisplaySize(int size);
    // Explorer-style views for the file grid. Icons uses m_thumbnailDisplaySize.
    enum class GridView { Icons, List, Details, Tiles };
    void setGridView(GridView view, int iconSize = 0);
    void applyGridView();
    void stepGridView(int direction); // Ctrl+scroll wheel: sizes, then List, Details, Tiles
    void updateGridViewActions();
    void updateGridChrome();
    void alignSizeSlider(); // keep the slider under the grid's right edge, left of the metadata panel
    QSize thumbnailResolution() const;
    void requestThumbnail(QListWidgetItem *item, int priority);
    void applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail, bool pending);
    bool isImageFile(const QString &filePath) const;

    // Viewer: frames load in the background (FrameLoader); these show and step through them.
    enum CompareMode { CompareOff, CompareNoView, ComparePinned };
    void showFrame(const FrameLoader::Frame &frame, bool requested);
    void showGifFrame(const QImage &source, const QImage &display);
    void stepImage(int delta, bool wrap = false);
    void goToImage(int index);
    void prefetchAround();
    void togglePlayback();
    void stopPlayback();
    void playbackTick();
    bool handleViewerKey(QKeyEvent *event);
    void refreshCurrentDisplay();
    void updateScopes();
    void setCompareMode(CompareMode mode);
    void pinCurrentImage();
    void updateCompare();
    void updateImageInfo();
    void updatePixelReadout(const QPoint &pixel);
    void showViewerContextMenu(const QPoint &pos);
    void copyColorText(const QString &text, const QColor &color);
    void handlePathActivated(const QString &path);
    bool isViewerVisible() const;

    // Extras: channels, clipping, full screen, clipboard and Explorer.
    void setChannel(int channel, bool toggle);
    void setFullScreenMode(bool on);
    void copyImageToClipboard();
    void exportImageAs();
    void batchConvert(const QStringList &paths);
    // Batch Rename (F2): opens the dialog for the files and folders, then renames them in the
    // background. Each batch is kept so Undo Rename (Ctrl+Z) can put the names back.
    void batchRename(const QStringList &paths);
    void runRename(const QList<BatchRename::Move> &moves, bool undo);
    void copyPathToClipboard(const QString &path);
    void showInExplorer(const QString &path);
    // Adds "Pin to Quick access" or "Unpin from Quick access" for a folder to a context menu.
    void addPinAction(QMenu *menu, const QString &folderPath);
    void showGridContextMenu(const QPoint &pos);

    // File browser selection and the file clipboard (copy, cut and paste, also with Explorer).
    bool handleGridSelectionEvent(QObject *watched, QEvent *event);
    // Builds a selection in grid order with filtered-out rows omitted.
    QItemSelection visibleGridSelection(int first, int last) const;
    QStringList selectedPaths() const;
    void copySelectedFiles(bool cut);
    void pasteFiles(const QString &targetFolder);
    void finishFileJob();
    void updateEditActions();
    QString currentPath() const;
    void scheduleMetadata();
    void addRecentImage(const QString &filePath);
    void updateRecentImagesMenu();
    void loadMetadata();

    std::shared_ptr<ImageLoader> m_imageLoader;
    FrameLoader *m_frameLoader = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    QPointer<QDialog> m_updateDialog;
    PathBar *m_pathBar = nullptr;
    QLineEdit *m_gridSearch = nullptr;
    QCheckBox *m_imagesOnly = nullptr;
    int m_visibleGridItems = 0;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_pixelLabel = nullptr;
    QLabel *m_swatchLabel = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pinAction = nullptr;
    QActionGroup *m_compareGroup = nullptr;
    QActionGroup *m_channelGroup = nullptr;
    QAction *m_clippingAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_copyImageAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_batchConvertAction = nullptr;
    QThreadPool m_batchPool;
    std::atomic<bool> m_batchCanceled{false};
    bool m_batchRunning = false;
    QAction *m_batchRenameAction = nullptr;
    QAction *m_undoRenameAction = nullptr;
    QList<QList<BatchRename::Move>> m_renameHistory; // finished rename batches, newest last
    QAction *m_copyPathAction = nullptr;
    QAction *m_showInExplorerAction = nullptr;
    QAction *m_cutAction = nullptr;
    QAction *m_copyFilesAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_invertSelectionAction = nullptr;
    QPersistentModelIndex m_selectionAnchor;
    bool m_shiftSelecting = false;
    bool m_shiftPressTaken = false;
    FileOps::Job *m_fileJob = nullptr;
    QProgressDialog *m_fileJobProgress = nullptr;
    QTimer m_fileJobTimer;
    QString m_fileJobTarget;
    QStringList m_fileJobSourceFolders;
    QList<QWidget *> m_hiddenForFullScreen;
    MetadataPanel *m_metadataPanel = nullptr;
    QDockWidget *m_metadataDock = nullptr;
    QAction *m_metadataAction = nullptr;
    ScopesPanel *m_scopesPanel = nullptr;
    QDockWidget *m_scopesDock = nullptr;
    QAction *m_scopesAction = nullptr;
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
    QWidget *m_imagePage = nullptr;
    OverlayButton *m_closeImageButton = nullptr;
    OverlayButton *m_previousImageButton = nullptr;
    OverlayButton *m_nextImageButton = nullptr;
    QAction *m_toggleFoldersAction = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_upAction = nullptr;
    QActionGroup *m_gridViewGroup = nullptr;
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
    int m_thumbnailDisplaySize = 128;
    QSlider *m_thumbnailSizeSlider = nullptr;
    QWidget *m_sliderSpacer = nullptr;
    int m_statusBarReserve = -1; // room right of the status bar's last widget (size grip)
    GridView m_gridView = GridView::Icons;
    qreal m_wheelRemainder = 0.0; // part of a pixel left over from high-resolution wheels
    FileItemDelegate *m_fileDelegate = nullptr;
    QHeaderView *m_detailsHeader = nullptr;
    int m_currentImageIndex = -1;
};
