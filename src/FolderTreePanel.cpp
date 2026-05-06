#include "FolderTreePanel.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileSystemModel>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtEnvironmentVariables>

namespace {
const int FolderPathRole = Qt::UserRole;
}

FolderTreePanel::FolderTreePanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_quickLocations(new QListWidget(this))
    , m_treeView(new QTreeView(this))
{
    m_model->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    m_model->setRootPath(QString());

    m_quickLocations->setFrameShape(QFrame::NoFrame);
    m_quickLocations->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_quickLocations->setSelectionMode(QAbstractItemView::SingleSelection);
    m_quickLocations->setUniformItemSizes(true);
    m_quickLocations->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_quickLocations->setVisible(false);
    populateQuickLocations();

    auto *quickLocationDivider = new QFrame(this);
    quickLocationDivider->setFrameShape(QFrame::HLine);
    quickLocationDivider->setFrameShadow(QFrame::Plain);
    quickLocationDivider->setForegroundRole(QPalette::Mid);
    quickLocationDivider->setVisible(m_quickLocations->count() > 0);

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
    layout->setSpacing(0);
    layout->addWidget(m_quickLocations);
    layout->addSpacing(6);
    layout->addWidget(quickLocationDivider);
    layout->addSpacing(6);
    layout->addWidget(m_treeView, 1);

    connect(m_treeView, &QTreeView::clicked, this, &FolderTreePanel::handleClicked);
    connect(m_quickLocations, &QListWidget::itemClicked, this, &FolderTreePanel::handleQuickLocationClicked);
}

void FolderTreePanel::addQuickLocation(const QString &label, const QString &path)
{
    if (path.isEmpty() || !QDir(path).exists()) {
        return;
    }

    for (int row = 0; row < m_quickLocations->count(); ++row) {
        const QListWidgetItem *existingItem = m_quickLocations->item(row);
        if (QDir::cleanPath(existingItem->data(FolderPathRole).toString()) == QDir::cleanPath(path)) {
            return;
        }
    }

    auto *item = new QListWidgetItem(label, m_quickLocations);
    item->setToolTip(path);
    item->setData(FolderPathRole, path);
    m_quickLocations->setVisible(true);
    m_quickLocations->setFixedHeight(m_quickLocations->sizeHintForRow(0) * m_quickLocations->count()
        + 2 * m_quickLocations->frameWidth());
}

void FolderTreePanel::populateQuickLocations()
{
    addQuickLocation(QStringLiteral("Personal OneDrive"), qEnvironmentVariable("OneDriveConsumer"));
    addQuickLocation(QStringLiteral("Work OneDrive"), qEnvironmentVariable("OneDriveCommercial"));
    addQuickLocation(QStringLiteral("OneDrive"), qEnvironmentVariable("OneDrive"));
}

void FolderTreePanel::handleClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString path = m_model->filePath(index);
    if (!path.isEmpty()) {
        selectFolder(path);
    }
}

void FolderTreePanel::handleQuickLocationClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    selectFolder(item->data(FolderPathRole).toString());
}

void FolderTreePanel::selectFolder(const QString &folderPath)
{
    if (folderPath.isEmpty()) {
        return;
    }

    const QModelIndex index = m_model->index(folderPath);
    if (index.isValid()) {
        m_treeView->setCurrentIndex(index);
        m_treeView->scrollTo(index);
    }

    emit folderSelected(folderPath);
}
