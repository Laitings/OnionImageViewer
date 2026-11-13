#pragma once

#include "ColorTransform.h"

#include <QImage>
#include <QList>
#include <QString>

#include <array>
#include <vector>

// Video-style scopes (histogram and waveform) computed from an image. compute() is slow-ish (it
// reads up to a few hundred thousand pixels) and is meant to run on a background thread; the
// result holds everything the scopes panel paints, so painting is cheap.
namespace Scopes {

// Where a value sits on a scope's value axis.
class Axis
{
public:
    enum class Kind {
        Linear, // 0 at the bottom, top() at the top
        Stops,  // logarithmic (powers of two), for scene-linear float images
    };

    Axis() = default;
    Axis(Kind kind, float top);

    Kind kind() const { return m_kind; }
    float top() const { return m_top; }

    // Position 0..1 of a value (0 = bottom of the scope). Values outside the axis are clamped.
    float position(float value) const;

    struct Tick {
        float position;
        QString label; // empty for minor grid lines
        bool highlight; // 18% grey on the Stops axis
    };
    QList<Tick> ticks() const;

private:
    Kind m_kind = Kind::Linear;
    float m_top = 1.0f;
    float m_bottomStop = -10.0f; // Stops: log2 of the value at the bottom
    float m_topStop = 0.0f;      // Stops: log2(top)
};

// Histogram bins and waveform levels.
constexpr int Bins = 256;
constexpr int Levels = 256;

struct Result {
    bool valid = false;
    Axis axis;
    // R, G, B and luma (Rec.709 weights on the values as they are), Bins counts each.
    std::array<std::vector<quint32>, 4> histogram;
    // Waveforms, columns of the image left to right, value bottom to top, already shaded:
    QImage lumaWaveform; // luma trace
    QImage rgbWaveform;  // R, G and B traces added together (white where they overlap)
    QImage parade;       // R, G and B traces side by side
    int sampleStep = 1;  // every sampleStep-th pixel in x and y was read
    QString description; // e.g. "Display · LogC4 → Rec.709"
};

// Reads one pixel as float RGB (8-bit and 16-bit integer formats scaled to 0..1, float formats as
// stored). Works on any QImage format.
void readPixel(const QImage &image, int x, int y, float rgb[3]);

// What the stored values of an image are, to choose the axis: floats that are scene linear get a
// Stops axis, everything else a linear one (extended above 1 for float values beyond 1).
bool isFloatImage(const QImage &image);

// Computes the scopes of an image from a downsampled grid of at most about maxSamples pixels.
// sceneLinear selects the Stops axis for float images (see ColorTransform::resolveInput).
Result compute(const QImage &image, bool sceneLinear, int maxSamples = 400000);

} // namespace Scopes
