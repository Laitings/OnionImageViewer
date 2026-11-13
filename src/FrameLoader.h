#pragma once

#include "ColorTransform.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <memory>

class ImageLoader;

// Loads and renders images on background threads, keeps recently used frames in memory and
// reads ahead, so stepping or playing through a sequence doesn't wait on the disk each time.
// The newest request wins: when frames are asked for faster than they load, the ones in
// between are skipped instead of queueing up.
class FrameLoader final : public QObject
{
    Q_OBJECT

public:
    struct Frame
    {
        QString path;
        QImage source;  // as read (full precision), for the pixel readout and re-rendering
        QImage display; // rendered with the current view
        QString error;
    };

    explicit FrameLoader(std::shared_ptr<const ImageLoader> loader, QObject *parent = nullptr);
    ~FrameLoader() override;

    // Changes how frames are rendered; drops cached renders.
    void setRender(ColorTransform::View view, ColorTransform::Input input);

    // Ask for a frame. frameReady is emitted when it is available (right away if cached).
    void request(const QString &path);
    // Read these ahead at low priority (nearest first), as far as the memory budget allows.
    void prefetch(const QStringList &paths);
    bool isCached(const QString &path) const;
    void clear();

    // Play the displayed GIF independently of folder sequence playback.
    void startGif(const QString &path);
    void stopGif();

signals:
    // Emitted for every frame that finishes loading, not only the requested one, so a viewer
    // that is scrubbing can show frames as they arrive.
    void frameReady(const FrameLoader::Frame &frame, bool requested);
    void gifFrameReady(const QImage &source, const QImage &display);

private:
    class GifReader;
    struct GifFrame
    {
        QImage source;
        QImage display;
        int delay = 0;
        int renderGeneration = 0;
    };

    void requestGifFrame(const QString &path = {});
    void showGifFrame();
    void startJob(const QString &path, int priority);
    void finished(const Frame &frame, int generation);
    void store(const Frame &frame);
    static qint64 frameBytes(const Frame &frame);

    std::shared_ptr<const ImageLoader> m_loader;
    QThreadPool m_pool;
    ColorTransform::View m_view = ColorTransform::View::None;
    ColorTransform::Input m_input = ColorTransform::Input::Auto;
    int m_generation = 0;
    QString m_wanted;
    QSet<QString> m_pending;
    QHash<QString, Frame> m_cache;
    QStringList m_lru; // most recently used last
    qint64 m_cacheBytes = 0;
    qint64 m_budgetBytes = 0;
    QThread m_gifThread;
    GifReader *m_gifReader = nullptr;
    std::atomic<int> m_gifGeneration{0};
    QTimer m_gifTimer;
    GifFrame m_nextGifFrame;
};
