#include "ColorTransform.h"

#include <QFloat16>
#include <QtMath>

#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace {

using ColorTransform::View;

// ---- Log curves ---------------------------------------------------------------------------

// ARRI LogC3, EI 800 (ALEXA classic, Mini, LF, 65). Code value 0..1 -> scene linear.
float logC3ToLinear(float t)
{
    constexpr float cut = 0.010591f;
    constexpr float a = 5.555556f;
    constexpr float b = 0.052272f;
    constexpr float c = 0.247190f;
    constexpr float d = 0.385537f;
    constexpr float e = 5.367655f;
    constexpr float f = 0.092809f;
    if (t > e * cut + f) {
        return (std::pow(10.0f, (t - d) / c) - b) / a;
    }
    return (t - f) / e;
}

// ARRI LogC4 (ALEXA 35). Code value 0..1 -> scene linear.
float logC4ToLinear(float t)
{
    static const double a = (std::pow(2.0, 18.0) - 16.0) / 117.45;
    static const double b = (1023.0 - 95.0) / 1023.0;
    static const double c = 95.0 / 1023.0;
    static const double s = (7.0 * std::log(2.0) * std::pow(2.0, 7.0 - 14.0 * c / b)) / (a * b);
    static const double tt = (std::pow(2.0, 14.0 * (-c / b) + 6.0) - 64.0) / a;
    if (t >= 0.0f) {
        return static_cast<float>((std::pow(2.0, 14.0 * (t - c) / b + 6.0) - 64.0) / a);
    }
    return static_cast<float>(t * s + tt);
}

// ---- Gamut ----------------------------------------------------------------------------------

using Matrix = std::array<float, 9>;

// Linear ALEXA Wide Gamut 3/4 -> linear Rec.709 primaries (both D65), via XYZ.
constexpr Matrix Awg3ToRec709 = {
    1.6172671f, -0.5372007f, -0.0802254f,
    -0.0705743f, 1.3346437f, -0.2640463f,
    -0.0211071f, -0.2270081f, 1.2483555f,
};
constexpr Matrix Awg4ToRec709 = {
    1.8928222f, -0.7807572f, -0.1122241f,
    -0.2057051f, 1.3402884f, -0.1345603f,
    -0.0127088f, -0.1522214f, 1.1651702f,
};
constexpr Matrix Identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};

// ---- Output ---------------------------------------------------------------------------------

