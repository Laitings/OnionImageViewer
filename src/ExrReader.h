#pragma once

#include <QImage>
#include <QString>

// OpenEXR support. Only active when the app is built with the OpenEXR library
// (see vcpkg.json); otherwise isAvailable() is false and read() reports an error.
namespace Exr {

bool isAvailable();
bool isExrFile(const QString &filePath);

// Reads the main RGB(A) layer of the first part and converts linear values to sRGB for display
// (values above 1.0 are clipped). With preferPreview, a preview image stored in the file is
// returned instead when there is one: much faster, and ideal for thumbnails.
QImage read(const QString &filePath, QString *error = nullptr, bool preferPreview = false);

} // namespace Exr
