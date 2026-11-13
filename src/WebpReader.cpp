#include "WebpReader.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>

#ifdef ONION_HAVE_LIBWEBP
#include <webp/decode.h>
#include <webp/demux.h>
#endif

namespace {

#ifdef ONION_HAVE_LIBWEBP

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QByteArray readFile(const QString &filePath, QString *error, qint64 maxBytes = -1)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return {};
    }
    QByteArray data = maxBytes < 0 ? file.readAll() : file.read(maxBytes);
    if (data.isEmpty()) {
        setError(error, QStringLiteral("The file is empty"));
    }
    return data;
}

WebPData webpData(const QByteArray &data)
{
    WebPData result;
    result.bytes = reinterpret_cast<const uint8_t *>(data.constData());
    result.size = size_t(data.size());
    return result;
}

// Copies one metadata chunk ("ICCP", "EXIF") out of the file; empty when it has none.
QByteArray chunk(const QByteArray &data, const char *fourcc)
{
    const WebPData raw = webpData(data);
    WebPDemuxer *demux = WebPDemux(&raw);
    if (!demux) {
        return {};
    }
    QByteArray result;
    WebPChunkIterator it;
    if (WebPDemuxGetChunk(demux, fourcc, 1, &it)) {
        result = QByteArray(reinterpret_cast<const char *>(it.chunk.bytes), qsizetype(it.chunk.size));
        WebPDemuxReleaseChunkIterator(&it);
    }
    WebPDemuxDelete(demux);
    return result;
}

// The embedded ICC profile, or an invalid colour space when there is none (or it is sRGB).
QColorSpace profileOf(const QByteArray &data)
{
    const QByteArray icc = chunk(data, "ICCP");
    if (icc.isEmpty()) {
        return {};
    }
    const QColorSpace space = QColorSpace::fromIccProfile(icc);
    return space.isValid() && space != QColorSpace(QColorSpace::SRgb) ? space : QColorSpace();
}

// Converts to sRGB like the rest of the display pipeline expects.
void applyProfile(QImage &image, const QColorSpace &profile)
{
    if (profile.isValid()) {
        image.setColorSpace(profile);
        image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    }
}

QImage::Format formatFor(bool alpha)
{
    return alpha ? QImage::Format_RGBA8888 : QImage::Format_RGBX8888;
}

QImage decodeStill(const QByteArray &data, const WebPBitstreamFeatures &features, const QSize &targetSize, QString *error)
{
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) {
        setError(error, QStringLiteral("libwebp version mismatch"));
        return {};
    }
    QSize size(features.width, features.height);
    if (targetSize.isValid() && (size.width() > targetSize.width() || size.height() > targetSize.height())) {
        size = size.scaled(targetSize, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        config.options.use_scaling = 1;
        config.options.scaled_width = size.width();
        config.options.scaled_height = size.height();
    }
    config.options.use_threads = 1;

    QImage image(size, formatFor(features.has_alpha));
    if (image.isNull()) {
        setError(error, QStringLiteral("Not enough memory for a %1 × %2 image").arg(size.width()).arg(size.height()));
        return {};
    }
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = image.bits();
    config.output.u.RGBA.stride = int(image.bytesPerLine());
    config.output.u.RGBA.size = size_t(image.sizeInBytes());

    const VP8StatusCode status = WebPDecode(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), &config);
    WebPFreeDecBuffer(&config.output);
    if (status != VP8_STATUS_OK) {
        setError(error, QStringLiteral("WebP decoding failed (error %1)").arg(int(status)));
        return {};
    }
    return image;
}

QImage canvasImage(const uint8_t *pixels, const WebPAnimInfo &info)
{
    // The decoder owns the canvas and reuses it for the next frame, so copy it.
    return QImage(pixels, int(info.canvas_width), int(info.canvas_height), int(info.canvas_width) * 4,
                  QImage::Format_RGBA8888).copy();
}

#endif // ONION_HAVE_LIBWEBP

} // namespace

