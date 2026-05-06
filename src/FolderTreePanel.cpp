#include "FolderTreePanel.h"

#include <QDir>
#include <QFileSystemModel>
#include <QTreeView>
#include <QVBoxLayout>

FolderTreePanel::FolderTreePanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_treeView(new QTreeView(this))
{
    m_model->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    m_model->setRootPath(QString());

    m_treeView->setModel(m_model);
    m_treeView->setRootIndex(QModelIndex());
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(false);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setExpandsOnDoubleClick(true);

    for (int column = 1; column < m_model->columnCount(); ++column) {
        m_treeView->hideColumn(column);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_treeView);

    connect(m_treeView, &QTreeView::clicked, this, &FolderTreePanel::handleClicked);
}

void FolderTreePanel::handleClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString path = m_model->filePath(index);
    if (!path.isEmpty()) {
        emit folderSelected(path);
    }
}
