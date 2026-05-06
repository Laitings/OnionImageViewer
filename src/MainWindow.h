#pragma once

#include <QMainWindow>
#include <memory>

class QAction;
class FolderTreePanel;
class ImageLoader;
class ImageViewport;
class QLabel;
class QSplitter;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolbar();
    void createCentralLayout();
    void applyDarkPalette();
    void loadSettings();
    void saveSettings();
    void openImage();
    void setFolderPanelVisible(bool visible);
    void handleFolderSelected(const QString &folderPath);

    std::unique_ptr<ImageLoader> m_imageLoader;
    FolderTreePanel *m_folderTreePanel = nullptr;
    ImageViewport *m_imageViewport = nullptr;
    QSplitter *m_splitter = nullptr;
    QLabel *m_folderPathLabel = nullptr;
    QAction *m_toggleFoldersAction = nullptr;
    QAction *m_openImageAction = nullptr;
    QAction *m_fitToWindowAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
};
