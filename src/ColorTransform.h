#pragma once

#include <QImage>
#include <QString>

// Display ("view") transforms. Readers hand over images at full precision with their stored
// values untouched (DPX as 16-bit code values, EXR as half floats); toDisplay() turns them into
// an 8-bit image for the screen.
namespace ColorTransform {

enum class View {
    None,          // as stored: EXR (scene-linear) gets the sRGB curve, everything else unchanged
    LogC3ToRec709, // ARRI LogC3 (EI 800) / ALEXA Wide Gamut 3, built-in approximation
    LogC4ToRec709, // ARRI LogC4 / ALEXA Wide Gamut 4, built-in approximation
    CustomLut,     // a .cube LUT file loaded with loadLut()
};

QString displayName(View view);
QString settingsKey(View view);
View fromSettingsKey(const QString &key);

// Identifies what a view renders, for thumbnail caches: for CustomLut it includes the LUT file
// and its modification time, so changing or editing the LUT gives new thumbnails.
QString cacheKey(View view);

// Loads an Adobe/Resolve .cube file (1D or 3D) as the CustomLut view. The LUT is applied to the
// stored values (DPX code values 0..1, EXR values as they are), so a camera log-to-Rec.709 LUT
// works directly. Safe to call while other threads render; they keep the LUT they started with.
bool loadLut(const QString &filePath, QString *error = nullptr);
bool hasLut();
QString lutFilePath();

QImage toDisplay(const QImage &source, View view);

} // namespace ColorTransform
