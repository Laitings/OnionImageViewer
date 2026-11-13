#include "BatchRename.h"

#include "Exif.h"
#include "HeifReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace BatchRename {
namespace {

struct NameParts
{
    QString base;
    QString ext; // without the dot
};

NameParts splitName(const QString &name, bool isFolder)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (isFolder || dot <= 0) {
        return {name, QString()};
    }
    return {name.left(dot), name.mid(dot + 1)};
}

// Splits "A001_C002.1001" into "A001_C002." and "1001".
NameParts splitFrame(const QString &base)
{
    qsizetype start = base.size();
    while (start > 0 && base.at(start - 1) >= QLatin1Char('0') && base.at(start - 1) <= QLatin1Char('9')) {
        --start;
    }
    return {base.left(start), base.mid(start)};
}

bool isSeparator(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char(' ');
}

QString changeCase(const QString &text, CaseChange change)
{
    switch (change) {
    case CaseChange::Lower:
        return text.toLower();
    case CaseChange::Upper:
        return text.toUpper();
    case CaseChange::Title: {
        QString result = text.toLower();
        bool wordStart = true;
        for (QChar &c : result) {
            if (c.isLetter()) {
                if (wordStart) {
                    c = c.toUpper();
                }
                wordStart = false;
            } else {
                wordStart = !c.isDigit();
            }
        }
        return result;
    }
    case CaseChange::Keep:
        break;
    }
    return text;
}

QString removeRange(const QString &text, const Options &options)
{
    const qsizetype size = text.size();
    qsizetype first = qMin(options.removeFrom, options.removeTo);
    qsizetype last = qMax(options.removeFrom, options.removeTo);
    if (first < 1 || first > size) {
        return text;
    }
    last = qMin(last, size);
    // Positions are 1-based; from the end, position 1 is the last character.
    const qsizetype start = options.removeFromEnd ? size - last : first - 1;
    QString result = text;
    result.remove(start, last - first + 1);
    return result;
}

struct Context
{
    QString name;
    QString ext;
    QString frame;
    QString folder;
    qint64 counter = 0;
    QDateTime taken;
    QDateTime modified;
};

// Fills in the tokens of pattern. Sets error (and returns nothing) for a token it doesn't know.
QString expand(const QString &pattern, const Context &context, const Options &options, QString *error)
{
    QString result;
    qsizetype i = 0;
    while (i < pattern.size()) {
        const QChar c = pattern.at(i);
        if (c != QLatin1Char('{')) {
            result += c;
            ++i;
            continue;
        }
        const qsizetype close = pattern.indexOf(QLatin1Char('}'), i + 1);
        if (close < 0) {
            *error = QStringLiteral("The pattern has a { without a closing }");
            return QString();
        }
        const QString token = pattern.mid(i + 1, close - i - 1);
        i = close + 1;
        const qsizetype colon = token.indexOf(QLatin1Char(':'));
        const QString name = (colon < 0 ? token : token.left(colon)).trimmed().toLower();
        const QString argument = colon < 0 ? QString() : token.mid(colon + 1);
        if (name == QLatin1String("name")) {
            result += context.name;
        } else if (name == QLatin1String("ext")) {
            result += context.ext;
        } else if (name == QLatin1String("frame")) {
            result += context.frame;
        } else if (name == QLatin1String("folder")) {
            result += context.folder;
        } else if (name == QLatin1String("n")) {
            int padding = options.counterPadding;
            if (!argument.isEmpty()) {
                bool ok = false;
                padding = argument.toInt(&ok);
                if (!ok || padding < 0 || padding > 12) {
                    *error = QStringLiteral("{n:%1}: put the number of digits after the colon, like {n:4}").arg(argument);
                    return QString();
                }
            }
            result += QStringLiteral("%1").arg(context.counter, padding, 10, QLatin1Char('0'));
        } else if (name == QLatin1String("date") || name == QLatin1String("modified")) {
            const QDateTime when = name == QLatin1String("date") && context.taken.isValid() ? context.taken : context.modified;
            const QString format = argument.isEmpty() ? QStringLiteral("yyyy-MM-dd") : argument;
            result += QLocale::c().toString(when, format);
        } else {
            *error = QStringLiteral("Unknown token {%1}").arg(token);
            return QString();
        }
    }
    return result;
}

