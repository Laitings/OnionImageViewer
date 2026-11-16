#pragma once

#include <QString>
#include <QtGlobal>

// The folder of cached folder thumbnails (PNG files named by a hash of the file and the view),
// under the app's cache location. Nothing is cached for OneDrive online-only files.
namespace ThumbnailCache {

// The cache folder; created when missing.
QString directory();
// Total size of the cached thumbnails.
qint64 sizeBytes();
// Deletes every cached thumbnail. Returns how many files were removed.
int clear();
// Deletes the least recently modified thumbnails until the cache is at most limitBytes.
// A limit of 0 or less means no limit and removes nothing.
void prune(qint64 limitBytes);

} // namespace ThumbnailCache
