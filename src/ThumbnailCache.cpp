#include "ThumbnailCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace ThumbnailCache {

QString directory()
{
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/thumbnails");
    QDir().mkpath(cacheDirectory);
    return cacheDirectory;
}

qint64 sizeBytes()
{
    qint64 total = 0;
    const QFileInfoList files = QDir(directory()).entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    for (const QFileInfo &file : files) {
        total += file.size();
    }
    return total;
}

int clear()
{
    int removed = 0;
    const QFileInfoList files = QDir(directory()).entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    for (const QFileInfo &file : files) {
        if (QFile::remove(file.absoluteFilePath())) {
            ++removed;
        }
    }
    return removed;
}

void prune(qint64 limitBytes)
{
    if (limitBytes <= 0) {
        return;
    }
    QFileInfoList files = QDir(directory()).entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    qint64 total = 0;
    for (const QFileInfo &file : files) {
        total += file.size();
    }
    if (total <= limitBytes) {
        return;
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (const QFileInfo &file : files) {
        if (total <= limitBytes) {
            break;
        }
        const qint64 size = file.size();
        if (QFile::remove(file.absoluteFilePath())) {
            total -= size;
        }
    }
}

} // namespace ThumbnailCache
