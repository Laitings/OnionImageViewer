#include "Sequence.h"

#include <QFileInfo>

#include <algorithm>

namespace Sequence {
namespace {

bool isDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// Compares frame numbers by value without converting them, so any number of digits works:
// leading zeros are skipped, then a longer number is the larger one.
int compareFrames(const QString &a, const QString &b)
{
    qsizetype i = 0;
    while (i + 1 < a.size() && a.at(i) == QLatin1Char('0')) {
        ++i;
    }
    qsizetype j = 0;
    while (j + 1 < b.size() && b.at(j) == QLatin1Char('0')) {
        ++j;
    }
    const qsizetype lengthA = a.size() - i;
    const qsizetype lengthB = b.size() - j;
    if (lengthA != lengthB) {
        return lengthA < lengthB ? -1 : 1;
    }
    const int byValue = QStringView(a).mid(i).compare(QStringView(b).mid(j));
    if (byValue != 0) {
        return byValue;
    }
    // Same value written differently ("1" and "001"): keep a stable order.
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

// The name with every run of digits replaced by '#', with the folder, compared without case:
// files that differ only in their numbers share it.
QString looseKey(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    QString pattern;
    pattern.reserve(name.size());
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (isDigit(name.at(i))) {
            if (i == 0 || !isDigit(name.at(i - 1))) {
                pattern += QLatin1Char('#');
            }
        } else {
            pattern += name.at(i);
        }
    }
    return (info.path() + QLatin1Char('/') + pattern).toCaseFolded();
}

// Collects the files of paths with the given key, as indices; index is always included.
QVector<int> collect(const QStringList &paths, int index, const QString &suffix, const QString &wanted,
                     QString (*keyOf)(const QString &))
{
    QVector<int> frames;
    for (int i = 0; i < paths.size(); ++i) {
        // Cheap test first: the sequence's files all share the extension.
        if (i != index && !paths.at(i).endsWith(suffix, Qt::CaseInsensitive)) {
            continue;
        }
        if (i == index || keyOf(paths.at(i)) == wanted) {
            frames.append(i);
        }
    }
    return frames;
}

} // namespace

Name split(const QString &fileName)
{
    qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        dot = fileName.size();
    }
    qsizetype start = dot;
    while (start > 0 && isDigit(fileName.at(start - 1))) {
        --start;
    }
    return {fileName.left(start), fileName.mid(start, dot - start), fileName.mid(dot)};
}

QString key(const QString &path)
{
    const QFileInfo info(path);
    const Name name = split(info.fileName());
    if (name.frame.isEmpty()) {
        return QString();
    }
    return (info.path() + QLatin1Char('/') + name.prefix + QLatin1Char('|') + name.suffix).toCaseFolded();
}

QVector<int> framesOf(const QStringList &paths, int index)
{
    if (index < 0 || index >= paths.size()) {
        return {};
    }
    const QString wanted = key(paths.at(index));
    if (wanted.isEmpty()) {
        return {};
    }
    const QString suffix = split(QFileInfo(paths.at(index)).fileName()).suffix;
    QVector<int> frames = collect(paths, index, suffix, wanted, &key);
    if (frames.size() < 2) {
        // Names where another number changes along with the frame number, such as
        // "Door_v0010001_00001.png", "Door_v0010002_00002.png": group files that differ only in
        // their numbers, still ordered by the last one.
        frames = collect(paths, index, suffix, looseKey(paths.at(index)), &looseKey);
    }
    if (frames.size() < 2) {
        return {};
    }
    QVector<QString> numbers; // frame number of each entry in frames
    numbers.reserve(frames.size());
    for (const int i : frames) {
        numbers.append(split(QFileInfo(paths.at(i)).fileName()).frame);
    }
    QVector<int> order(frames.size());
    for (int i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&numbers](int a, int b) {
        return compareFrames(numbers.at(a), numbers.at(b)) < 0;
    }); // equal frame numbers keep their (name) order
    QVector<int> sorted;
    sorted.reserve(frames.size());
    for (const int i : order) {
        sorted.append(frames.at(i));
    }
    return sorted;
}

QString displayName(const QStringList &paths, const QVector<int> &frames)
{
    if (frames.isEmpty()) {
        return QString();
    }
    const Name first = split(QFileInfo(paths.at(frames.first())).fileName());
    const Name last = split(QFileInfo(paths.at(frames.last())).fileName());
    QString prefix = first.prefix;
    if (first.prefix.compare(last.prefix, Qt::CaseInsensitive) != 0) {
        // Other numbers change too: show them as '#' ("Door_v#_[00001-00055].png").
        qsizetype start = 0;
        while (start < first.prefix.size() && start < last.prefix.size()
               && first.prefix.at(start) == last.prefix.at(start)) {
            ++start;
        }
        while (start > 0 && isDigit(first.prefix.at(start - 1))) {
            --start;
        }
        qsizetype end = 0;
        while (end < first.prefix.size() - start && end < last.prefix.size() - start
               && first.prefix.at(first.prefix.size() - 1 - end) == last.prefix.at(last.prefix.size() - 1 - end)) {
            ++end;
        }
        while (end > 0 && isDigit(first.prefix.at(first.prefix.size() - end))) {
            --end;
        }
        prefix = first.prefix.left(start) + QLatin1Char('#') + first.prefix.right(end);
    }
    return QStringLiteral("%1[%2-%3]%4").arg(prefix, first.frame, last.frame, first.suffix);
}

} // namespace Sequence
