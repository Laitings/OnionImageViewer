#pragma once

#include <QImage>
#include <QString>

// OpenEXR support. Only active when the app is built with the OpenEXR library
// (see vcpkg.json); otherwise isAvailable() is false and read() reports an error.
namespace Exr {

bool isAvailable();
bool isExrFile(const QString &filePath);

// Reads the main RGB layer of the first part as a half-float image with the stored values
// (pass it through ColorTransform::toDisplay to show it). With preferPreview, an 8-bit preview
// image stored in the file is returned instead when there is one: much faster, for thumbnails.
QImage read(const QString &filePath, QString *error = nullptr, bool preferPreview = false);

} // namespace Exr
