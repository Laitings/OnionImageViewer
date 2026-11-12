#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

// Display ("view") transforms. Readers hand over images at full precision with their stored
// values untouched (DPX as 16-bit code values, EXR as half floats); toDisplay() turns them into
// an 8-bit image for the screen.
namespace ColorTransform {

enum class View {
    None,          // as stored: EXR (scene-linear) gets the sRGB curve, everything else unchanged
    LogC3ToRec709, // ARRI LogC3 (EI 800) / AWG3: ARRI's LUT when found, else an approximation
    LogC4ToRec709, // ARRI LogC4 / AWG4: ARRI's LUT when found, else an approximation
    CustomLut,     // a .cube LUT file loaded with loadLut()
};

// What the stored values are, for floating-point (EXR) images. ACES footage is first converted
// to the log encoding the view expects (LogC4 for the LogC4 and LUT views, LogC3 for LogC3), the
// way a VFX house's "ACES to LogC4" DCTL does, or to linear Rec.709 for the None view.
// Integer images (DPX, TIFF, ...) are always taken as stored.
enum class Input {
    Auto,      // from the file's tags (ACES chromaticities/flag, camera log tags), otherwise Linear
    CameraLog, // camera log code values: shown as they are with the None view
    Linear,    // scene linear: the None view adds the sRGB curve
    Aces2065, // ACES2065-1: AP0 primaries, scene linear
    AcesCg,   // ACEScg: AP1 primaries, scene linear
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

// Looks for ARRI's LogC3/LogC4 -> Rec.709 (Gamma 2.4) .cube files in the folders (searched in
// order, with subfolders) and makes the built-in LogC3/LogC4 views use them. Without them the
// views fall back to an approximation. Returns how many of the two were found.
int findArriLuts(const QStringList &folders);
QString arriLutFilePath(View view);

QString inputDisplayName(Input input);
QString inputSettingsKey(Input input);
Input inputFromSettingsKey(const QString &key);

// Image text key readers use to tag what they know about an image's values.
inline QString colorSpaceTextKey() { return QStringLiteral("ColorSpace"); }

// Turns Auto into the input the image is tagged with (Linear when an untagged float image,
// CameraLog for integer images such as DPX).
Input resolveInput(Input input, const QImage &source);

QImage toDisplay(const QImage &source, View view, Input input = Input::Auto);

} // namespace ColorTransform
