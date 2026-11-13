#include "FileOps.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QSaveFile>
#include <QThread>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FileOps {

namespace {

// Explorer marks cut files with this clipboard format (a DWORD drop effect). Qt passes any Windows
// clipboard format through under this MIME name.
const QString DropEffectMime = QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\"");
// The same for GNOME/KDE file managers, so cut works on Linux too.
const QString GnomeMime = QStringLiteral("x-special/gnome-copied-files");
const QString KdeCutMime = QStringLiteral("application/x-kde-cutselection");

constexpr quint32 DropEffectCopy = 1;
constexpr quint32 DropEffectMove = 2;
constexpr quint32 DropEffectLink = 4;

const QDir::Filters AllEntries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;

QString freeName(const QString &folder, const QString &fileName, bool isDir, const QString &firstSuffix,
                 const QString &numberedSuffix, int firstNumber)
{
    const QFileInfo info(fileName);
    // Folders and dot files have no extension to keep apart.
    QString base = fileName;
    QString extension;
    const QString suffix = info.suffix();
    if (!suffix.isEmpty() && fileName.size() > suffix.size() + 1 && !isDir) {
        base = fileName.left(fileName.size() - suffix.size() - 1);
        extension = QLatin1Char('.') + suffix;
    }
    const QDir dir(folder);
    if (!firstSuffix.isNull()) {
        const QString candidate = base + firstSuffix + extension;
        if (!QFileInfo::exists(dir.filePath(candidate))) {
            return candidate;
        }
    }
    for (int number = firstNumber;; ++number) {
        const QString candidate = base + numberedSuffix.arg(number) + extension;
        if (!QFileInfo::exists(dir.filePath(candidate))) {
            return candidate;
        }
    }
}

} // namespace

QMimeData *makeMimeData(const QStringList &paths, bool cut)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    QStringList nativePaths;
    QByteArray gnome = cut ? QByteArrayLiteral("cut") : QByteArrayLiteral("copy");
    for (const QString &path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        nativePaths.append(QDir::toNativeSeparators(path));
        gnome += '\n' + url.toEncoded();
    }
    mime->setUrls(urls);
    mime->setText(nativePaths.join(QLatin1Char('\n')));

    const quint32 effect = cut ? DropEffectMove : (DropEffectCopy | DropEffectLink);
    QByteArray effectBytes(4, '\0');
    for (int i = 0; i < 4; ++i) {
        effectBytes[i] = char((effect >> (8 * i)) & 0xff); // little-endian DWORD
    }
    mime->setData(DropEffectMime, effectBytes);
#ifndef Q_OS_WIN
    mime->setData(GnomeMime, gnome);
    if (cut) {
        mime->setData(KdeCutMime, QByteArrayLiteral("1"));
    }
#endif
    return mime;
}

bool readMimeData(const QMimeData *mime, QStringList *paths, bool *cut)
{
    paths->clear();
    *cut = false;
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (!path.isEmpty() && !paths->contains(path)) {
                paths->append(path);
            }
        }
    }
    if (paths->isEmpty()) {
        return false;
    }

    if (mime->hasFormat(DropEffectMime)) {
        const QByteArray bytes = mime->data(DropEffectMime);
        if (bytes.size() >= 4) {
            quint32 effect = 0;
            for (int i = 0; i < 4; ++i) {
                effect |= quint32(quint8(bytes.at(i))) << (8 * i);
            }
            // Explorer: Copy puts COPY|LINK, Cut puts MOVE.
            *cut = (effect & DropEffectMove) && !(effect & DropEffectCopy);
        }
    } else if (mime->hasFormat(GnomeMime)) {
        *cut = mime->data(GnomeMime).startsWith("cut");
    } else if (mime->hasFormat(KdeCutMime)) {
        *cut = mime->data(KdeCutMime).startsWith('1');
    }
    return true;
}

QString copyName(const QString &folder, const QString &fileName, bool isDir)
{
    return freeName(folder, fileName, isDir, QStringLiteral(" - Copy"), QStringLiteral(" - Copy (%1)"), 2);
}

QString keepBothName(const QString &folder, const QString &fileName, bool isDir)
{
    return freeName(folder, fileName, isDir, QString(), QStringLiteral(" (%1)"), 2);
}

bool isSameOrInside(const QString &folder, const QString &path)
{
    const Qt::CaseSensitivity cs =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    const QString a = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    const QString b = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (a.compare(b, cs) == 0) {
        return true;
    }
    const QString prefix = b.endsWith(QLatin1Char('/')) ? b : b + QLatin1Char('/');
    return a.startsWith(prefix, cs);
}

Job::Job(const QList<Item> &items, bool move, QObject *parent)
    : QObject(parent)
    , m_items(items)
    , m_move(move)
{
}

Job::~Job()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait();
        delete m_thread;
    }
}

void Job::start()
{
    m_thread = QThread::create([this]() { run(); });
    connect(m_thread, &QThread::finished, this, &Job::finished);
    m_thread->start();
}

void Job::count(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink()) {
        m_totalBytes += info.size();
        ++m_totalFiles;
        return;
    }
    QDirIterator it(path, AllEntries, QDirIterator::Subdirectories);
    while (it.hasNext() && !m_cancel) {
        it.next();
        const QFileInfo entry = it.fileInfo();
        if (!entry.isDir() || entry.isSymLink()) {
            m_totalBytes += entry.size();
            ++m_totalFiles;
        }
    }
}

