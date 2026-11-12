#pragma once

#include <QImage>
#include <QString>

// Display ("view") transforms. Readers hand over images at full precision with their stored
// values untouched (DPX as 16-bit code values, EXR as half floats); toDisplay() turns them into
// an 8-bit image for the screen.
namespace ColorTransform {

enum class View {
    None,          // as stored: EXR (scene-linear) gets the sRGB curve, everything else unchanged
    LogC3ToRec709, // ARRI LogC3 (EI 800) / ALEXA Wide Gamut 3
    LogC4ToRec709, // ARRI LogC4 / ALEXA Wide Gamut 4
};

QString displayName(View view);
QString settingsKey(View view);
View fromSettingsKey(const QString &key);

QImage toDisplay(const QImage &source, View view);

} // namespace ColorTransform
