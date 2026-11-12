#include "ColorTransform.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFloat16>
#include <QList>
#include <QStringList>
#include <QtMath>

#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
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

// Scene linear -> ARRI LogC3 (EI 800) code value.
float linearToLogC3(float x)
{
    constexpr float cut = 0.010591f;
    constexpr float a = 5.555556f;
    constexpr float b = 0.052272f;
    constexpr float c = 0.247190f;
    constexpr float d = 0.385537f;
    constexpr float e = 5.367655f;
    constexpr float f = 0.092809f;
    return x > cut ? c * std::log10(a * x + b) + d : e * x + f;
}

// Scene linear -> ARRI LogC4 code value.
float linearToLogC4(float x)
{
    static const float a = static_cast<float>((std::pow(2.0, 18.0) - 16.0) / 117.45);
    static const float b = static_cast<float>((1023.0 - 95.0) / 1023.0);
    static const float c = static_cast<float>(95.0 / 1023.0);
    static const float s = static_cast<float>((7.0 * std::log(2.0) * std::pow(2.0, 7.0 - 14.0 * (95.0 / 1023.0) / ((1023.0 - 95.0) / 1023.0)))
                                              / (((std::pow(2.0, 18.0) - 16.0) / 117.45) * ((1023.0 - 95.0) / 1023.0)));
    static const float t = static_cast<float>((std::pow(2.0, 14.0 * (-(95.0 / 1023.0) / ((1023.0 - 95.0) / 1023.0)) + 6.0) - 64.0)
                                              / ((std::pow(2.0, 18.0) - 16.0) / 117.45));
    if (x >= t) {
        return (std::log2(qMax(a * x + 64.0f, 1e-6f)) - 6.0f) / 14.0f * b + c;
    }
    return (x - t) / s + c;
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

// ACES (AP0 / AP1, ACES white) -> AWG4 / AWG3 / Rec.709 (D65). AP0 -> AWG4 is the matrix from
// the ARRI/VFX "AP0 Linear to ARRI LogC4" DCTL; the others are derived from the primaries with a
// CAT02 white point adaptation.
constexpr Matrix Ap0ToAwg4 = {
    1.328523584f, -0.190906551f, -0.137617033f,
    -0.001087250f, 0.992641938f, 0.008445313f,
    0.000664054f, 0.001132192f, 0.998203754f,
};
constexpr Matrix Ap0ToAwg3 = {
    1.515986008f, -0.361341757f, -0.154644251f,
    -0.128327275f, 1.019314554f, 0.109012721f,
    -0.010510203f, 0.060833202f, 0.949677001f,
};
constexpr Matrix Ap0ToRec709 = {
    2.521934730f, -1.137023897f, -0.384910833f,
    -0.275479428f, 1.369828979f, -0.094349551f,
    -0.015982870f, -0.147789234f, 1.163772104f,
};
constexpr Matrix Ap1ToAwg4 = {
    0.918387668f, 0.022549182f, 0.059063150f,
    0.043675423f, 0.853378761f, 0.102945816f,
    -0.005022625f, 0.004758976f, 1.000263649f,
};
constexpr Matrix Ap1ToAwg3 = {
    1.038964267f, -0.097990613f, 0.059026346f,
    -0.044188132f, 0.858661168f, 0.185526964f,
    -0.009832151f, 0.054640635f, 0.955191516f,
};
constexpr Matrix Ap1ToRec709 = {
    1.705079644f, -0.624233461f, -0.080846182f,
    -0.129700530f, 1.138468547f, -0.008768016f,
    -0.024166345f, -0.124614158f, 1.148780503f,
};

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

// Built-in display renderings, used when ARRI's own LUTs are not available. Each is a smooth
// curve on the camera's log code value (two blended logistic steps, normalised to 0..1) fitted to
// the neutral axis of ARRI's v1 "Gamma24_Rec709" LUTs; the output is display-encoded (gamma 2.4)
// like those LUTs. Colours are not modelled exactly: ARRI's gamut mapping is more involved.
struct ToneCurve
{
    float k1, t1, g, k2, t2, w;
};

float toneCurve(const ToneCurve &p, float code)
{
    auto raw = [&p](float t) {
        return p.w * std::pow(1.0f + std::exp(-p.k1 * (t - p.t1)), -p.g)
            + (1.0f - p.w) / (1.0f + std::exp(-p.k2 * (t - p.t2)));
    };
    const float low = raw(0.0f);
    const float high = raw(1.0f);
    return qBound(0.0f, (raw(code) - low) / (high - low), 1.0f);
}

constexpr ToneCurve LogC3Curve = {8.225126f, 0.2402257f, 2.3298827f, 14.5402211f, 0.5827083f, 0.6954402f};
constexpr ToneCurve LogC4Curve = {9.7833035f, 0.2651433f, 2.6555779f, 25.6883184f, 0.1928446f, 0.6973401f};

float logC3Tone(float linear)
{
    return toneCurve(LogC3Curve, linearToLogC3(qMax(0.0f, linear)));
}

float logC4Tone(float linear)
{
    return toneCurve(LogC4Curve, linearToLogC4(qMax(0.0f, linear)));
}

// The output table is indexed by sqrt(linear / range): fine steps in the shadows, and enough
// range for camera highlights (LogC4 reaches about 470 linear).
constexpr float OutputLutRange = 1024.0f; // linear values above this clip
constexpr int OutputLutSize = 65536;

struct Tables
{
    std::vector<float> decode16;   // 16-bit integer code value -> linear
    std::vector<float> decodeHalf; // half-float bit pattern -> linear
    std::vector<uchar> output;     // sqrt(linear / OutputLutRange) -> 8-bit display value
    Matrix matrix = Identity;
    float saturation = 1.0f;
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
    float (*tone)(float) = srgbEncode;
    switch (view) {
    case View::LogC3ToRec709:
        decode = logC3ToLinear;
        tone = logC3Tone;
        tables->matrix = Awg3ToRec709;
        tables->saturation = 0.9f;
        break;
    case View::LogC4ToRec709:
        decode = logC4ToLinear;
        tone = logC4Tone;
        tables->matrix = Awg4ToRec709;
        tables->saturation = 0.9f;
        break;
    case View::None:
    case View::CustomLut:
    case View::Auto:
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
        const float position = static_cast<float>(i) / (OutputLutSize - 1);
        const float linear = position * position * OutputLutRange;
        tables->output[i] = static_cast<uchar>(qBound(0, qRound(tone(linear) * 255.0f), 255));
    }
    return tables;
}

const Tables &tablesFor(View view)
{
    static std::mutex mutex;
    static std::shared_ptr<const Tables> cache[5];
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
    if (linear >= OutputLutRange) {
        return tables.output[OutputLutSize - 1];
    }
    const int index = static_cast<int>(std::sqrt(linear * (1.0f / OutputLutRange)) * (OutputLutSize - 1) + 0.5f);
    return tables.output[static_cast<size_t>(qMin(index, OutputLutSize - 1))];
}

// ---- .cube LUTs -----------------------------------------------------------------------------

struct CubeLut
{
    QString filePath;
    QString identity; // path + size + mtime, for caches
    int size1D = 0;
    int size3D = 0;
    float domainMin[3] = {0, 0, 0};
    float domainMax[3] = {1, 1, 1};
    std::vector<float> data; // RGB triplets; for 3D, red varies fastest
    // Input value -> position on the LUT grid (0..size-1), per channel, for 16-bit integer
    // code values and for half-float bit patterns.
    std::vector<float> coord16[3];
    std::vector<float> coordHalf[3];
};

// Loaded LUTs: the user's custom LUT, and ARRI's own LogC3/LogC4 -> Rec.709 LUTs, which the
// built-in views use when they are found.
enum LutSlot { CustomSlot, ArriLogC3Slot, ArriLogC4Slot, LutSlotCount };

std::mutex lutMutex;
std::shared_ptr<const CubeLut> loadedLuts[LutSlotCount];

std::shared_ptr<const CubeLut> lutSnapshot(LutSlot slot = CustomSlot)
{
    std::lock_guard<std::mutex> lock(lutMutex);
    return loadedLuts[slot];
}

void storeLut(LutSlot slot, std::shared_ptr<const CubeLut> lut)
{
    std::lock_guard<std::mutex> lock(lutMutex);
    loadedLuts[slot] = std::move(lut);
}

// The LUT a view renders through, if any.
std::shared_ptr<const CubeLut> lutForView(View view)
{
    switch (view) {
    case View::CustomLut:
        return lutSnapshot(CustomSlot);
    case View::LogC3ToRec709:
        return lutSnapshot(ArriLogC3Slot);
    case View::LogC4ToRec709:
        return lutSnapshot(ArriLogC4Slot);
    case View::None:
    case View::Auto:
        break;
    }
    return nullptr;
}

bool parseFloats(const QList<QByteArray> &parts, int first, int count, float *out)
{
    if (parts.size() < first + count) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        bool ok = false;
        out[i] = parts.at(first + i).toFloat(&ok);
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<CubeLut> parseCube(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return nullptr;
    }
    auto fail = [error](const QString &message) -> std::shared_ptr<CubeLut> {
        if (error) {
            *error = message;
        }
        return nullptr;
    };

    auto lut = std::make_shared<CubeLut>();
    lut->filePath = filePath;
    std::vector<float> &data = lut->data;
    int lineNumber = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        ++lineNumber;
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const QList<QByteArray> parts = line.simplified().split(' ');
        const QByteArray keyword = parts.first().toUpper();
        const char first = keyword.at(0);
        const bool numeric = (first >= '0' && first <= '9') || first == '-' || first == '.' || first == '+';
        if (numeric) {
            float rgb[3];
            if (!parseFloats(parts, 0, 3, rgb)) {
                return fail(QStringLiteral("Bad value on line %1").arg(lineNumber));
            }
            data.insert(data.end(), rgb, rgb + 3);
        } else if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE") {
            bool ok = false;
            const int size = parts.size() > 1 ? parts.at(1).toInt(&ok) : 0;
            if (!ok || size < 2 || size > 65536 || (keyword == "LUT_3D_SIZE" && size > 256)) {
                return fail(QStringLiteral("Bad %1 on line %2").arg(QString::fromLatin1(keyword)).arg(lineNumber));
            }
            (keyword == "LUT_3D_SIZE" ? lut->size3D : lut->size1D) = size;
        } else if (keyword == "DOMAIN_MIN") {
            if (!parseFloats(parts, 1, 3, lut->domainMin)) {
                return fail(QStringLiteral("Bad DOMAIN_MIN on line %1").arg(lineNumber));
            }
        } else if (keyword == "DOMAIN_MAX") {
            if (!parseFloats(parts, 1, 3, lut->domainMax)) {
                return fail(QStringLiteral("Bad DOMAIN_MAX on line %1").arg(lineNumber));
            }
        } else if (keyword == "LUT_1D_INPUT_RANGE" || keyword == "LUT_3D_INPUT_RANGE") {
            float range[2];
            if (!parseFloats(parts, 1, 2, range)) {
                return fail(QStringLiteral("Bad %1 on line %2").arg(QString::fromLatin1(keyword)).arg(lineNumber));
            }
            for (int c = 0; c < 3; ++c) {
                lut->domainMin[c] = range[0];
                lut->domainMax[c] = range[1];
            }
        }
        // TITLE and unknown keywords are ignored.
    }

    if (lut->size3D > 0 && lut->size1D > 0) {
        return fail(QStringLiteral("LUTs with both a 1D and a 3D part are not supported"));
    }
    const qsizetype expected = lut->size3D > 0 ? qsizetype(lut->size3D) * lut->size3D * lut->size3D * 3
                                               : qsizetype(lut->size1D) * 3;
    if (expected == 0) {
        return fail(QStringLiteral("No LUT_3D_SIZE or LUT_1D_SIZE"));
    }
    if (qsizetype(data.size()) != expected) {
        return fail(QStringLiteral("Expected %1 entries, found %2").arg(expected / 3).arg(data.size() / 3));
    }
    for (int c = 0; c < 3; ++c) {
        if (!(lut->domainMax[c] > lut->domainMin[c])) {
            return fail(QStringLiteral("Empty domain"));
        }
    }

    const int size = lut->size3D > 0 ? lut->size3D : lut->size1D;
    for (int c = 0; c < 3; ++c) {
        const float scale = static_cast<float>(size - 1) / (lut->domainMax[c] - lut->domainMin[c]);
        auto toCoord = [&](float value) {
            if (!std::isfinite(value)) {
                value = 0.0f;
            }
            return qBound(0.0f, (value - lut->domainMin[c]) * scale, static_cast<float>(size - 1));
        };
        lut->coord16[c].resize(65536);
        lut->coordHalf[c].resize(65536);
        for (int i = 0; i < 65536; ++i) {
            lut->coord16[c][i] = toCoord(static_cast<float>(i) / 65535.0f);
            lut->coordHalf[c][i] = toCoord(halfBitsToFloat(static_cast<quint16>(i)));
        }
    }

    const QFileInfo info(filePath);
    lut->identity = info.absoluteFilePath() + QStringLiteral("|") + QString::number(info.size())
        + QStringLiteral("|") + QString::number(info.lastModified().toMSecsSinceEpoch());
    return lut;
}