#ifdef Q_OS_WIN
namespace {
struct CopyProgress
{
    Job *job;
    std::atomic<qint64> *doneBytes;
    qint64 before;
};

DWORD CALLBACK copyProgressRoutine(LARGE_INTEGER, LARGE_INTEGER transferred, LARGE_INTEGER, LARGE_INTEGER,
                                   DWORD, DWORD, HANDLE, HANDLE, LPVOID data)
{
    auto *progress = static_cast<CopyProgress *>(data);
    *progress->doneBytes = progress->before + transferred.QuadPart;
    return progress->job->isCanceled() ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}
} // namespace
#endif

bool Job::copyFile(const QString &source, const QString &target, bool replace)
{
    const qint64 before = m_doneBytes;
    const qint64 size = QFileInfo(source).size();
#ifdef Q_OS_WIN
    // CopyFileEx keeps the file times and attributes, and lets the server copy the data itself
    // when both ends are on the same share.
    CopyProgress progress{this, &m_doneBytes, before};
    const std::wstring from = QDir::toNativeSeparators(source).toStdWString();
    const std::wstring to = QDir::toNativeSeparators(target).toStdWString();
    if (replace) {
        SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL); // a read-only target would refuse
    }
    if (!CopyFileExW(from.c_str(), to.c_str(), copyProgressRoutine, &progress, nullptr,
                     replace ? 0 : COPY_FILE_FAIL_IF_EXISTS)) {
        const DWORD error = GetLastError();
        if (error != ERROR_REQUEST_ABORTED) {
            wchar_t *message = nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, error, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
            m_errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(source),
                                                         message ? QString::fromWCharArray(message).trimmed()
                                                                 : QString::number(error)));
            LocalFree(message);
        }
        m_doneBytes = before + size;
        return false;
    }
#else
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        m_errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(source), in.errorString()));
        m_doneBytes = before + size;
        return false;
    }
    if (!replace && QFileInfo::exists(target)) {
        m_errors.append(QStringLiteral("%1: already exists").arg(QDir::toNativeSeparators(target)));
        m_doneBytes = before + size;
        return false;
    }
    // Written to a temporary file and renamed at the end, so a cancel or error leaves nothing
    // half-copied behind (and a replaced file stays intact until then).
    QSaveFile out(target);
    out.setDirectWriteFallback(true);
    if (!out.open(QIODevice::WriteOnly)) {
        m_errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(target), out.errorString()));
        m_doneBytes = before + size;
        return false;
    }
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
    while (!in.atEnd()) {
        if (m_cancel) {
            out.cancelWriting();
            return false;
        }
        const qint64 read = in.read(buffer.data(), buffer.size());
        if (read < 0 || out.write(buffer.constData(), read) != read) {
            m_errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(source),
                                                         read < 0 ? in.errorString() : out.errorString()));
            out.cancelWriting();
            m_doneBytes = before + size;
            return false;
        }
        m_doneBytes += read;
    }
    if (!out.commit()) {
        m_errors.append(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(target), out.errorString()));
        m_doneBytes = before + size;
        return false;
    }
    QFile copied(target);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(QFileInfo(source).lastModified(), QFileDevice::FileModificationTime);
    }
    copied.setPermissions(in.permissions());
#endif
    ++m_doneFiles;
    return true;
}

bool Job::copyTree(const QString &source, const QString &target, bool replace)
{
    const QFileInfo info(source);
    if (!info.isDir() || info.isSymLink()) {
        return copyFile(source, target, replace);
    }
    if (!QFileInfo(target).isDir() && !QDir().mkpath(target)) {
        m_errors.append(QStringLiteral("%1: could not create the folder").arg(QDir::toNativeSeparators(target)));
        return false;
    }
    bool ok = true;
    const QFileInfoList entries = QDir(source).entryInfoList(AllEntries, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (m_cancel) {
            return false;
        }
        ok = copyTree(entry.filePath(), QDir(target).filePath(entry.fileName()), replace) && ok;
    }
    return ok;
}

bool Job::removeTree(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        return QDir(path).removeRecursively();
    }
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::WriteOwner);
    return QFile::remove(path);
}

void Job::run()
{
    for (const Item &item : m_items) {
        count(item.source);
    }
    m_counting = false;

    for (const Item &item : m_items) {
        if (m_cancel) {
            break;
        }
        const QFileInfo sourceInfo(item.source);
        const bool isDir = sourceInfo.isDir() && !sourceInfo.isSymLink();
        if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
            m_errors.append(QStringLiteral("%1: not found").arg(QDir::toNativeSeparators(item.source)));
            continue;
        }

        if (m_move) {
            // Same drive: a rename, instant. A file being replaced is removed first; folders
            // being merged go through copy and delete.
            const bool targetExists = QFileInfo::exists(item.target);
            if (!targetExists || (!isDir && item.replace && !QFileInfo(item.target).isDir() && removeTree(item.target))) {
                if (QDir().rename(item.source, item.target)) {
                    if (isDir) {
                        QDirIterator it(item.target, AllEntries, QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            it.next();
                            if (!it.fileInfo().isDir()) {
                                m_doneBytes += it.fileInfo().size();
                                ++m_doneFiles;
                            }
                        }
                    } else {
                        m_doneBytes += sourceInfo.size();
                        ++m_doneFiles;
                    }
                    m_created.append(item.target);
                    continue;
                }
            }
        }

        const int errorsBefore = m_errors.size();
        const bool copied = copyTree(item.source, item.target, item.replace);
        if (QFileInfo::exists(item.target)) {
            m_created.append(item.target);
        }
        if (copied && m_move && !m_cancel && m_errors.size() == errorsBefore) {
            if (!removeTree(item.source)) {
                m_errors.append(QStringLiteral("%1: copied, but the original could not be removed")
                                    .arg(QDir::toNativeSeparators(item.source)));
            }
        }
    }
}

} // namespace FileOps
