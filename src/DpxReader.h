#pragma once

#include <QImage>
#include <QString>

// Minimal reader for SMPTE 268M DPX files, the common film/VFX cases:
// RGB, RGBA, ABGR and luma, 8/10/12/16 bits per component, filled (method A/B) or packed,
// big or little endian. Values are shown as stored (no log-to-display conversion).
namespace Dpx {

bool isDpxFile(const QString &filePath);

// Reads the first image element. On failure returns a null image and sets error.
QImage read(const QString &filePath, QString *error = nullptr);

} // namespace Dpx
