#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Image sequences: files in one folder whose names differ only in a frame number at the end of
// the name, before the extension ("A001_C002.1001.dpx", "shot_0042.exr", "IMG_0001.jpg"). Works
// for every format the viewer opens. The batch renamer splits frame numbers the same way.
namespace Sequence {

struct Name
{
    QString prefix; // everything before the frame number, e.g. "A001_C002."
    QString frame;  // the frame number's digits as written, e.g. "1001"; empty when there is none
    QString suffix; // the extension with its dot, e.g. ".dpx"; empty when there is none
};

// Splits a file name (no folder) into prefix, frame number and extension. The frame number is the
// run of digits right before the last dot (or at the end, when there is no extension).
Name split(const QString &fileName);

// What identifies the sequence a file belongs to: its folder, prefix and extension, compared
// without case as Windows does. Empty when the name has no frame number.
QString key(const QString &path);

// The files of paths that are in the same sequence as paths[index], as indices into paths,
// ordered by frame number (so 9 comes before 10 even without zero padding, and other files
// sorted in between don't matter). When no other file shares the prefix, files whose names differ
// only in their numbers count instead ("Door_v0010001_00001.png", "Door_v0010002_00002.png").
// Empty when the file has no frame number or is the only one.
QVector<int> framesOf(const QStringList &paths, int index);

// A readable name for the sequence, with the frame range: "A001_C002.[1001-1200].dpx". Numbers
// that change besides the frame number show as '#'.
QString displayName(const QStringList &paths, const QVector<int> &frames);

} // namespace Sequence
