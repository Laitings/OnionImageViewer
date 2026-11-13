#pragma once

#include <QList>
#include <QString>

// Everything a file's header says about it, for the metadata panel: all EXR attributes (every
// part), the full DPX header, DNG facts, HEIF facts with the Exif data, or what Qt's image reader reports
// for other formats. Only the
// header is read, never the pixels, so this is cheap even over the network.
namespace Metadata {

struct Entry
{
    QString group; // e.g. "File", "EXR", "DPX image", "DPX film"
    QString key;
    QString value;
};

QList<Entry> read(const QString &filePath);

// Tab-separated "group / key / value" lines, for copying to the clipboard.
QString toText(const QList<Entry> &entries);

} // namespace Metadata
