#pragma once

#include <QImage>
#include <QSize>
#include <QString>

// Minimal reader for SMPTE 268M DPX files, the common film/VFX cases:
// RGB, RGBA, ABGR and luma, 8/10/12/16 bits per component, filled (method A/B) or packed,
// big or little endian. Returns a 16-bit-per-channel image holding the stored code values;
// pass it through ColorTransform::toDisplay to show it.
namespace Dpx {

bool isDpxFile(const QString &filePath);

// Reads the first image element. On failure returns a null image and sets error.
// With a target size, only every Nth line and pixel is decoded so the result is at least that
// big (but not much bigger): a fast preview for thumbnails.
QImage read(const QString &filePath, QString *error = nullptr, const QSize &targetSize = QSize());

} // namespace Dpx
