#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Matches file names against the search field. Plain text matches anywhere in the name, as before.
// A query containing * or ? is read Explorer-style: one or more patterns separated by ; or spaces,
// each matched against the whole name (so "*.psd" means names ending in .psd), and a name is shown
// when any pattern matches. Matching ignores case. Patterns are compiled once per query.
class NameFilter final
{
public:
    explicit NameFilter(const QString &query)
    {
        const QString trimmed = query.trimmed();
        if (!trimmed.contains(QLatin1Char('*')) && !trimmed.contains(QLatin1Char('?'))) {
            m_text = trimmed;
            return;
        }
        static const QRegularExpression separators(QStringLiteral("[;\\s]+"));
        for (const QString &term : trimmed.split(separators, Qt::SkipEmptyParts)) {
            if (term.contains(QLatin1Char('*')) || term.contains(QLatin1Char('?'))) {
                // Only * and ? are special; brackets are common in frame-range names like shot[0001-0100].exr.
                QString regex = QRegularExpression::escape(term);
                regex.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
                regex.replace(QStringLiteral("\\?"), QStringLiteral("."));
                QRegularExpression pattern(QRegularExpression::anchoredPattern(regex),
                                           QRegularExpression::CaseInsensitiveOption
                                               | QRegularExpression::DotMatchesEverythingOption);
                if (pattern.isValid()) {
                    pattern.optimize();
                    m_patterns.append(pattern);
                }
            } else {
                m_terms.append(term);
            }
        }
    }

    bool matches(const QString &name) const
    {
        if (m_patterns.isEmpty() && m_terms.isEmpty()) {
            return name.contains(m_text, Qt::CaseInsensitive);
        }
        for (const QRegularExpression &pattern : m_patterns) {
            if (pattern.match(name).hasMatch()) {
                return true;
            }
        }
        for (const QString &term : m_terms) {
            if (name.contains(term, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

private:
    QString m_text;
    QList<QRegularExpression> m_patterns;
    QStringList m_terms;
};