inline uchar toByte(float value)
{
    return static_cast<uchar>(qBound(0.0f, value * 255.0f + 0.5f, 255.0f));
}

// Tetrahedral interpolation in a 3D LUT; r, g, b are grid positions.
inline QRgb sample3D(const CubeLut &lut, float r, float g, float b)
{
    const int n = lut.size3D;
    const int r0 = qMin(static_cast<int>(r), n - 2);
    const int g0 = qMin(static_cast<int>(g), n - 2);
    const int b0 = qMin(static_cast<int>(b), n - 2);
    const float fr = r - r0;
    const float fg = g - g0;
    const float fb = b - b0;
    const int sr = 3;
    const int sg = 3 * n;
    const int sb = 3 * n * n;
    const float *base = lut.data.data() + r0 * sr + g0 * sg + b0 * sb;
    const float *c000 = base;
    const float *c111 = base + sr + sg + sb;

    // Pick the tetrahedron by ordering the fractions.
    const float *c1;
    const float *c2;
    float w0, w1, w2, w3;
    if (fr >= fg) {
        if (fg >= fb) {
            c1 = base + sr; c2 = base + sr + sg;
            w0 = 1 - fr; w1 = fr - fg; w2 = fg - fb; w3 = fb;
        } else if (fr >= fb) {
            c1 = base + sr; c2 = base + sr + sb;
            w0 = 1 - fr; w1 = fr - fb; w2 = fb - fg; w3 = fg;
        } else {
            c1 = base + sb; c2 = base + sr + sb;
            w0 = 1 - fb; w1 = fb - fr; w2 = fr - fg; w3 = fg;
        }
    } else {
        if (fb >= fg) {
            c1 = base + sb; c2 = base + sg + sb;
            w0 = 1 - fb; w1 = fb - fg; w2 = fg - fr; w3 = fr;
        } else if (fb >= fr) {
            c1 = base + sg; c2 = base + sg + sb;
            w0 = 1 - fg; w1 = fg - fb; w2 = fb - fr; w3 = fr;
        } else {
            c1 = base + sg; c2 = base + sr + sg;
            w0 = 1 - fg; w1 = fg - fr; w2 = fr - fb; w3 = fb;
        }
    }
    return qRgb(toByte(w0 * c000[0] + w1 * c1[0] + w2 * c2[0] + w3 * c111[0]),
                toByte(w0 * c000[1] + w1 * c1[1] + w2 * c2[1] + w3 * c111[1]),
                toByte(w0 * c000[2] + w1 * c1[2] + w2 * c2[2] + w3 * c111[2]));
}

