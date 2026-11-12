#pragma once

#include <QWidget>

class QFileSystemModel;
class QListWidget;
class QListWidgetItem;
class QModelIndex;
class QTreeView;

class FolderTreePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit FolderTreePanel(QWidget *parent = nullptr);

    // Selects and reveals a folder in the tree without emitting folderSelected (for following
    // navigation done elsewhere, e.g. in the file grid).
    void showFolder(const QString &folderPath);

signals:
    void folderSelected(const QString &folderPath);

private:
    void addQuickLocation(const QString &label, const QString &path);
    void populateQuickLocations();
    void handleClicked(const QModelIndex &index);
    void handleQuickLocationClicked(QListWidgetItem *item);
    void selectFolder(const QString &folderPath);

    QFileSystemModel *m_model = nullptr;
    QListWidget *m_quickLocations = nullptr;
    QTreeView *m_treeView = nullptr;
};
