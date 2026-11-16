#pragma once

#include <QChar>
#include <QString>

// Small helpers for text read out of files.
namespace TextUtil {

// Text from a file field that may carry padding: cut at the first NUL, drop other control
// characters and trim. ICC profile descriptions are the usual case: some profiles pad the name
// with NULs, which showed up as a row of boxes after "sRGB" in the metadata panel.
inline QString printable(QString text)
{
    const qsizetype nul = text.indexOf(QChar(0));
    if (nul >= 0) {
        text.truncate(nul);
    }
    QString result;
    result.reserve(text.size());
    for (const QChar character : std::as_const(text)) {
        if (!character.isNonCharacter() && character.category() != QChar::Other_Control) {
            result.append(character);
        }
    }
    return result.trimmed();
}

} // namespace TextUtil
