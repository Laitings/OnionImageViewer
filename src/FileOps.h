#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

class QMimeData;
class QThread;

// Copy, cut and paste of files through the system clipboard, the way Explorer does it: files
// copied here paste in Explorer and the other way round, and Explorer's Cut moves them.
namespace FileOps {

// Clipboard data for files: file URLs (CF_HDROP on Windows) plus the paths as text. With cut,
// it is marked as a move (Explorer's "Preferred DropEffect").
QMimeData *makeMimeData(const QStringList &paths, bool cut);

// The local files and folders on the clipboard, and whether they were cut (to be moved).
// False when there are none.
bool readMimeData(const QMimeData *mime, QStringList *paths, bool *cut);

// "name - Copy.ext", "name - Copy (2).ext", ...: a free name for a copy in the same folder.
// Folders keep their whole name (no extension split).
QString copyName(const QString &folder, const QString &fileName, bool isDir);
// "name (2).ext", "name (3).ext", ...: a free name next to an existing file ("Keep both").
QString keepBothName(const QString &folder, const QString &fileName, bool isDir);

// True if folder is path itself or inside it (pasting a folder into itself).
bool isSameOrInside(const QString &folder, const QString &path);

struct Item
{
    QString source;
    QString target;       // full path of the new file or folder
    bool replace = false; // overwrite what is at target (folders are merged)
};

// Copies or moves the items on a background thread. Poll progress from the GUI thread; finished()
// is emitted there when done.
class Job final : public QObject
{
    Q_OBJECT

public:
    Job(const QList<Item> &items, bool move, QObject *parent = nullptr);
    ~Job() override;

    void start();
    void cancel() { m_cancel = true; }
    bool isCanceled() const { return m_cancel; }

    // Progress, safe to read while the job runs.
    qint64 totalBytes() const { return m_totalBytes; }
    qint64 doneBytes() const { return m_doneBytes; }
    int totalFiles() const { return m_totalFiles; }
    int doneFiles() const { return m_doneFiles; }
    bool counting() const { return m_counting; }

    // After finished(): what went wrong, and the top-level targets that now exist.
    QStringList errors() const { return m_errors; }
    QStringList created() const { return m_created; }
    bool move() const { return m_move; }

    // Runs the job on the calling thread (used by start(), and by tests).
    void run();

signals:
    void finished();

private:
    void count(const QString &path);
    bool copyFile(const QString &source, const QString &target, bool replace);
    bool copyTree(const QString &source, const QString &target, bool replace);
    bool removeTree(const QString &path);

    QList<Item> m_items;
    bool m_move = false;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_counting{true};
    std::atomic<qint64> m_totalBytes{0};
    std::atomic<qint64> m_doneBytes{0};
    std::atomic<int> m_totalFiles{0};
    std::atomic<int> m_doneFiles{0};
    QStringList m_errors;
    QStringList m_created;
};

} // namespace FileOps
