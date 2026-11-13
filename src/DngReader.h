#pragma once

#include <QImage>
#include <QList>
#include <QPair>
#include <QString>

namespace Dng {

bool isAvailable();
bool isDngFile(const QString &filePath);

// Develops the full-resolution raw image to sRGB using the camera white balance.
QImage read(const QString &filePath, QString *error = nullptr);

// Uses the embedded preview when available and develops a reduced image otherwise.
QImage readThumbnail(const QString &filePath, const QSize &size);

// Reads the DNG header without unpacking raw pixels.
QList<QPair<QString, QString>> describe(const QString &filePath);

} // namespace Dng
