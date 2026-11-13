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
    QVector<int> frames;
    if (index < 0 || index >= paths.size()) {
        return frames;
    }
    const QString wanted = key(paths.at(index));
    if (wanted.isEmpty()) {
        return frames;
    }
    // Cheap test first: the sequence's files all share the extension.
    const QString suffix = split(QFileInfo(paths.at(index)).fileName()).suffix;
    QVector<QString> numbers; // frame number of each entry in frames
    for (int i = 0; i < paths.size(); ++i) {
        if (i != index && !paths.at(i).endsWith(suffix, Qt::CaseInsensitive)) {
            continue;
        }
        if (i == index || key(paths.at(i)) == wanted) {
            frames.append(i);
        }
    }
    if (frames.size() < 2) {
        return {};
    }
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
    });
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
    return QStringLiteral("%1[%2-%3]%4").arg(first.prefix, first.frame, last.frame, first.suffix);
}

} // namespace Sequence
