#pragma once

#include <QStringList>
#include <QWidget>

class QFileSystemModel;
class QMenu;
class QResizeEvent;
class QModelIndex;
class QPoint;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;

// The folder panel left of the file grid, laid out like Explorer's navigation pane: a
// "Quick access" section with the folders the user pinned, then "This PC" with the drive tree.
// Both sections fold away. Pins are added and removed from the right-click menus (here and in
// the grid, through pinFolder/unpinFolder) and reordered by dragging; the owner stores them.
class FolderTreePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit FolderTreePanel(QWidget *parent = nullptr);

    // Selects and reveals a folder without emitting folderSelected (for following navigation done
    // elsewhere, e.g. in the file grid). A pinned folder is highlighted in Quick access, any other
    // one in the drive tree.
    void showFolder(const QString &folderPath);

    // The pinned folders, in the order shown. Setting them replaces the list without emitting
    // pinnedFoldersChanged.
    QStringList pinnedFolders() const;
    void setPinnedFolders(const QStringList &folderPaths);
    bool isPinned(const QString &folderPath) const;
    void pinFolder(const QString &folderPath);
    void unpinFolder(const QString &folderPath);

    // What a fresh install starts with, like Explorer: the OneDrive folders, Desktop, Downloads,
    // Documents and Pictures, where they exist.
    static QStringList defaultPinnedFolders();

    bool isQuickAccessExpanded() const;
    void setQuickAccessExpanded(bool expanded);
    bool isThisPcExpanded() const;
    void setThisPcExpanded(bool expanded);

signals:
    void folderSelected(const QString &folderPath);
    // The pins changed through the user (pin, unpin or reorder).
    void pinnedFoldersChanged(const QStringList &folderPaths);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QTreeWidgetItem *addPinItem(const QString &folderPath, int position = -1);
    QTreeWidgetItem *pinItem(const QString &folderPath) const;
    void movePin(QTreeWidgetItem *item, int offset);
    void emitPinsChanged();
    void updateSectionHeight();
    void handleTreeClicked(const QModelIndex &index);
    void handleSectionClicked(QTreeWidgetItem *item);
    void showSectionContextMenu(const QPoint &pos);
    void showTreeContextMenu(const QPoint &pos);
    void addFolderActions(QMenu *menu, const QString &folderPath);
    void highlightFolder(const QString &folderPath, bool preferPin);
    void selectFolder(const QString &folderPath, bool fromPin);

    QFileSystemModel *m_model = nullptr;
    QTreeWidget *m_sections = nullptr;
    QTreeWidgetItem *m_quickAccessItem = nullptr;
    QTreeWidgetItem *m_thisPcItem = nullptr;
    QWidget *m_treeContainer = nullptr;
    QTreeView *m_treeView = nullptr;
    QString m_currentFolder;
};