QString nativeName(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

} // namespace

QString key(const QString &folder, const QString &name)
{
    return (QDir::cleanPath(folder) + QLatin1Char('/') + name).toLower();
}

QString nameProblem(const QString &name)
{
    if (name.isEmpty()) {
        return QStringLiteral("The new name is empty");
    }
    if (name.size() > 255) {
        return QStringLiteral("The name is longer than 255 characters");
    }
    static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (const QChar c : name) {
        if (c.unicode() < 32) {
            return QStringLiteral("Windows doesn't allow control characters in file names");
        }
        if (forbidden.contains(c)) {
            return QStringLiteral("Windows doesn't allow %1 in file names").arg(c);
        }
    }
    if (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.'))) {
        return QStringLiteral("Windows doesn't allow a name that ends in a space or a dot");
    }
    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9"),
    };
    const QString device = name.section(QLatin1Char('.'), 0, 0).trimmed().toUpper();
    if (reserved.contains(device)) {
        return QStringLiteral("%1 is a reserved name in Windows").arg(device);
    }
    return QString();
}

QList<Result> plan(const QList<Source> &sources, const Options &options, const QSet<QString> &existing)
{
    QList<Result> results;
    results.reserve(sources.size());

    struct Parsed
    {
        QString folder;
        QString folderName;
        QString name;
        NameParts parts;
        NameParts frame;
    };
    QList<Parsed> parsed;
    parsed.reserve(sources.size());
    for (const Source &source : sources) {
        const QFileInfo info(source.path);
        Parsed p;
        p.folder = info.absolutePath();
        p.folderName = QFileInfo(p.folder).fileName();
        p.name = info.fileName();
        p.parts = splitName(p.name, source.isFolder);
        if (!source.isFolder) {
            p.frame = splitFrame(p.parts.base);
        }
        parsed.append(p);
    }

    // Image sequences: two or more files with the same folder, prefix and extension, each ending
    // in a frame number. A single file named like name.0001.ext counts as a sequence too.
    QList<int> sequenceOf(sources.size(), -1);
    if (options.keepFrameNumbers) {
        QHash<QString, QList<int>> groups;
        QStringList order;
        for (int i = 0; i < parsed.size(); ++i) {
            if (parsed.at(i).frame.ext.isEmpty()) {
                continue;
            }
            const QString groupKey = key(parsed.at(i).folder, parsed.at(i).frame.base) + QLatin1Char('|') + parsed.at(i).parts.ext.toLower();
            if (!groups.contains(groupKey)) {
                order.append(groupKey);
            }
            groups[groupKey].append(i);
        }
        int next = 0;
        for (const QString &groupKey : order) {
            const QList<int> &members = groups.value(groupKey);
            if (members.size() < 2 && !parsed.at(members.first()).frame.base.endsWith(QLatin1Char('.'))) {
                continue;
            }
            for (int index : members) {
                sequenceOf[index] = next;
            }
            ++next;
        }
    }

    // One counter value per file, or per sequence, in the order given.
    QList<qint64> counterOf(sources.size(), 0);
    {
        QHash<int, qint64> sequenceCounter;
        qint64 unit = 0;
        for (int i = 0; i < sources.size(); ++i) {
            const int sequence = sequenceOf.at(i);
            if (sequence >= 0 && sequenceCounter.contains(sequence)) {
                counterOf[i] = sequenceCounter.value(sequence);
                continue;
            }
            counterOf[i] = options.counterStart + unit * options.counterStep;
            if (sequence >= 0) {
                sequenceCounter.insert(sequence, counterOf.at(i));
            }
            ++unit;
        }
    }

    QString globalError;
    QRegularExpression expression;
    if (!options.find.isEmpty() && options.regex) {
        expression.setPattern(options.find);
        if (!options.matchCase) {
            expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }
        if (!expression.isValid()) {
            globalError = QStringLiteral("Find: %1").arg(expression.errorString());
        }
    }

    for (int i = 0; i < sources.size(); ++i) {
        const Parsed &p = parsed.at(i);
        const Source &source = sources.at(i);
        Result result;
        result.oldName = p.name;
        result.sequence = sequenceOf.at(i);
        const bool inSequence = result.sequence >= 0;

        QString separator;
        Context context;
        context.ext = p.parts.ext;
        context.frame = p.frame.ext;
        context.folder = p.folderName;
        context.counter = counterOf.at(i);
        context.taken = source.taken;
        context.modified = source.modified;
        context.name = p.parts.base;
        if (inSequence) {
            context.name = p.frame.base;
            if (!context.name.isEmpty() && isSeparator(context.name.back())) {
                separator = context.name.back();
                context.name.chop(1);
            }
        }

        QString error = globalError;
        QString stem;
        if (error.isEmpty()) {
            stem = expand(options.pattern, context, options, &error);
        }
        if (error.isEmpty() && options.removeEnabled) {
            stem = removeRange(stem, options);
        }
        if (error.isEmpty() && !options.find.isEmpty()) {
            if (options.regex) {
                stem.replace(expression, options.replace);
            } else {
                stem.replace(options.find, options.replace, options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive);
            }
        }
        stem = changeCase(stem, options.nameCase);

        QString extension = p.parts.ext;
        if (options.extensionCase == CaseChange::Lower || options.extensionCase == CaseChange::Upper) {
            extension = changeCase(extension, options.extensionCase);
        }
        QString newName = stem;
        if (inSequence) {
            newName += (stem.isEmpty() ? QString() : separator) + context.frame;
        }
        if (!extension.isEmpty()) {
            newName += QLatin1Char('.') + extension;
        }
        result.newName = error.isEmpty() ? newName : p.name;

        if (!error.isEmpty()) {
            result.status = Status::Problem;
            result.problem = error;
        } else if (newName == p.name) {
            result.status = Status::Unchanged;
        } else if (!inSequence && stem.isEmpty()) {
            result.status = Status::Problem;
            result.problem = QStringLiteral("The new name is empty");
        } else {
            result.problem = nameProblem(newName);
            result.status = result.problem.isEmpty() ? Status::Rename : Status::Problem;
        }
        results.append(result);
    }

    // Conflicts: two files ending with the same name, or a new name that another file has.
    QSet<QString> sourceKeys;
    QHash<QString, int> finalCount;
    for (int i = 0; i < results.size(); ++i) {
        const QString folder = parsed.at(i).folder;
        sourceKeys.insert(key(folder, results.at(i).oldName));
        const Result &r = results.at(i);
        finalCount[key(folder, r.status == Status::Rename ? r.newName : r.oldName)] += 1;
    }
    for (int i = 0; i < results.size(); ++i) {
        Result &r = results[i];
        if (r.status != Status::Rename) {
            continue;
        }
        const QString newKey = key(parsed.at(i).folder, r.newName);
        if (finalCount.value(newKey) > 1) {
            r.status = Status::Problem;
            r.problem = QStringLiteral("Another file in the list gets the same name");
        } else if (existing.contains(newKey) && !sourceKeys.contains(newKey)) {
            r.status = Status::Problem;
            r.problem = QStringLiteral("A file with this name already exists");
        }
    }
    return results;
}

