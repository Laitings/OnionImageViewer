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

// The same entries laid out as columns (Group, Name, Value) padded with spaces, so they line up
// in any text editor with a monospace font. A group's name is only written on its first row.
QString toAlignedText(const QList<Entry> &entries);
// A Markdown table (| Group | Name | Value |); pipes and line breaks in values are escaped.
QString toMarkdown(const QList<Entry> &entries);
// Comma-separated values with a header row, quoted as needed, for spreadsheets that prefer CSV.
QString toCsv(const QList<Entry> &entries);

} // namespace Metadata
