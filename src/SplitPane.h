#pragma once

#include "ColorTransform.h"
#include "FrameLoader.h"

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

class ImageLoader;
class ImageViewport;
class PathBar;
class QKeyEvent;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QToolButton;

// The second pane of the split viewer. It has its own folder grid (with a path bar and an Up
// button) to pick an image independently of the main viewer, and its own viewport and frame
// loader to show it. MainWindow decides which pane is active, routes keys to it and keeps the
// color view, channels and (optionally) zoom and pan in step between the two panes.
class SplitPane final : public QWidget
{
    Q_OBJECT

public:
    // Returns a thumbnail for an image file, from the cache or the shell where possible (see
    // loadThumbnailImage in MainWindow.cpp). Runs on a background thread.
    using ThumbnailFunction = std::function<QImage(const QString &filePath, ColorTransform::View view,
                                                   ColorTransform::Input input, const QSize &size, bool *pending)>;
    // Whether the viewer can open a file (by its extension; never reads the file).
    using ImageFilter = std::function<bool(const QString &filePath)>;

    SplitPane(std::shared_ptr<const ImageLoader> loader, ThumbnailFunction thumbnails, ImageFilter isImage,
              QWidget *parent = nullptr);
    ~SplitPane() override;

    ImageViewport *viewport() const { return m_viewport; }

    // Lists a folder in the pane's grid and shows the grid. selectPath, if listed, is selected.
    void browse(const QString &folderPath, const QString &selectPath = QString());
    QString folderPath() const { return m_folderPath; }
    // Shows the grid again; the image stays loaded so Esc in the grid returns to it.
    void showBrowser();
    bool isBrowsing() const;

    // Color view and input for the image and the thumbnails (follows the main window).
    void setRender(ColorTransform::View view, ColorTransform::Input input);

    // The shown image: as read, and as displayed. Null while browsing before any image.
    QString currentPath() const { return m_currentPath; }
    // The file the pane is about: the image under the cursor in its grid while browsing,
    // otherwise the shown image. Used for the metadata panel, Copy Path and Show in Explorer.
    QString selectedPath() const;
    const QImage &sourceImage() const { return m_source; }
    const QImage &displayImage() const { return m_display; }
    // One line for the status bar: name, size, position in the folder (or the folder's count).
    QString infoText() const;

    // Marks the pane as the one keys go to, with an accent frame and header.
    void setActive(bool active);
    // Gives keyboard focus to the grid or the viewport, whichever is shown.
    void focusContent();

    // Pane keys: in the viewer Left/Right (Shift: 10), Home/End and Esc (back to the grid);
    // in the grid Enter (open), Backspace (up) and Esc (back to the image). Returns true when used.
    bool handleKey(QKeyEvent *event);

signals:
    // A new image is shown, or the pane switched between its grid and its viewer.
    void contentChanged();
    // The current item of the pane's grid changed.
    void selectionChanged();
    // A newly opened image is on screen (not when stepping with the arrow keys).
    void imageOpened();
    void closeRequested();

private:
    void openItem(QListWidgetItem *item);
    void goUp();
    void showImage(int index, bool fresh);
    void showFrame(const FrameLoader::Frame &frame, bool requested);
    void scheduleVisibleThumbnails();
    void requestThumbnail(QListWidgetItem *item);
    void applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail);
    void resetThumbnails();
    void updateHeader();

    FrameLoader *m_frameLoader = nullptr;
    ThumbnailFunction m_thumbnails;
    ImageFilter m_isImage;
    QWidget *m_header = nullptr;
    PathBar *m_pathBar = nullptr;
    QToolButton *m_upButton = nullptr;
    QToolButton *m_browseButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_grid = nullptr;
    ImageViewport *m_viewport = nullptr;
    QIcon m_folderIcon;
    QIcon m_fileIcon;

    QString m_folderPath;
    QStringList m_imagePaths;
    QHash<QString, QListWidgetItem *> m_items;
    int m_index = -1;
    bool m_freshOpen = false;
    QString m_currentPath;
    QImage m_source;
    QImage m_display;
    bool m_active = false;
    ColorTransform::View m_view = ColorTransform::View::None;
    ColorTransform::Input m_input = ColorTransform::Input::Auto;

    QThreadPool m_thumbnailPool;
    QTimer m_thumbnailTimer;
    QSet<QString> m_requested;
    int m_thumbnailGeneration = 0;
};