bool usesDateTaken(const QString &pattern)
{
    static const QRegularExpression token(QStringLiteral("\\{\\s*date\\s*(:[^}]*)?\\}"), QRegularExpression::CaseInsensitiveOption);
    return token.match(pattern).hasMatch();
}

QDateTime readDateTaken(const QString &path)
{
    if (Heif::isHeifFile(path)) {
        return Exif::dateTaken(Heif::exifData(path));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QDateTime();
    }
    QByteArray head = file.read(64 * 1024);
    if (head.startsWith("II*") || head.startsWith(QByteArray("MM\0*", 4))) {
        return Exif::dateTaken(head);
    }
    if (!head.startsWith("\xFF\xD8")) {
        return QDateTime();
    }
    // JPEG: walk the markers up to the Exif APP1 segment, which comes before the image data.
    qsizetype pos = 2;
    const auto byte = [&head](qsizetype at) { return static_cast<uchar>(head.at(at)); };
    while (pos + 4 <= head.size()) {
        if (byte(pos) != 0xFF) {
            break;
        }
        const uchar marker = byte(pos + 1);
        if (marker == 0xFF) {
            ++pos;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) {
            break;
        }
        const int length = (byte(pos + 2) << 8) | byte(pos + 3);
        if (length < 2) {
            break;
        }
        if (marker == 0xE1) {
            if (pos + 2 + length > head.size()) {
                head += file.read(pos + 2 + length - head.size());
            }
            const QByteArray segment = head.mid(pos + 4, length - 2);
            if (segment.startsWith(QByteArray("Exif\0\0", 6))) {
                return Exif::dateTaken(segment);
            }
        }
        pos += 2 + length;
        if (pos + 4 > head.size() && pos < 1024 * 1024) {
            head += file.read(pos + 4 - head.size() + 4096);
        }
    }
    return QDateTime();
}

