#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// Camera raw files (Sony ARW, Canon CR2/CR3, Nikon NEF, Adobe DNG, Fujifilm RAF and the other
// formats in suffixes()) through LibRaw. Without LibRaw at build time isAvailable() is false and
// the readers return nothing.
namespace Raw {

bool isAvailable();

// The lower-case extensions treated as camera raw, without the dot.
const QStringList &suffixes();

// True for a file with one of suffixes(); checks the name only, never the contents.
bool isRawFile(const QString &filePath);

// Develops the full-resolution raw image to sRGB (8 bit) using the camera white balance, with the
// camera's orientation applied.
QImage read(const QString &filePath, QString *error = nullptr);

// Uses the embedded JPEG preview when there is one (fast: no raw decoding) and develops a
// half-size image otherwise. The result fits size when the preview is a JPEG.
QImage readThumbnail(const QString &filePath, const QSize &size);

// Camera, lens, exposure, date and sizes, read from the header without unpacking the raw pixels.
QList<QPair<QString, QString>> describe(const QString &filePath);

// When the picture was taken, from the raw header, as local time. Invalid when unknown.
QDateTime dateTaken(const QString &filePath);

} // namespace Raw
