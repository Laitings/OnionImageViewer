#include "FileItemDelegate.h"

#include <QApplication>
#include <QHeaderView>
#include <QPainter>
#include <QStyle>

void FileItemDelegate::setHeader(QHeaderView *header)
{
    m_header = header;
}

FileItemDelegate::FileItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

namespace {

QIcon::Mode iconMode(const QStyleOptionViewItem &option)
{
    if (!(option.state & QStyle::State_Enabled)) {
        return QIcon::Disabled;
    }
    return QIcon::Normal; // thumbnails should keep their colors when selected
}

QColor textColor(const QStyleOptionViewItem &option, bool secondary)
{
    const bool selected = option.state & QStyle::State_Selected;
    QColor color = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    if (secondary && !selected) {
        color.setAlphaF(0.62);
    }
    return color;
}

void drawBackground(QPainter *painter, const QStyleOptionViewItem &option)
{
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, widget);
}

} // namespace

void FileItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    if (m_layout == Layout::Standard) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    painter->save();
    drawBackground(painter, opt);
    if (m_layout == Layout::Details) {
        paintDetails(painter, opt);
    } else {
        paintTiles(painter, opt, index);
    }
    painter->restore();
}

void FileItemDelegate::paintDetails(QPainter *painter, const QStyleOptionViewItem &opt) const
{
    if (!m_header) {
        return;
    }
    const QModelIndex &index = opt.index;
    const QRect row = opt.rect;
    const int padding = 6;

    auto columnRect = [&](int column) {
        return QRect(row.x() + m_header->sectionPosition(column) + padding, row.y(),
                     m_header->sectionSize(column) - 2 * padding, row.height());
    };

    // Name: icon, then the file name.
    const QRect nameRect = columnRect(NameColumn);
    const QRect iconRect(nameRect.x(), row.y() + (row.height() - DetailsIconSize) / 2, DetailsIconSize, DetailsIconSize);
    opt.icon.paint(painter, iconRect, Qt::AlignCenter, iconMode(opt));
    const QRect textRect = nameRect.adjusted(DetailsIconSize + 8, 0, 0, 0);
    painter->setFont(opt.font);
    painter->setPen(textColor(opt, false));
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, textRect.width()));

    const struct {
        int column;
        int role;
        Qt::Alignment alignment;
    } columns[] = {
        {ModifiedColumn, ModifiedTextRole, Qt::AlignLeft},
        {TypeColumn, TypeTextRole, Qt::AlignLeft},
        {SizeColumn, SizeTextRole, Qt::AlignRight},
    };
    painter->setPen(textColor(opt, true));
    for (const auto &column : columns) {
        if (m_header->isSectionHidden(column.column)) {
            continue;
        }
        const QRect rect = columnRect(column.column);
        const QString text = index.data(column.role).toString();
        painter->drawText(rect, Qt::AlignVCenter | column.alignment,
                          opt.fontMetrics.elidedText(text, Qt::ElideRight, rect.width()));
    }
}

void FileItemDelegate::paintTiles(QPainter *painter, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const QRect rect = opt.rect.adjusted(6, 6, -6, -6);
    const QRect iconRect(rect.x(), rect.y() + (rect.height() - TileIconSize) / 2, TileIconSize, TileIconSize);
    opt.icon.paint(painter, iconRect, Qt::AlignCenter, iconMode(opt));

    const QRect textRect = rect.adjusted(TileIconSize + 10, 0, 0, 0);
    const QFontMetrics &fm = opt.fontMetrics;
    QStringList lines = {opt.text};
    for (int role : {TypeTextRole, SizeTextRole}) {
        const QString text = index.data(role).toString();
        if (!text.isEmpty()) {
            lines.append(text);
        }
    }
    const int lineHeight = fm.height() + 1;
    int y = textRect.y() + (textRect.height() - lineHeight * int(lines.size())) / 2;
    painter->setFont(opt.font);
    for (int i = 0; i < lines.size(); ++i) {
        painter->setPen(textColor(opt, i > 0));
        painter->drawText(QRect(textRect.x(), y, textRect.width(), lineHeight), Qt::AlignVCenter | Qt::AlignLeft,
                          fm.elidedText(lines.at(i), Qt::ElideRight, textRect.width()));
        y += lineHeight;
    }
}

void FileItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    const QSize iconBox = option->decorationSize; // the view's icon size
    QStyledItemDelegate::initStyleOption(option, index);
    if (m_layout == Layout::Standard && m_standardItemSize.isValid()) {
        // Centre wide and tall thumbnails in the same square, so names line up along a row.
        option->decorationSize = iconBox;
    }
}

QSize FileItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    switch (m_layout) {
    case Layout::Details:
        return QSize(m_header ? m_header->length() : 600, DetailsRowHeight);
    case Layout::Tiles:
        return QSize(TileWidth, TileHeight);
    case Layout::Standard:
        if (m_standardItemSize.isValid()) {
            return m_standardItemSize;
        }
        break;
    }
    return QStyledItemDelegate::sizeHint(option, index);
}
