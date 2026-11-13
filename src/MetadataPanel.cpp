#include "MetadataPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
const int IsGroupRole = Qt::UserRole + 1;
}

MetadataPanel::MetadataPanel(QWidget *parent)
    : QWidget(parent)
    , m_titleLabel(new QLabel(this))
    , m_filterEdit(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
{
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    m_filterEdit->setPlaceholderText(QStringLiteral("Filter (e.g. camera, lens, timecode)"));
    m_filterEdit->setClearButtonEnabled(true);

    auto *copyButton = new QToolButton(this);
    copyButton->setText(QStringLiteral("Copy All"));
    copyButton->setToolTip(QStringLiteral("Copy the shown metadata as tab-separated text (pastes into a spreadsheet)"));

    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setTextElideMode(Qt::ElideMiddle);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setIndentation(12);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // long values are elided; hover for all of it

    auto *filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(m_filterEdit, 1);
    filterRow->addWidget(copyButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 0); // flush with the bottom of the file grid
    layout->addWidget(m_titleLabel);
    layout->addLayout(filterRow);
    layout->addWidget(m_tree, 1);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &MetadataPanel::applyFilter);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &MetadataPanel::showContextMenu);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        m_collapsedGroups.insert(item->text(0));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        m_collapsedGroups.remove(item->text(0));
    });
    connect(copyButton, &QToolButton::clicked, this, [this]() {
        // Only what the filter shows.
        QList<Metadata::Entry> shown;
        const QString filter = m_filterEdit->text().trimmed();
        for (const Metadata::Entry &entry : std::as_const(m_entries)) {
            if (filter.isEmpty() || entry.key.contains(filter, Qt::CaseInsensitive)
                || entry.value.contains(filter, Qt::CaseInsensitive) || entry.group.contains(filter, Qt::CaseInsensitive)) {
                shown.append(entry);
            }
        }
        copyText(Metadata::toText(shown));
    });

    showMessage(QStringLiteral("Select an image to see its metadata."));
}

void MetadataPanel::showEntries(const QString &filePath, const QList<Metadata::Entry> &entries)
{
    m_filePath = filePath;
    m_entries = entries;
    m_titleLabel->setText(QFileInfo(filePath).fileName());
    m_titleLabel->setToolTip(filePath);
    rebuild();
}

void MetadataPanel::showMessage(const QString &message)
{
    m_filePath.clear();
    m_entries.clear();
    m_titleLabel->setText(message);
    m_titleLabel->setToolTip(QString());
    m_tree->clear();
}

void MetadataPanel::rebuild()
{
    const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    m_tree->setUpdatesEnabled(false);
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> groups;
    for (const Metadata::Entry &entry : std::as_const(m_entries)) {
        QTreeWidgetItem *group = groups.value(entry.group);
        if (!group) {
            group = new QTreeWidgetItem(m_tree, {entry.group});
            group->setData(0, IsGroupRole, true);
            group->setFirstColumnSpanned(true);
            QFont font = group->font(0);
            font.setBold(true);
            group->setFont(0, font);
            groups.insert(entry.group, group);
        }
        auto *item = new QTreeWidgetItem(group, {entry.key, entry.value});
        item->setToolTip(1, entry.value);
        item->setToolTip(0, entry.key);
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        it.value()->setExpanded(!m_collapsedGroups.contains(it.key()));
    }
    applyFilter();
    // Names as wide as they need, but leave at least half the width for the values.
    m_tree->resizeColumnToContents(0);
    m_tree->setColumnWidth(0, qMin(m_tree->columnWidth(0) + 8, m_tree->viewport()->width() / 2));
    m_tree->setUpdatesEnabled(true);
    // Stepping through a sequence: stay at the same spot in the list.
    m_tree->verticalScrollBar()->setValue(scroll);
}

void MetadataPanel::applyFilter()
{
    const QString filter = m_filterEdit->text().trimmed();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        const bool groupMatches = !filter.isEmpty() && group->text(0).contains(filter, Qt::CaseInsensitive);
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            const bool match = filter.isEmpty() || groupMatches || item->text(0).contains(filter, Qt::CaseInsensitive)
                || item->text(1).contains(filter, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible += match ? 1 : 0;
        }
        group->setHidden(visible == 0);
        if (!filter.isEmpty() && visible > 0) {
            group->setExpanded(true);
        }
    }
}

void MetadataPanel::showContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *clicked = m_tree->itemAt(pos);
    QMenu menu(this);
    if (clicked && !clicked->data(0, IsGroupRole).toBool()) {
        menu.addAction(QStringLiteral("Copy Value"), this, [this, clicked]() {
            copyText(clicked->text(1));
        });
    }
    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    if (!selected.isEmpty()) {
        menu.addAction(QStringLiteral("Copy Selected Rows"), this, [this, selected]() {
            QStringList lines;
            for (const QTreeWidgetItem *item : selected) {
                lines << (item->data(0, IsGroupRole).toBool() ? item->text(0)
                                                              : item->text(0) + QLatin1Char('\t') + item->text(1));
            }
            copyText(lines.join(QLatin1Char('\n')));
        });
    }
    menu.addAction(QStringLiteral("Copy All"), this, [this]() {
        copyText(Metadata::toText(m_entries));
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Expand All"), m_tree, &QTreeWidget::expandAll);
    menu.addAction(QStringLiteral("Collapse All"), m_tree, &QTreeWidget::collapseAll);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void MetadataPanel::copyText(const QString &text)
{
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}