namespace Webp {

bool isAvailable()
{
#ifdef ONION_HAVE_LIBWEBP
    return true;
#else
    return false;
#endif
}

bool isWebpFile(const QString &filePath)
{
    return isAvailable() && QFileInfo(filePath).suffix().compare(QLatin1String("webp"), Qt::CaseInsensitive) == 0;
}

QImage read(const QString &filePath, QString *error, const QSize &targetSize)
{
#ifdef ONION_HAVE_LIBWEBP
    const QByteArray data = readFile(filePath, error);
    if (data.isEmpty()) {
        return {};
    }
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), &features) != VP8_STATUS_OK) {
        setError(error, QStringLiteral("Not a valid WebP file"));
        return {};
    }

    QImage image;
    if (features.has_animation) {
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = MODE_RGBA;
        options.use_threads = 1;
        const WebPData raw = webpData(data);
        WebPAnimDecoder *decoder = WebPAnimDecoderNew(&raw, &options);
        WebPAnimInfo info;
        uint8_t *pixels = nullptr;
        int timestamp = 0;
        if (decoder && WebPAnimDecoderGetInfo(decoder, &info) && WebPAnimDecoderGetNext(decoder, &pixels, &timestamp)) {
            image = canvasImage(pixels, info);
        }
        WebPAnimDecoderDelete(decoder);
        if (image.isNull()) {
            setError(error, QStringLiteral("WebP animation decoding failed"));
            return {};
        }
        if (targetSize.isValid() && (image.width() > targetSize.width() || image.height() > targetSize.height())) {
            image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    } else {
        image = decodeStill(data, features, targetSize, error);
    }
    if (!image.isNull()) {
        applyProfile(image, profileOf(data));
    }
    return image;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(targetSize);
    if (error) {
        *error = QStringLiteral("Built without libwebp");
    }
    return {};
#endif
}

QList<QPair<QString, QString>> describe(const QString &filePath, QString *error)
{
    QList<QPair<QString, QString>> facts;
#ifdef ONION_HAVE_LIBWEBP
    const QByteArray data = readFile(filePath, error);
    if (data.isEmpty()) {
        return facts;
    }
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()), &features) != VP8_STATUS_OK) {
        setError(error, QStringLiteral("Not a valid WebP file"));
        return facts;
    }
    const WebPData raw = webpData(data);
    WebPDemuxer *demux = WebPDemux(&raw);
    const int width = demux ? int(WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH)) : features.width;
    const int height = demux ? int(WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT)) : features.height;
    facts.append({QStringLiteral("Size"), QStringLiteral("%1 × %2").arg(width).arg(height)});
    static const char *const compression[] = {"Mixed", "Lossy", "Lossless"};
    facts.append({QStringLiteral("Compression"),
                  QString::fromLatin1(features.format >= 0 && features.format <= 2 ? compression[features.format] : "Unknown")});
    facts.append({QStringLiteral("Alpha"), features.has_alpha ? QStringLiteral("Yes") : QStringLiteral("No")});
    if (features.has_animation && demux) {
        facts.append({QStringLiteral("Frames"), QString::number(WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT))});
        const uint32_t loops = WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT);
        facts.append({QStringLiteral("Loop count"), loops == 0 ? QStringLiteral("Forever") : QString::number(loops)});
        WebPIterator it;
        int duration = 0;
        if (WebPDemuxGetFrame(demux, 1, &it)) {
            do {
                duration += it.duration;
            } while (WebPDemuxNextFrame(&it));
            WebPDemuxReleaseIterator(&it);
        }
        facts.append({QStringLiteral("Duration"), QStringLiteral("%1 s").arg(duration / 1000.0, 0, 'f', 2)});
    }
    if (demux) {
        WebPDemuxDelete(demux);
    }
    const QByteArray icc = chunk(data, "ICCP");
    if (!icc.isEmpty()) {
        const QColorSpace space = QColorSpace::fromIccProfile(icc);
        const QString name = space.isValid() ? space.description() : QString();
        facts.append({QStringLiteral("Colour profile"), name.isEmpty() ? QStringLiteral("Embedded ICC profile") : name});
    }
