#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

// The batch renamer's engine: turns a list of files and a set of options into new names (with a
// reason for every name that can't be used), and carries out a list of renames safely. It has no
// UI, so the dialog (BatchRenameDialog) and the undo in MainWindow share it.
namespace BatchRename {

enum class CaseChange { Keep, Lower, Upper, Title };

struct Options
{
    // The new name without the extension. Tokens: {name} {n} {date} {modified} {frame} {folder}
    // {ext}, with an optional argument after a colon: {n:4} (digits), {date:yyyyMMdd} (Qt date
    // format). The extension is always kept, so a rename never changes a file's type.
    QString pattern = QStringLiteral("{name}");
    QString find;
    QString replace;
    bool regex = false;
    bool matchCase = false;
    CaseChange nameCase = CaseChange::Keep;
    CaseChange extensionCase = CaseChange::Keep; // Title is treated as Keep
    int counterStart = 1;
    int counterStep = 1;
    int counterPadding = 3;
    // Files that form an image sequence (name ending in a frame number, two or more selected with
    // the same prefix and extension, or one named like name.0001.ext) keep their frame numbers:
    // {name} is the part before the frame number, the sequence takes one counter value, and the
    // frame number goes back on the end, e.g. A001_C002.1001.dpx -> Shot_010.1001.dpx.
    bool keepFrameNumbers = true;
};

struct Source
{
    QString path; // absolute
    bool isFolder = false;
    QDateTime modified;
    QDateTime taken; // Exif date taken; invalid when there is none (or it hasn't been read)
};

enum class Status { Unchanged, Rename, Problem };

struct Result
{
    QString oldName;
    QString newName;
    Status status = Status::Unchanged;
    QString problem;    // why newName can't be used, for Status::Problem
    int sequence = -1;  // index of the image sequence the file belongs to, or -1
};

// The new name for every source, in the same order. existing holds every name already in the
// folders involved, as key(folder, name); a new name that matches one of them (ignoring case, as
// Windows does) is a conflict unless that file is itself being renamed away in this batch.
QList<Result> plan(const QList<Source> &sources, const Options &options, const QSet<QString> &existing);
QString key(const QString &folder, const QString &name);

// Whether the pattern uses {date}, the only token that needs the files' headers read.
bool usesDateTaken(const QString &pattern);

// The Exif date taken from a JPEG, TIFF/DNG or HEIF file. Reads only the header (up to 64 KB, or
// libheif for HEIF), sequentially. Slow on network shares: call it off the UI thread.
QDateTime readDateTaken(const QString &path);

// Default for Options::keepFrameNumbers for this selection: on when it holds something that
// looks like a film/VFX sequence (DPX, EXR, CIN, or a frame number after a dot), off for camera
// photo numbering such as IMG_0001.jpg.
bool looksLikeFrameSequence(const QStringList &paths);

// Why name can't be a Windows file name, or empty when it can.
QString nameProblem(const QString &name);

struct Move
{
    QString from; // absolute paths
    QString to;
};

struct Outcome
{
    QList<Move> done;    // the renames that were made (all of them, or none after a roll back)
    QStringList errors;
    bool rolledBack = false;
};

// Renames every file in moves. Names may swap or shift (a->b, b->c) and may change only in case:
// those files go through a temporary name first. If any rename fails, the ones already made are
// undone, so the folder ends as it was. progress (may be empty) gets the number of renames made.
Outcome apply(const QList<Move> &moves, const std::function<void(int done, int total)> &progress = {});

} // namespace BatchRename
