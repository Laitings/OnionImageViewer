#include "FileItemDelegate.h"

#include <QApplication>
#include <QHash>
#include <QHeaderView>
#include <QImage>
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
        color.setAlphaF(0.62f);
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
        if (m_standardItemSize.isValid()) {
            paintStandard(painter, option, index);
        } else {
            QStyledItemDelegate::paint(painter, option, index);
        }
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

// Icon view: the icon sits in a square box (the view's icon size) and the name below it. Folder
// icons and small or wide thumbnails don't fill the box, which left the name far beneath the
// picture, so the icon is moved down until its visible part ends just above the name.
void FileItemDelegate::paintStandard(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QSize box = opt.decorationSize;
    const int boxTop = opt.rect.y() + 4;
    const QRect iconBox(opt.rect.x() + (opt.rect.width() - box.width()) / 2, boxTop, box.width(), box.height());

    // Where the visible part of the icon ends inside the box (cached per pixmap).
    int visibleBottom = box.height();
    const QPixmap pixmap = opt.icon.pixmap(box, iconMode(opt));
    if (!pixmap.isNull()) {
        static QHash<qint64, int> cache;
        const qint64 key = pixmap.cacheKey() ^ (qint64(box.width()) << 40) ^ (qint64(box.height()) << 20);
        auto found = cache.constFind(key);
        if (found == cache.constEnd()) {
            const qreal ratio = pixmap.devicePixelRatio();
            const QImage image = pixmap.toImage();
            int last = image.height() - 1;
            if (image.hasAlphaChannel()) {
                while (last > 0) {
                    bool empty = true;
                    for (int x = 0; x < image.width() && empty; ++x) {
                        empty = qAlpha(image.pixel(x, last)) < 16;
                    }
                    if (!empty) {
                        break;
                    }
                    --last;
                }
            }
            const int logicalHeight = int(image.height() / ratio);
            const int top = (box.height() - logicalHeight) / 2; // the icon is centred in the box
            if (cache.size() > 4000) {
                cache.clear();
            }
            found = cache.insert(key, top + int((last + 1) / ratio));
        }
        visibleBottom = found.value();
    }
    const int shift = qMax(0, box.height() - visibleBottom);

    painter->save();
    opt.icon.paint(painter, iconBox.translated(0, shift), Qt::AlignCenter, iconMode(opt));
    painter->restore();

    // The name and its selection highlight, in the space below the icon box.
    QStyleOptionViewItem text = opt;
    text.icon = QIcon();
    text.features &= ~QStyleOptionViewItem::HasDecoration;
    text.decorationSize = QSize();
    const int textTop = boxTop + box.height() + 2;
    text.rect = QRect(opt.rect.x(), textTop, opt.rect.width(), qMax(0, opt.rect.bottom() - textTop + 1));
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &text, painter, widget);
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