inline float sample1D(const CubeLut &lut, float position, int channel)
{
    const int i0 = qMin(static_cast<int>(position), lut.size1D - 2);
    const float f = position - i0;
    const float a = lut.data[size_t(i0) * 3 + channel];
    const float b = lut.data[size_t(i0 + 1) * 3 + channel];
    return a + (b - a) * f;
}

// ---- Helpers --------------------------------------------------------------------------------

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

// Runs rowFunction(y) for every row, split over a few threads for big images.
template <typename Function>
void forEachRow(int height, qint64 pixelCount, Function rowFunction)
{
    const int hardware = static_cast<int>(std::thread::hardware_concurrency());
    const int threadCount = pixelCount < 1000000 ? 1 : qBound(1, hardware, 8);
    if (threadCount == 1) {
        for (int y = 0; y < height; ++y) {
            rowFunction(y);
        }
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(threadCount - 1);
    auto band = [&](int index) {
        const int begin = height * index / threadCount;
        const int end = height * (index + 1) / threadCount;
        for (int y = begin; y < end; ++y) {
            rowFunction(y);
        }
    };
    for (int i = 1; i < threadCount; ++i) {
        threads.emplace_back(band, i);
    }
    band(0);
    for (std::thread &thread : threads) {
        thread.join();
    }
}

QImage applyLut(const QImage &input, bool floatSource, const CubeLut &lut)
{
    const int width = input.width();
    const int height = input.height();
    QImage result(width, height, QImage::Format_RGB32);
    if (result.isNull()) {
        return result;
    }
    const std::vector<float> *coord = floatSource ? lut.coordHalf : lut.coord16;
    const float *cr = coord[0].data();
    const float *cg = coord[1].data();
    const float *cb = coord[2].data();
    forEachRow(height, qint64(width) * height, [&](int y) {
        const auto *in = reinterpret_cast<const quint16 *>(input.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const float r = cr[in[x * 4 + 0]];
            const float g = cg[in[x * 4 + 1]];
            const float b = cb[in[x * 4 + 2]];
            if (lut.size3D > 0) {
                out[x] = sample3D(lut, r, g, b);
            } else {
                out[x] = qRgb(toByte(sample1D(lut, r, 0)), toByte(sample1D(lut, g, 1)), toByte(sample1D(lut, b, 2)));
            }
        }
    });
    return result;
}

// Converts ACES scene-linear values (half floats) to what the view expects: a camera log
// encoding, or linear Rec.709 for the None view. Like the usual ACES-to-LogC DCTLs, negative
// values are clamped before and after the matrix.
QImage convertAces(const QImage &input, View view, ColorTransform::Input aces)
{
    const bool ap0 = aces == ColorTransform::Input::Aces2065;
    const Matrix *matrix = nullptr;
    float (*encode)(float) = nullptr;
    switch (view) {
    case View::LogC3ToRec709:
        matrix = ap0 ? &Ap0ToAwg3 : &Ap1ToAwg3;
        encode = linearToLogC3;
        break;
    case View::LogC4ToRec709:
    case View::CustomLut:
        matrix = ap0 ? &Ap0ToAwg4 : &Ap1ToAwg4;
        encode = linearToLogC4;
        break;
    case View::None:
    case View::Auto:
        matrix = ap0 ? &Ap0ToRec709 : &Ap1ToRec709;
        break;
    }
    const Matrix &m = *matrix;
    const bool clamp = encode != nullptr;

    const int width = input.width();
    const int height = input.height();
    QImage result(width, height, QImage::Format_RGBX16FPx4);
    if (result.isNull()) {
        return result;
    }
    forEachRow(height, qint64(width) * height, [&](int y) {
        const auto *in = reinterpret_cast<const qfloat16 *>(input.constScanLine(y));
        auto *out = reinterpret_cast<qfloat16 *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            float r = static_cast<float>(in[x * 4 + 0]);
            float g = static_cast<float>(in[x * 4 + 1]);
            float b = static_cast<float>(in[x * 4 + 2]);
            if (!std::isfinite(r)) r = 0.0f;
            if (!std::isfinite(g)) g = 0.0f;
            if (!std::isfinite(b)) b = 0.0f;
            if (clamp) {
                r = qMax(r, 0.0f);
                g = qMax(g, 0.0f);
                b = qMax(b, 0.0f);
            }
            float r2 = m[0] * r + m[1] * g + m[2] * b;
            float g2 = m[3] * r + m[4] * g + m[5] * b;
            float b2 = m[6] * r + m[7] * g + m[8] * b;
            if (encode) {
                r2 = encode(qMax(r2, 0.0f));
                g2 = encode(qMax(g2, 0.0f));
                b2 = encode(qMax(b2, 0.0f));
            }
            out[x * 4 + 0] = qfloat16(r2);
            out[x * 4 + 1] = qfloat16(g2);
            out[x * 4 + 2] = qfloat16(b2);
            out[x * 4 + 3] = qfloat16(1.0f);
        }
    });
    return result;
}

} // namespace