float srgbEncode(float linear)
{
    if (!(linear > 0.0f)) {
        return 0.0f;
    }
    if (linear >= 1.0f) {
        return 1.0f;
    }
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// A simple filmic tone curve (Narkowicz's ACES fit) so highlights roll off instead of clipping.
// Scene mid grey 0.18 lands at about 41% on screen. An approximation of a camera's display
// rendering, not ARRI's own LUT.
float filmicTone(float linear)
{
    const float x = qMax(0.0f, linear) * 0.6f;
    return qBound(0.0f, (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 1.0f);
}

constexpr int OutputLutRange = 16; // linear values above this clip
constexpr int OutputLutSteps = 4096;
constexpr int OutputLutSize = OutputLutRange * OutputLutSteps + 1;

struct Tables
{
    std::vector<float> decode16;  // 16-bit integer code value -> linear
    std::vector<float> decodeHalf; // half-float bit pattern -> linear
    std::vector<uchar> output;    // linear (0..OutputLutRange) -> 8-bit display value
    Matrix matrix = Identity;
};

float halfBitsToFloat(quint16 bits)
{
    float value = 0.0f;
    qFloatFromFloat16(&value, reinterpret_cast<const qfloat16 *>(&bits), 1);
    return value;
}

std::shared_ptr<const Tables> buildTables(View view)
{
    auto tables = std::make_shared<Tables>();
    float (*decode)(float) = nullptr;
    switch (view) {
    case View::LogC3ToRec709:
        decode = logC3ToLinear;
        tables->matrix = Awg3ToRec709;
        break;
    case View::LogC4ToRec709:
        decode = logC4ToLinear;
        tables->matrix = Awg4ToRec709;
        break;
    case View::None:
        break;
    }

    tables->decode16.resize(65536);
    tables->decodeHalf.resize(65536);
    for (int i = 0; i < 65536; ++i) {
        const float code16 = static_cast<float>(i) / 65535.0f;
        const float codeHalf = halfBitsToFloat(static_cast<quint16>(i));
        tables->decode16[i] = decode ? decode(code16) : code16;
        tables->decodeHalf[i] = decode ? decode(std::isfinite(codeHalf) ? codeHalf : 0.0f) : codeHalf;
    }

    tables->output.resize(OutputLutSize);
    for (int i = 0; i < OutputLutSize; ++i) {
        const float linear = static_cast<float>(i) / OutputLutSteps;
        const float display = decode ? srgbEncode(filmicTone(linear)) : srgbEncode(linear);
        tables->output[i] = static_cast<uchar>(qBound(0, qRound(display * 255.0f), 255));
    }
    return tables;
}

const Tables &tablesFor(View view)
{
    static std::mutex mutex;
    static std::shared_ptr<const Tables> cache[3];
    std::lock_guard<std::mutex> lock(mutex);
    auto &slot = cache[static_cast<int>(view)];
    if (!slot) {
        slot = buildTables(view);
    }
    return *slot;
}

inline uchar outputValue(const Tables &tables, float linear)
{
    if (!(linear > 0.0f)) {
        return tables.output[0];
    }
    const int index = linear >= OutputLutRange ? OutputLutSize - 1 : static_cast<int>(linear * OutputLutSteps + 0.5f);
    return tables.output[static_cast<size_t>(qMin(index, OutputLutSize - 1))];
}

bool isFloatFormat(QImage::Format format)
{
    switch (format) {
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

} // namespace

namespace ColorTransform {

QString displayName(View view)
{
    switch (view) {
    case View::LogC3ToRec709:
        return QStringLiteral("ARRI LogC3 → Rec.709");
    case View::LogC4ToRec709:
        return QStringLiteral("ARRI LogC4 → Rec.709");
    case View::None:
        break;
    }
    return QStringLiteral("None (as stored)");
}

QString settingsKey(View view)
{
    switch (view) {
    case View::LogC3ToRec709:
        return QStringLiteral("logc3-rec709");
    case View::LogC4ToRec709:
        return QStringLiteral("logc4-rec709");
    case View::None:
        break;
    }
    return QStringLiteral("none");
}

View fromSettingsKey(const QString &key)
{
    for (View view : {View::LogC3ToRec709, View::LogC4ToRec709}) {
        if (key == settingsKey(view)) {
            return view;
        }
    }
    return View::None;
}

QImage toDisplay(const QImage &source, View view)
{
    if (source.isNull()) {
        return source;
    }

    const bool floatSource = isFloatFormat(source.format());

    // Nothing to do for ordinary 8-bit or integer images shown as stored.
    if (view == View::None && !floatSource) {
        return source.hasAlphaChannel() ? source.convertToFormat(QImage::Format_ARGB32)
                                        : source.convertToFormat(QImage::Format_RGB32);
    }

    const Tables &tables = tablesFor(view);
    const Matrix &m = tables.matrix;
    const int width = source.width();
    const int height = source.height();
    QImage result(width, height, QImage::Format_RGB32);
    if (result.isNull()) {
        return result;
    }

    // Work from 16-bit values: half floats keep their bit patterns, integers are widened to 16 bit.
    const QImage input = floatSource ? source.convertToFormat(QImage::Format_RGBX16FPx4)
                                     : source.convertToFormat(QImage::Format_RGBX64);
    const std::vector<float> &decode = floatSource ? tables.decodeHalf : tables.decode16;

    for (int y = 0; y < height; ++y) {
        const auto *in = reinterpret_cast<const quint16 *>(input.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const float r = decode[in[x * 4 + 0]];
            const float g = decode[in[x * 4 + 1]];
            const float b = decode[in[x * 4 + 2]];
            const float r2 = m[0] * r + m[1] * g + m[2] * b;
            const float g2 = m[3] * r + m[4] * g + m[5] * b;
            const float b2 = m[6] * r + m[7] * g + m[8] * b;
            out[x] = qRgb(outputValue(tables, r2), outputValue(tables, g2), outputValue(tables, b2));
        }
    }
    return result;
}

} // namespace ColorTransform