#else
    Q_UNUSED(filePath);
    if (error) {
        *error = QStringLiteral("Built without libwebp");
    }
#endif
    return facts;
}

QByteArray exifData(const QString &filePath)
{
#ifdef ONION_HAVE_LIBWEBP
    const QByteArray data = readFile(filePath, nullptr);
    if (data.isEmpty()) {
        return {};
    }
    QByteArray exif = chunk(data, "EXIF");
    // Some writers keep the JPEG-style "Exif\0\0" prefix.
    if (exif.startsWith(QByteArrayLiteral("Exif\0\0"))) {
        exif.remove(0, 6);
    }
    return exif;
#else
    Q_UNUSED(filePath);
    return {};
#endif
}

struct Animation::Private
{
#ifdef ONION_HAVE_LIBWEBP
    ~Private() { WebPAnimDecoderDelete(decoder); }

    QByteArray data; // the decoder points into it
    WebPAnimDecoder *decoder = nullptr;
    WebPAnimInfo info{};
    QColorSpace profile;
    int lastTimestamp = 0;
#endif
};

Animation::Animation() = default;
Animation::~Animation() = default;

bool Animation::open(const QString &filePath)
{
    d.reset();
#ifdef ONION_HAVE_LIBWEBP
    if (!isWebpFile(filePath)) {
        return false;
    }
    // The VP8X header at the start says whether the file is animated; skip still images without
    // reading them in full.
    const QByteArray head = readFile(filePath, nullptr, 64);
    WebPBitstreamFeatures features;
    if (head.isEmpty()
        || WebPGetFeatures(reinterpret_cast<const uint8_t *>(head.constData()), size_t(head.size()), &features) != VP8_STATUS_OK
        || !features.has_animation) {
        return false;
    }
    auto p = std::make_unique<Private>();
    p->data = readFile(filePath, nullptr);
    if (p->data.isEmpty()) {
        return false;
    }
    WebPAnimDecoderOptions options;
    WebPAnimDecoderOptionsInit(&options);
    options.color_mode = MODE_RGBA;
    options.use_threads = 1;
    const WebPData raw = webpData(p->data);
    p->decoder = WebPAnimDecoderNew(&raw, &options);
    if (!p->decoder || !WebPAnimDecoderGetInfo(p->decoder, &p->info) || p->info.frame_count < 2) {
        return false;
    }
    p->profile = profileOf(p->data);
    d = std::move(p);
    return true;
#else
    Q_UNUSED(filePath);
    return false;
#endif
}

int Animation::repeats() const
{
#ifdef ONION_HAVE_LIBWEBP
    // WebP counts plays (0 = forever); callers want the repeats after the first play.
    if (d) {
        return d->info.loop_count == 0 ? -1 : int(d->info.loop_count) - 1;
    }
#endif
    return 0;
}

bool Animation::hasMoreFrames() const
{
#ifdef ONION_HAVE_LIBWEBP
    return d && WebPAnimDecoderHasMoreFrames(d->decoder);
#else
    return false;
#endif
}

void Animation::rewind()
{
#ifdef ONION_HAVE_LIBWEBP
    if (d) {
        WebPAnimDecoderReset(d->decoder);
        d->lastTimestamp = 0;
    }
#endif
}

QImage Animation::next(int *delayMs)
{
#ifdef ONION_HAVE_LIBWEBP
    uint8_t *pixels = nullptr;
    int timestamp = 0;
    if (!d || !WebPAnimDecoderGetNext(d->decoder, &pixels, &timestamp)) {
        return {};
    }
    if (delayMs) {
        *delayMs = timestamp - d->lastTimestamp;
    }
    d->lastTimestamp = timestamp;
    QImage image = canvasImage(pixels, d->info);
    applyProfile(image, d->profile);
    return image;
#else
    Q_UNUSED(delayMs);
    return {};
#endif
}

} // namespace Webp
