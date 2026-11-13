#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QPair>
#include <QSize>
#include <QString>

// HEIF / HEIC support (iPhone photos, Canon and Fuji .hif), through libheif with the libde265
// HEVC decoder. Only active when the app is built with libheif (see vcpkg.json); otherwise
// isAvailable() is false and read() reports an error.
//
// libheif and libde265 are LGPL, so on Windows they are linked as DLLs (triplets/), never
// statically.
namespace Heif {

bool isAvailable();
bool isHeifFile(const QString &filePath);

// Decodes the primary image with its rotation, mirroring and crop applied. 8-bit files give an
// 8-bit image, 10/12-bit files a 16-bit one. Images with an ICC profile or Display P3 / Rec.2020
// primaries are converted to sRGB. With a target size (thumbnails), the thumbnail stored in the
// file is used when it is at least that big. On failure returns a null image and sets error.
QImage read(const QString &filePath, QString *error = nullptr, const QSize &targetSize = QSize());

// Facts about the file for the metadata panel: size, bit depth, alpha, colour profile, number of
// images, thumbnails, depth map. Reads the header, not the pixels.
QList<QPair<QString, QString>> describe(const QString &filePath, QString *error = nullptr);

// The primary image's Exif block, starting at the TIFF header ("II*\0" or "MM\0*"); empty when
// the file has none. Pass it to Exif::describe.
QByteArray exifData(const QString &filePath);

} // namespace Heif
