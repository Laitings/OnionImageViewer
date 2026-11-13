#include "Scopes.h"

#include <QColor>
#include <QFloat16>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace Scopes {

namespace {

constexpr float LumaR = 0.2126f;
constexpr float LumaG = 0.7152f;
constexpr float LumaB = 0.0722f;
constexpr int MaxWaveformColumns = 512;
constexpr float MaxTop = 256.0f; // float images: the axis stops here, brighter values clip

bool isFastFormat(QImage::Format format)
{
    switch (format) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

// The smallest power of two at or above value (value > 1), capped at MaxTop.
float powerOfTwoAbove(float value)
{
    float top = 1.0f;
    while (top < value && top < MaxTop) {
        top *= 2.0f;
    }
    return top;
}

int binOf(float position, int count)
{
    return std::min(count - 1, std::max(0, int(position * float(count))));
}

QString stopLabel(int stop)
{
    if (stop >= 0) {
        return QString::number(1 << stop);
    }
    return QString::number(std::ldexp(1.0, stop), 'g', 2);
}

// Brightness 0..1 of a waveform cell from how many samples landed in it. Logarithmic, so single
// stray pixels still show while busy areas don't all saturate.
float shade(quint32 count, float logMax)
{
    if (count == 0 || logMax <= 0.0f) {
        return 0.0f;
    }
    return std::min(1.0f, 0.22f + 0.78f * std::log1p(float(count)) / logMax);
}

QRgb addColor(QRgb pixel, float amount, int r, int g, int b)
{
    return qRgb(std::min(255, qRed(pixel) + int(amount * float(r))),
                std::min(255, qGreen(pixel) + int(amount * float(g))),
                std::min(255, qBlue(pixel) + int(amount * float(b))));
}

} // namespace

Axis::Axis(Kind kind, float top)
    : m_kind(kind)
    , m_top(std::max(top, 1.0f))
{
    m_topStop = std::log2(m_top);
}

float Axis::position(float value) const
{
    float p = 0.0f;
    if (m_kind == Kind::Linear) {
        if (!(value > 0.0f)) {
            return 0.0f; // also NaN
        }
        p = value / m_top;
    } else {
        if (!(value > std::exp2(m_bottomStop))) {
            return 0.0f;
        }
        p = (std::log2(value) - m_bottomStop) / (m_topStop - m_bottomStop);
    }
    return std::min(1.0f, p);
}

QList<Axis::Tick> Axis::ticks() const
{
    QList<Tick> result;
    if (m_kind == Kind::Linear) {
        if (m_top <= 1.0f) {
            for (int i = 0; i <= 4; ++i) {
                result.append({float(i) / 4.0f, QString::number(i * 25), false});
            }
            return result;
        }
        result.append({0.0f, QStringLiteral("0"), false});
        result.append({position(0.5f), QString(), false});
        for (float v = 1.0f; v <= m_top; v *= 2.0f) {
            result.append({position(v), QString::number(double(v)), false});
        }
        return result;
    }
    const int top = int(std::lround(m_topStop));
    const int bottom = int(std::lround(m_bottomStop));
    for (int stop = bottom; stop <= top; ++stop) {
        const bool labelled = stop % 2 == 0 || stop == top;
        result.append({position(std::exp2(float(stop))), labelled ? stopLabel(stop) : QString(), false});
    }
    result.append({position(0.18f), QStringLiteral("18%"), true});
    return result;
}

void readPixel(const QImage &image, int x, int y, float rgb[3])
{
    const uchar *line = image.constScanLine(y);
    switch (image.format()) {
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied: {
        const QRgb p = reinterpret_cast<const QRgb *>(line)[x];
        rgb[0] = float(qRed(p)) / 255.0f;
        rgb[1] = float(qGreen(p)) / 255.0f;
        rgb[2] = float(qBlue(p)) / 255.0f;
        return;
    }
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied: {
        const uchar *p = line + x * 4;
        rgb[0] = float(p[0]) / 255.0f;
        rgb[1] = float(p[1]) / 255.0f;
        rgb[2] = float(p[2]) / 255.0f;
        return;
    }
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied: {
        const auto *p = reinterpret_cast<const quint16 *>(line) + x * 4;
        rgb[0] = float(p[0]) / 65535.0f;
        rgb[1] = float(p[1]) / 65535.0f;
        rgb[2] = float(p[2]) / 65535.0f;
        return;
    }
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied: {
        const auto *p = reinterpret_cast<const qfloat16 *>(line) + x * 4;
        rgb[0] = float(p[0]);
        rgb[1] = float(p[1]);
        rgb[2] = float(p[2]);
        return;
    }
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied: {
        const auto *p = reinterpret_cast<const float *>(line) + x * 4;
        rgb[0] = p[0];
        rgb[1] = p[1];
        rgb[2] = p[2];
        return;
    }
    default: {
        const QColor color = image.pixelColor(x, y);
        rgb[0] = float(color.redF());
        rgb[1] = float(color.greenF());
        rgb[2] = float(color.blueF());
        return;
    }
    }
}

bool isFloatImage(const QImage &image)
{
    switch (image.format()) {
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
        return true;
    default:
        return false;
    }
}

Result compute(const QImage &source, bool sceneLinear, int maxSamples)
{
    Result result;
    if (source.isNull() || maxSamples <= 0) {
        return result;
    }
    QImage image = source;
    if (!isFastFormat(image.format())) {
        image = image.convertToFormat(image.depth() > 32 ? QImage::Format_RGBA64 : QImage::Format_ARGB32);
    }

    const int width = image.width();
    const int height = image.height();
    const int step = std::max(1, int(std::ceil(std::sqrt(double(width) * double(height) / double(maxSamples)))));
    const int columns = (width + step - 1) / step;
    const int rows = (height + step - 1) / step;

    // First read the samples, then pick the axis from the brightest one.
    std::vector<float> samples(size_t(columns) * size_t(rows) * 3);
    float brightest = 0.0f;
    size_t at = 0;
    for (int row = 0; row < rows; ++row) {
        const int y = row * step;
        for (int column = 0; column < columns; ++column) {
            float *rgb = samples.data() + at;
            readPixel(image, column * step, y, rgb);
            for (int c = 0; c < 3; ++c) {
                if (std::isfinite(rgb[c]) && rgb[c] > brightest) {
                    brightest = rgb[c];
                }
            }
            at += 3;
        }
    }

    const bool floatImage = isFloatImage(image);
    const float top = floatImage && brightest > 1.0f ? powerOfTwoAbove(brightest) : 1.0f;
    result.axis = Axis(floatImage && sceneLinear ? Axis::Kind::Stops : Axis::Kind::Linear, top);

    const int waveColumns = std::min(columns, MaxWaveformColumns);
    for (auto &bins : result.histogram) {
        bins.assign(Bins, 0);
    }
    std::array<std::vector<quint32>, 4> wave;
    for (auto &cells : wave) {
        cells.assign(size_t(waveColumns) * Levels, 0);
    }

    at = 0;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float *rgb = samples.data() + at;
            at += 3;
            const size_t waveColumn = size_t(column) * size_t(waveColumns) / size_t(columns);
            const float values[4] = {rgb[0], rgb[1], rgb[2], LumaR * rgb[0] + LumaG * rgb[1] + LumaB * rgb[2]};
            for (int c = 0; c < 4; ++c) {
                const float position = result.axis.position(values[c]);
                ++result.histogram[size_t(c)][size_t(binOf(position, Bins))];
                ++wave[size_t(c)][waveColumn * Levels + size_t(binOf(position, Levels))];
            }
        }
    }

    // Shade the waveforms: one brightness scale for R, G and B, one for luma.
    quint32 rgbMax = 0;
    for (int c = 0; c < 3; ++c) {
        rgbMax = std::max(rgbMax, *std::max_element(wave[size_t(c)].begin(), wave[size_t(c)].end()));
    }
    const quint32 lumaMax = *std::max_element(wave[3].begin(), wave[3].end());
    const float rgbLogMax = std::log1p(float(rgbMax));
    const float lumaLogMax = std::log1p(float(lumaMax));

    struct Trace {
        int r, g, b;
    };
    static constexpr Trace Colors[3] = {{255, 45, 45}, {45, 235, 45}, {70, 110, 255}};

    result.lumaWaveform = QImage(waveColumns, Levels, QImage::Format_RGB32);
    result.rgbWaveform = QImage(waveColumns, Levels, QImage::Format_RGB32);
    result.parade = QImage(waveColumns * 3, Levels, QImage::Format_RGB32);
    result.lumaWaveform.fill(Qt::black);
    result.rgbWaveform.fill(Qt::black);
    result.parade.fill(Qt::black);
    for (int level = 0; level < Levels; ++level) {
        const int y = Levels - 1 - level;
        auto *luma = reinterpret_cast<QRgb *>(result.lumaWaveform.scanLine(y));
        auto *overlay = reinterpret_cast<QRgb *>(result.rgbWaveform.scanLine(y));
        auto *parade = reinterpret_cast<QRgb *>(result.parade.scanLine(y));
        for (int column = 0; column < waveColumns; ++column) {
            const size_t cell = size_t(column) * Levels + size_t(level);
            const float l = shade(wave[3][cell], lumaLogMax);
            luma[column] = qRgb(int(l * 205.0f), int(l * 255.0f), int(l * 205.0f));
            QRgb mixed = qRgb(0, 0, 0);
            for (int c = 0; c < 3; ++c) {
                const float s = shade(wave[size_t(c)][cell], rgbLogMax);
                const Trace &color = Colors[c];
                mixed = addColor(mixed, s, color.r, color.g, color.b);
                parade[c * waveColumns + column] = addColor(qRgb(0, 0, 0), s, color.r, color.g, color.b);
            }
            overlay[column] = mixed;
        }
    }

    result.sampleStep = step;
    result.valid = true;
    return result;
}

} // namespace Scopes
