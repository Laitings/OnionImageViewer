#pragma once

#include <QWidget>

class QFileSystemModel;
class QModelIndex;
class QTreeView;

class FolderTreePanel final : public QWidget
{
    Q_OBJECT

public:
    explicit FolderTreePanel(QWidget *parent = nullptr);

signals:
    void folderSelected(const QString &folderPath);

private:
    void handleClicked(const QModelIndex &index);

    QFileSystemModel *m_model = nullptr;
    QTreeView *m_treeView = nullptr;
};
