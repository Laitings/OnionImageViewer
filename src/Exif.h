#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// A small Exif reader for the metadata panel: camera, lens, exposure, dates and GPS position,
// in readable form ("1/120 s", "f/1.8", "26 mm (35 mm equivalent)"). Unknown tags are skipped.
namespace Exif {

// tiff starts at the TIFF header ("II*\0" or "MM\0*"); a leading "Exif\0\0" is skipped. Returns
// nothing for data that isn't Exif. Broken offsets are ignored rather than read past the end.
QList<QPair<QString, QString>> describe(const QByteArray &tiff);

} // namespace Exif