namespace ColorTransform {

QString displayName(View view)
{
    switch (view) {
    case View::LogC3ToRec709:
        return arriLutFilePath(view).isEmpty() ? QStringLiteral("ARRI LogC3 → Rec.709 (approx.)")
                                                : QStringLiteral("ARRI LogC3 → Rec.709");
    case View::LogC4ToRec709:
        return arriLutFilePath(view).isEmpty() ? QStringLiteral("ARRI LogC4 → Rec.709 (approx.)")
                                                : QStringLiteral("ARRI LogC4 → Rec.709");
    case View::Auto:
        return QStringLiteral("Auto (from file tags)");
    case View::CustomLut: {
        const QString path = lutFilePath();
        return path.isEmpty() ? QStringLiteral("LUT file")
                              : QStringLiteral("LUT: %1").arg(QFileInfo(path).completeBaseName());
    }
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
    case View::CustomLut:
        return QStringLiteral("lut");
    case View::Auto:
        return QStringLiteral("auto");
    case View::None:
        break;
    }
    return QStringLiteral("none");
}

View fromSettingsKey(const QString &key)
{
    for (View view : {View::LogC3ToRec709, View::LogC4ToRec709, View::CustomLut, View::Auto}) {
        if (key == settingsKey(view)) {
            return view;
        }
    }
    return View::None;
}

QString cacheKey(View view)
{
    if (view == View::Auto) {
        return QStringLiteral("auto|") + cacheKey(View::LogC3ToRec709) + QStringLiteral("|")
            + cacheKey(View::LogC4ToRec709);
    }
    // The suffix changes whenever the built-in curves change, so old thumbnails are remade.
    if (const auto lut = lutForView(view)) {
        return settingsKey(view) + QStringLiteral("|lut|") + lut->identity;
    }
    if (view == View::CustomLut) {
        return QStringLiteral("lut|");
    }
    return settingsKey(view) + QStringLiteral("|v3");
}

QString inputDisplayName(Input input)
{
    switch (input) {
    case Input::Auto:
        return QStringLiteral("Auto (from file tags)");
    case Input::CameraLog:
        return QStringLiteral("Camera log (show code values)");
    case Input::Linear:
        return QStringLiteral("Scene linear (sRGB curve)");
    case Input::Aces2065:
        return QStringLiteral("ACES2065-1 linear (AP0)");
    case Input::AcesCg:
        return QStringLiteral("ACEScg linear (AP1)");
    }
    return QString();
}

QString inputSettingsKey(Input input)
{
    switch (input) {
    case Input::Auto:
        return QStringLiteral("auto");
    case Input::CameraLog:
        return QStringLiteral("log");
    case Input::Linear:
        return QStringLiteral("linear");
    case Input::Aces2065:
        return QStringLiteral("aces2065");
    case Input::AcesCg:
        return QStringLiteral("acescg");
    }
    return QString();
}

Input inputFromSettingsKey(const QString &key)
{
    for (Input input : {Input::CameraLog, Input::Linear, Input::Aces2065, Input::AcesCg}) {
        if (key == inputSettingsKey(input)) {
            return input;
        }
    }
    return Input::Auto;
}

Input resolveInput(Input input, const QImage &source)
{
    if (input != Input::Auto) {
        return input;
    }
    const QString tag = source.text(colorSpaceTextKey());
    if (tag == inputSettingsKey(Input::Aces2065)) {
        return Input::Aces2065;
    }
    if (tag == inputSettingsKey(Input::AcesCg)) {
        return Input::AcesCg;
    }
    if (tag == inputSettingsKey(Input::CameraLog) || !isFloatFormat(source.format())) {
        return Input::CameraLog;
    }
    return Input::Linear;
}

bool loadLut(const QString &filePath, QString *error)
{
    std::shared_ptr<CubeLut> lut = parseCube(filePath, error);
    if (!lut) {
        return false;
    }
    storeLut(CustomSlot, std::move(lut));
    return true;
}

int findArriLuts(const QStringList &folders)
{
    // ARRI's file names, e.g. "ARRI_LogC4-to-Gamma24_Rec709-D65_v1-33.cube". Prefer the
    // standard (not "Classic") look, final over beta, and the 33-point version.
    auto score = [](const QString &name) {
        int value = 0;
        if (!name.contains(QStringLiteral("classic"), Qt::CaseInsensitive)) value += 8;
        if (!name.contains(QStringLiteral("beta"), Qt::CaseInsensitive)) value += 4;
        if (name.contains(QStringLiteral("-33")) || name.contains(QStringLiteral("_33"))) value += 2;
        return value;
    };
    const QString patterns[2] = {QStringLiteral("logc3-to-gamma24_rec709"), QStringLiteral("logc4-to-gamma24_rec709")};
    const LutSlot arriSlots[2] = {ArriLogC3Slot, ArriLogC4Slot};
    QString best[2];
    int bestScore[2] = {-1, -1};
    for (const QString &folder : folders) {
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
            continue;
        }
        QDirIterator it(folder, {QStringLiteral("*.cube")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QString name = it.fileName();
            for (int i = 0; i < 2; ++i) {
                if (name.contains(patterns[i], Qt::CaseInsensitive) && score(name) > bestScore[i]) {
                    best[i] = path;
                    bestScore[i] = score(name);
                }
            }
        }
        if (!best[0].isEmpty() && !best[1].isEmpty()) {
            break; // earlier folders win
        }
    }
    int found = 0;
    for (int i = 0; i < 2; ++i) {
        std::shared_ptr<CubeLut> lut = best[i].isEmpty() ? nullptr : parseCube(best[i], nullptr);
        found += lut ? 1 : 0;
        storeLut(arriSlots[i], std::move(lut));
    }
    return found;
}

QString arriLutFilePath(View view)
{
    if (view != View::LogC3ToRec709 && view != View::LogC4ToRec709) {
        return QString();
    }
    const auto lut = lutForView(view);
    return lut ? lut->filePath : QString();
}

bool hasLut()
{
    return lutSnapshot(CustomSlot) != nullptr;
}

QString lutFilePath()
{
    const auto lut = lutSnapshot(CustomSlot);
    return lut ? lut->filePath : QString();
}

View resolveView(View view, const QImage &source)
{
    if (view != View::Auto) {
        return view;
    }
    const QString space = source.text(colorSpaceTextKey());
    if (space == inputSettingsKey(Input::Aces2065) || space == inputSettingsKey(Input::AcesCg)) {
        return View::LogC4ToRec709; // ACES comps are shown through the camera's LogC4 look
    }
    const QString curve = source.text(cameraCurveTextKey());
    if (curve == QStringLiteral("logc4")) {
        return View::LogC4ToRec709;
    }
    if (curve == QStringLiteral("logc3")) {
        return View::LogC3ToRec709;
    }
    return View::None;
}

QString describeSource(const QImage &source, Input input)
{
    if (source.isNull()) {
        return QString();
    }
    QString format = source.text(sourceFormatTextKey());
    if (format.isEmpty()) {
        format = isFloatFormat(source.format()) ? QStringLiteral("float")
                                                : QStringLiteral("%1-bit").arg(source.depth() >= 64 ? 16 : 8);
    }
    QStringList parts = {format};

    const Input resolved = resolveInput(input, source);
    const bool tagged = !source.text(colorSpaceTextKey()).isEmpty();
    const QString curve = source.text(cameraCurveTextKey());
    QString space;
    switch (resolved) {
    case Input::Aces2065:
        space = QStringLiteral("ACES2065-1");
        break;
    case Input::AcesCg:
        space = QStringLiteral("ACEScg");
        break;
    case Input::CameraLog:
        space = curve == QStringLiteral("logc4") ? QStringLiteral("ARRI LogC4")
            : curve == QStringLiteral("logc3")   ? QStringLiteral("ARRI LogC3")
            : isFloatFormat(source.format())     ? QStringLiteral("camera log")
                                                 : QString();
        break;
    case Input::Linear:
        space = QStringLiteral("scene linear");
        break;
    case Input::Auto:
        break;
    }
    if (!space.isEmpty()) {
        parts.append(space + (tagged && input == Input::Auto ? QStringLiteral(" (tagged)") : QString()));
    }
    return parts.join(QStringLiteral(" · "));
}

QImage toDisplay(const QImage &source, View view, Input inputSpace)
{
    if (source.isNull()) {
        return source;
    }
    view = resolveView(view, source);

    const bool floatSource = isFloatFormat(source.format());
    std::shared_ptr<const CubeLut> lut = lutForView(view);
    if (view == View::CustomLut && !lut) {
        view = View::None;
    }

    const Input resolved = resolveInput(inputSpace, source);

    // Nothing to do for ordinary 8-bit or integer images shown as stored, or for camera log
    // EXRs (their values are already display-ready code values, like a log DPX).
    if (view == View::None && (!floatSource || resolved == Input::CameraLog)) {
        return source.hasAlphaChannel() ? source.convertToFormat(QImage::Format_ARGB32)
                                        : source.convertToFormat(QImage::Format_RGB32);
    }

    // Work from 16-bit values: half floats keep their bit patterns, integers are widened to 16 bit.
    QImage input = floatSource ? source.convertToFormat(QImage::Format_RGBX16FPx4)
                               : source.convertToFormat(QImage::Format_RGBX64);
    if (floatSource && (resolved == Input::Aces2065 || resolved == Input::AcesCg)) {
        input = convertAces(input, view, resolved);
    }
    if (lut) {
        return applyLut(input, floatSource, *lut);
    }

    const Tables &tables = tablesFor(view);
    const Matrix &m = tables.matrix;
    const float saturation = tables.saturation;
    const int width = input.width();
    const int height = input.height();
    QImage result(width, height, QImage::Format_RGB32);
    if (result.isNull()) {
        return result;
    }
    const std::vector<float> &decode = floatSource ? tables.decodeHalf : tables.decode16;

    forEachRow(height, qint64(width) * height, [&](int y) {
        const auto *in = reinterpret_cast<const quint16 *>(input.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const float r = decode[in[x * 4 + 0]];
            const float g = decode[in[x * 4 + 1]];
            const float b = decode[in[x * 4 + 2]];
            float r2 = m[0] * r + m[1] * g + m[2] * b;
            float g2 = m[3] * r + m[4] * g + m[5] * b;
            float b2 = m[6] * r + m[7] * g + m[8] * b;
            if (saturation != 1.0f || r2 < 0.0f || g2 < 0.0f || b2 < 0.0f) {
                // Pull colours toward their luminance: a gentle desaturation, and, for colours
                // outside Rec.709 (a negative channel), just far enough to bring them inside,
                // so they keep their hue instead of clipping one channel.
                const float luma = 0.2126f * r2 + 0.7152f * g2 + 0.0722f * b2;
                float amount = saturation;
                const float lowest = qMin(r2, qMin(g2, b2));
                if (lowest < 0.0f) {
                    amount = luma > 0.0f ? qMin(amount, luma / (luma - lowest)) : 0.0f;
                }
                r2 = luma + amount * (r2 - luma);
                g2 = luma + amount * (g2 - luma);
                b2 = luma + amount * (b2 - luma);
            }
            out[x] = qRgb(outputValue(tables, r2), outputValue(tables, g2), outputValue(tables, b2));
        }
    });
    return result;
}

} // namespace ColorTransform
