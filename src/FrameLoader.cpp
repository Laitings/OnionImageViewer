#include "FrameLoader.h"

#include "ImageLoader.h"
#include "WebpReader.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>

#include <utility>

namespace {
constexpr int PriorityRequested = 10;
constexpr int PriorityPrefetch = 0;
constexpr int MaxPrefetchJobs = 3;
}

// The reader stays on its worker thread. Reopening it (or rewinding) at the end of a pass lets
// long animations loop without keeping every decoded frame in memory. Animated WebP goes through
// libwebp when the app has it, otherwise through Qt's WebP plugin like GIFs.
class FrameLoader::GifReader final : public QObject
{
public:
    QImage read(const QString &path)
    {
        if (!path.isEmpty()) {
            reset();
            if (Webp::isWebpFile(path)) {
                auto webp = std::make_unique<Webp::Animation>();
                if (!webp->open(path)) {
                    return {};
                }
                m_repeats = webp->repeats();
                m_webp = std::move(webp);
            } else {
                m_format = QFileInfo(path).suffix().toLower().toLatin1();
                m_reader = std::make_unique<QImageReader>(path, m_format);
                if (m_reader->imageCount() < 2) {
                    reset();
                    return {};
                }
                m_repeats = m_reader->loopCount();
            }
        }
        QImage image;
        if (m_webp) {
            if (!m_webp->hasMoreFrames() && m_repeats != 0) {
                if (m_repeats > 0) {
                    --m_repeats;
                }
                m_webp->rewind();
            }
            image = m_webp->next(&m_delay);
        } else if (m_reader) {
            if (!m_reader->canRead() && m_repeats != 0) {
                if (m_repeats > 0) {
                    --m_repeats;
                }
                m_reader = std::make_unique<QImageReader>(m_reader->fileName(), m_format);
            }
            image = m_reader->read();
            m_delay = m_reader->nextImageDelay();
        }
        if (image.isNull()) {
            reset();
        }
        return image;
    }

    int delay() const { return m_delay; }
    void reset()
    {
        m_reader.reset();
        m_webp.reset();
    }

private:
    std::unique_ptr<QImageReader> m_reader;
    std::unique_ptr<Webp::Animation> m_webp;
    QByteArray m_format;
    int m_repeats = 0;
    int m_delay = 0;
};

bool FrameLoader::mayBeAnimated(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.compare(QLatin1String("gif"), Qt::CaseInsensitive) == 0
        || suffix.compare(QLatin1String("webp"), Qt::CaseInsensitive) == 0;
}

FrameLoader::FrameLoader(std::shared_ptr<const ImageLoader> loader, QObject *parent)
    : QObject(parent)
    , m_loader(std::move(loader))
{
    qRegisterMetaType<FrameLoader::Frame>("FrameLoader::Frame");
    m_pool.setMaxThreadCount(qBound(2, QThread::idealThreadCount() / 4, 4));
    // Enough for a handful of 4.6K half-float frames (source + display ~175 MB each), or a
    // few dozen 2K DPX frames.
    m_budgetBytes = qint64(sizeof(void *) == 8 ? 1536 : 384) * 1024 * 1024;
    m_gifTimer.setSingleShot(true);
    m_gifTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_gifTimer, &QTimer::timeout, this, &FrameLoader::showGifFrame);
}

FrameLoader::~FrameLoader()
{
    stopGif();
    m_gifThread.quit();
    m_gifThread.wait();
    m_pool.clear();
    m_pool.waitForDone();
}

void FrameLoader::startGif(const QString &path)
{
    stopGif();
    if (!m_gifReader) {
        m_gifReader = new GifReader;
        m_gifReader->moveToThread(&m_gifThread);
        connect(&m_gifThread, &QThread::finished, m_gifReader, &QObject::deleteLater);
        m_gifThread.start();
    }
    requestGifFrame(path);
}

void FrameLoader::stopGif()
{
    ++m_gifGeneration;
    m_gifTimer.stop();
    m_nextGifFrame = {};
    if (m_gifReader) {
        QMetaObject::invokeMethod(m_gifReader, &GifReader::reset, Qt::QueuedConnection);
    }
}