bool looksLikeFrameSequence(const QStringList &paths)
{
    QHash<QString, int> counts;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const NameParts parts = splitName(info.fileName(), false);
        const NameParts frame = splitFrame(parts.base);
        if (frame.ext.isEmpty()) {
            continue;
        }
        const QString ext = parts.ext.toLower();
        const bool filmFormat = ext == QLatin1String("dpx") || ext == QLatin1String("exr") || ext == QLatin1String("cin")
                                || ext == QLatin1String("ari") || ext == QLatin1String("arx");
        const bool dotted = frame.base.endsWith(QLatin1Char('.'));
        if (!filmFormat && !dotted) {
            continue;
        }
        if (dotted || ++counts[key(info.absolutePath(), frame.base) + QLatin1Char('|') + ext] >= 2) {
            return true;
        }
    }
    return false;
}

Outcome apply(const QList<Move> &moves, const std::function<void(int, int)> &progress)
{
    Outcome outcome;
    // A file whose name another file (or itself, for a change of case) is taking moves to a
    // temporary name first, so every target is free when it is renamed.
    QSet<QString> targets;
    for (const Move &move : moves) {
        targets.insert(QDir::cleanPath(move.to).toLower());
    }
    struct Step
    {
        Move move;
        QString temporary;
    };
    QList<Step> steps;
    int total = 0;
    const QString tag = QString::number(QRandomGenerator::global()->generate64(), 36);
    for (int i = 0; i < moves.size(); ++i) {
        Step step;
        step.move = moves.at(i);
        if (targets.contains(QDir::cleanPath(step.move.from).toLower())) {
            step.temporary = QFileInfo(step.move.from).absolutePath()
                             + QStringLiteral("/~onionrename-%1-%2.tmp").arg(tag).arg(i);
            ++total;
        }
        ++total;
        steps.append(step);
    }

    QList<Move> made;
    const auto rename = [&](const QString &from, const QString &to) {
        if (QFileInfo::exists(to)) {
            outcome.errors.append(QStringLiteral("%1 already exists").arg(nativeName(to)));
            return false;
        }
        if (!QDir().rename(from, to)) {
            outcome.errors.append(QStringLiteral("Could not rename %1 to %2 (is it open in another program?)")
                                      .arg(nativeName(from), QFileInfo(to).fileName()));
            return false;
        }
        made.append({from, to});
        if (progress) {
            progress(int(made.size()), total);
        }
        return true;
    };

    bool ok = true;
    for (const Step &step : steps) {
        if (!step.temporary.isEmpty() && !(ok = rename(step.move.from, step.temporary))) {
            break;
        }
    }
    if (ok) {
        for (const Step &step : steps) {
            if (!(ok = rename(step.temporary.isEmpty() ? step.move.from : step.temporary, step.move.to))) {
                break;
            }
        }
    }
    if (ok) {
        outcome.done = moves;
        return outcome;
    }

    // Undo in reverse order, which always works: each step finds the name it needs free.
    outcome.rolledBack = true;
    for (qsizetype i = made.size() - 1; i >= 0; --i) {
        if (!QDir().rename(made.at(i).to, made.at(i).from)) {
            outcome.errors.append(QStringLiteral("Could not put back %1 (it is now called %2)")
                                      .arg(nativeName(made.at(i).from), QFileInfo(made.at(i).to).fileName()));
        }
    }
    return outcome;
}

} // namespace BatchRename
