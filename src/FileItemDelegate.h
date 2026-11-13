#pragma once

#include <QPointer>
#include <QStyledItemDelegate>

class QHeaderView;

// Draws the file grid's items in the Details and Tiles views (the icon and list views use the
// standard drawing). Details lines the columns up with a header shown above the list.
class FileItemDelegate final : public QStyledItemDelegate
{
    Q_OBJECT

public:
    enum class Layout { Standard, Details, Tiles };

    // Extra text shown in Details and Tiles, set on each item.
    static constexpr int ModifiedTextRole = Qt::UserRole + 20;
    static constexpr int TypeTextRole = Qt::UserRole + 21;
    static constexpr int SizeTextRole = Qt::UserRole + 22;

    // Details columns, in header order.
    enum Column { NameColumn, ModifiedColumn, TypeColumn, SizeColumn, ColumnCount };

    static constexpr int DetailsRowHeight = 26;
    static constexpr int DetailsIconSize = 20;
    static constexpr int TileWidth = 300;
    static constexpr int TileHeight = 84;
    static constexpr int TileIconSize = 72;

    explicit FileItemDelegate(QObject *parent = nullptr);

    void setLayout(Layout layout) { m_layout = layout; }
    // In the icon view every item gets the same box (icon on top, name below), whatever shape
    // its thumbnail has. Otherwise the grid sizes all items from whichever it measured first.
    void setStandardItemSize(const QSize &size) { m_standardItemSize = size; }
    Layout layout() const { return m_layout; }
    void setHeader(QHeaderView *header);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    void paintDetails(QPainter *painter, const QStyleOptionViewItem &option) const;
    void paintTiles(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const;

    Layout m_layout = Layout::Standard;
    QSize m_standardItemSize;
    QPointer<QHeaderView> m_header;
};