void FrameLoader::requestGifFrame(const QString &path)
{
    const int generation = m_gifGeneration.load();
    const int renderGeneration = m_generation;
    const auto view = m_view;
    const auto input = m_input;
    QMetaObject::invokeMethod(m_gifReader, [this, path, generation, renderGeneration, view, input]() {
        if (generation != m_gifGeneration.load()) {
            return;
        }
        QImage source = m_gifReader->read(path);
        if (source.isNull() || generation != m_gifGeneration.load()) {
            return;
        }
        const GifFrame frame{source, ColorTransform::toDisplay(source, view, input),
                             m_gifReader->delay(), renderGeneration};
        QMetaObject::invokeMethod(this, [this, frame, generation]() {
            if (generation == m_gifGeneration.load()) {
                m_nextGifFrame = frame;
                if (!m_gifTimer.isActive()) {
                    showGifFrame();
                }
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void FrameLoader::showGifFrame()
{
    if (m_nextGifFrame.source.isNull()) {
        return;
    }
    GifFrame frame = std::exchange(m_nextGifFrame, {});
    if (frame.renderGeneration != m_generation) {
        frame.display = ColorTransform::toDisplay(frame.source, m_view, m_input);
    }
    // Decode one frame ahead so decoding time usually fits inside the current frame's delay.
    m_gifTimer.start(qMax(1, frame.delay));
    requestGifFrame();
    emit gifFrameReady(frame.source, frame.display);
}

void FrameLoader::setRender(ColorTransform::View view, ColorTransform::Input input)
{
    if (view == m_view && input == m_input) {
        return;
    }
    m_view = view;
    m_input = input;
    clear();
}

void FrameLoader::clear()
{
    ++m_generation;
    m_pool.clear();
    m_pending.clear();
    m_cache.clear();
    m_lru.clear();
    m_cacheBytes = 0;
    m_wanted.clear();
}

bool FrameLoader::isCached(const QString &path) const
{
    return m_cache.contains(path);
}

void FrameLoader::request(const QString &path)
{
    m_wanted = path;
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd()) {
        m_lru.removeOne(path);
        m_lru.append(path);
        emit frameReady(*it, true);
        return;
    }
    // Only start it now if the workers aren't all busy; otherwise the next finishing job picks
    // up whatever is wanted by then, which skips frames during fast scrubbing.
    if (!m_pending.contains(path) && m_pending.size() < m_pool.maxThreadCount()) {
        startJob(path, PriorityRequested);
    }
}

void FrameLoader::prefetch(const QStringList &paths)
{
    qint64 plannedBytes = m_cacheBytes;
    const qint64 typical = m_cache.isEmpty() ? 0 : m_cacheBytes / m_cache.size();
    for (const QString &path : paths) {
        if (m_pending.size() >= MaxPrefetchJobs) {
            break;
        }
        if (m_cache.contains(path) || m_pending.contains(path)) {
            continue;
        }
        plannedBytes += typical;
        if (plannedBytes > m_budgetBytes * 3 / 4) {
            break; // keep room so read-ahead doesn't push out the frames around the current one
        }
        startJob(path, PriorityPrefetch);
    }
}

void FrameLoader::startJob(const QString &path, int priority)
{
    m_pending.insert(path);
    const int generation = m_generation;
    const ColorTransform::View view = m_view;
    const ColorTransform::Input input = m_input;
    std::shared_ptr<const ImageLoader> loader = m_loader;
    m_pool.start([this, loader, path, view, input, generation]() {
        Frame frame;
        frame.path = path;
        const ImageLoader::Result result = loader->load(path);
        if (result.isValid()) {
            frame.source = result.image;
            frame.display = ColorTransform::toDisplay(result.image, view, input);
        } else {
            frame.error = result.error.isEmpty() ? QStringLiteral("Could not read the image.") : result.error;
        }
        QMetaObject::invokeMethod(this, [this, frame, generation]() {
            finished(frame, generation);
        }, Qt::QueuedConnection);
    }, priority);
}

void FrameLoader::finished(const Frame &frame, int generation)
{
    if (generation != m_generation) {
        return;
    }
    m_pending.remove(frame.path);
    if (frame.error.isEmpty()) {
        store(frame);
    }
    const bool requested = frame.path == m_wanted;
    emit frameReady(frame, requested);

    // A newer frame was asked for while the workers were busy: start it now.
    if (!m_wanted.isEmpty() && !m_cache.contains(m_wanted) && !m_pending.contains(m_wanted)) {
        startJob(m_wanted, PriorityRequested);
    }
}

qint64 FrameLoader::frameBytes(const Frame &frame)
{
    return frame.source.sizeInBytes() + frame.display.sizeInBytes();
}

void FrameLoader::store(const Frame &frame)
{
    if (m_cache.contains(frame.path)) {
        return;
    }
    m_cache.insert(frame.path, frame);
    m_lru.append(frame.path);
    m_cacheBytes += frameBytes(frame);
    // Drop the least recently used frames, but never the wanted one.
    while (m_cacheBytes > m_budgetBytes && m_lru.size() > 1) {
        const QString oldest = m_lru.first() == m_wanted ? m_lru.value(1) : m_lru.first();
        m_lru.removeOne(oldest);
        m_cacheBytes -= frameBytes(m_cache.value(oldest));
        m_cache.remove(oldest);
    }
}
