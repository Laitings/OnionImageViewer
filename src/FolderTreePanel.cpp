#include "FolderTreePanel.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtEnvironmentVariables>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const int FolderPathRole = Qt::UserRole;

// The default Windows icon provider asks the shell for every drive icon each time the tree
// repaints, and drive icons are never cached. With mapped network drives (or a disconnected
// one) that means network round trips on the UI thread on every resize. This provider hands out
// a few icons built once up front, so painting never touches the file system.
class FastFolderIconProvider final : public QFileIconProvider
{
public:
    FastFolderIconProvider()
        : m_folderIcon(toStaticIcon(QFileIconProvider::icon(QFileIconProvider::Folder)))
        , m_driveIcon(toStaticIcon(QFileIconProvider::icon(QFileIconProvider::Drive)))
        , m_networkIcon(toStaticIcon(QFileIconProvider::icon(QFileIconProvider::Network)))
    {
        setOptions(QFileIconProvider::DontUseCustomDirectoryIcons);
    }

    QIcon icon(IconType type) const override
    {
        switch (type) {
        case Drive:
            return m_driveIcon;
        case Network:
            return m_networkIcon;
        case Folder:
            return m_folderIcon;
        default:
            return QFileIconProvider::icon(type);
        }
    }

    QIcon icon(const QFileInfo &info) const override
    {
        if (info.isRoot()) {
            return isNetworkDrive(info) ? m_networkIcon : m_driveIcon;
        }
        return m_folderIcon;
    }

private:
    // Render the (possibly lazily loading) icon into plain pixmaps once, so later paints are free.
    static QIcon toStaticIcon(const QIcon &source)
    {
        QIcon result;
        for (int size : {16, 20, 24, 32, 48}) {
            const QPixmap pixmap = source.pixmap(size);
            if (!pixmap.isNull()) {
                result.addPixmap(pixmap);
            }
        }
        return result.isNull() ? source : result;
    }

    static bool isNetworkDrive(const QFileInfo &info)
    {
#ifdef Q_OS_WIN
        // GetDriveType only looks at the local drive table, so it never waits on the network.
        const QString root = QDir::toNativeSeparators(info.absoluteFilePath());
        return GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16())) == DRIVE_REMOTE;
#else
        Q_UNUSED(info)
        return false;
#endif
    }

    QIcon m_folderIcon;
    QIcon m_driveIcon;
    QIcon m_networkIcon;
};

QAbstractFileIconProvider *fastFolderIconProvider()
{
    // Lives for the whole program, because the model's background thread may still use it
    // while the window is being torn down.
    static FastFolderIconProvider provider;
    return &provider;
}
}

FolderTreePanel::FolderTreePanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_quickLocations(new QListWidget(this))
    , m_treeView(new QTreeView(this))
{
    m_model->setIconProvider(fastFolderIconProvider());
    m_model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
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
