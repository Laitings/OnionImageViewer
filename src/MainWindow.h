#pragma once

#include "ColorTransform.h"

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
class QSplitter;
class QStackedWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    void showAbout();
    void updateColorButton();
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

    std::unique_ptr<ImageLoader> m_imageLoader;
    FolderTreePanel *m_folderTreePanel = nullptr;
    QListWidget *m_fileGrid = nullptr;
    ImageViewport *m_imageViewport = nullptr;
    QStackedWidget *m_contentStack = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_folderPathLabel = nullptr;
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
    QMenu *m_recentLutsMenu = nullptr;
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
