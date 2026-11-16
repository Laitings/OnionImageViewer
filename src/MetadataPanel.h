#pragma once

#include "Metadata.h"

#include <QSet>
#include <QWidget>

class QLabel;
class QMenu;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Side panel listing a file's metadata (see Metadata::read), grouped, with a filter box.
class MetadataPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void showEntries(const QString &filePath, const QList<Metadata::Entry> &entries);
    void showMessage(const QString &message);
    QString filePath() const { return m_filePath; }

private:
    void rebuild();
    void applyFilter();
    void showContextMenu(const QPoint &pos);
    void copyText(const QString &text);
    // The entries the filter box lets through (all of them when it is empty).
    QList<Metadata::Entry> shownEntries() const;
    // Adds "Copy as" entries (tab-separated, aligned columns, Markdown, CSV) for the shown entries.
    void addCopyAsActions(QMenu *menu);

    QLabel *m_titleLabel = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QTreeWidget *m_tree = nullptr;
    QString m_filePath;
    QList<Metadata::Entry> m_entries;
    QSet<QString> m_collapsedGroups; // kept from file to file
};
