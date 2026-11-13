#include "FolderTreePanel.h"

#include "Icons.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QtEnvironmentVariables>

#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const int FolderPathRole = Qt::UserRole;
const int NameColumn = 0;
const int PinColumn = 1; // the small pin glyph right of each pinned folder

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

// The same folder in Explorer's eyes: separators and a trailing slash don't matter, and on
// Windows neither does letter case.
QString pathKey(const QString &path)
{
    QString key = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (key.size() > 3 && key.endsWith(QLatin1Char('/'))) {
        key.chop(1);
    }
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    return key;
}

bool isDriveRoot(const QString &path)
{
    static const QRegularExpression driveRoot(QStringLiteral("^[A-Za-z]:[/\\\\]?$"));
    return driveRoot.match(path).hasMatch();
}

QString pinLabel(const QString &path)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (isDriveRoot(clean)) {
        return QDir::toNativeSeparators(clean.left(2) + QLatin1Char('/'));
    }
    const QString name = clean.mid(clean.lastIndexOf(QLatin1Char('/')) + 1);
    return name.isEmpty() ? QDir::toNativeSeparators(clean) : name;
}

// Icons for pins: the shell's own icon (OneDrive's cloud folder, Desktop, Downloads, ...) for
// folders on local disks, where asking the shell is quick; plain folder, drive and network icons
// for everything else, so a pin on a slow or disconnected network drive never stalls the panel.
QIcon pinIcon(const QString &path)
{
    QAbstractFileIconProvider *fast = fastFolderIconProvider();
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    if (native.size() >= 2 && native.at(1) == QLatin1Char(':')) {
        const QString root = native.left(2) + QLatin1Char('\\');
        const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
        if (type == DRIVE_FIXED) {
            static QFileIconProvider shellIcons;
            QIcon shellIcon = shellIcons.icon(QFileInfo(native));
            QIcon result;
            for (int size : {16, 20, 24, 32}) {
                const QPixmap pixmap = shellIcon.pixmap(size);
                if (!pixmap.isNull()) {
                    result.addPixmap(pixmap);
                }
            }
            if (!result.isNull()) {
                return result;
            }
        }
        if (isDriveRoot(native)) {
            return fast->icon(type == DRIVE_REMOTE ? QAbstractFileIconProvider::Network : QAbstractFileIconProvider::Drive);
        }
    } else if (native.startsWith(QStringLiteral("\\\\")) && native.count(QLatin1Char('\\')) <= 3) {
        return fast->icon(QAbstractFileIconProvider::Network); // \\server\share
    }
#endif
    return fast->icon(isDriveRoot(path) ? QAbstractFileIconProvider::Drive : QAbstractFileIconProvider::Folder);
}

// The pin glyph shown right of each pinned folder, toned down so it doesn't compete with the
// folder names.
QIcon dimmedPinIcon()
{
    static const QIcon icon = [] {
        const QIcon pin = Icons::icon(Icons::Kind::Pin);
        QIcon result;
        for (int size : {14, 16, 28, 32}) {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setOpacity(0.45);
            pin.paint(&painter, QRect(0, 0, size, size));
            painter.end();
            result.addPixmap(pixmap);
        }
        return result;
    }();
    return icon;
}

// The Quick access and This PC sections. Pins can be dragged to a new place among the pins, and
// nowhere else.
class SectionTree final : public QTreeWidget
{
public:
    explicit SectionTree(QWidget *parent)
        : QTreeWidget(parent)
    {
    }

    QTreeWidgetItem *quickAccessItem = nullptr;
    std::function<void()> onPinsMoved;

protected:
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        QTreeWidget::dragMoveEvent(event);
        if (!isValidDrop(event)) {
            event->ignore();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!isValidDrop(event)) {
            event->ignore();
            return;
        }
        QTreeWidget::dropEvent(event);
        if (onPinsMoved) {
            onPinsMoved();
        }
    }

private:
    bool isValidDrop(QDropEvent *event) const
    {
        if (event->source() != this) {
            return false;
        }
        const QTreeWidgetItem *target = itemAt(event->position().toPoint());
        if (!target) {
            return false;
        }
        switch (dropIndicatorPosition()) {
        case AboveItem:
        case BelowItem:
            return target->parent() == quickAccessItem;
        case OnItem:
            return target == quickAccessItem;
        case OnViewport:
            return false;
        }
        return false;
    }
};
}

FolderTreePanel::FolderTreePanel(QWidget *parent)
    : QWidget(parent)
    , m_model(new QFileSystemModel(this))
    , m_sections(new SectionTree(this))
    , m_treeContainer(new QWidget(this))
    , m_treeView(new QTreeView(m_treeContainer))
{
    // One continuous pane, like Explorer's navigation pane, rather than boxes on the window colour.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);

    m_model->setIconProvider(fastFolderIconProvider());
    m_model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
    m_model->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    m_model->setRootPath(QString());

    auto *sections = static_cast<SectionTree *>(m_sections);
    m_sections->setColumnCount(2);
    m_sections->setHeaderHidden(true);
    m_sections->header()->setStretchLastSection(false);
    m_sections->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_sections->header()->setSectionResizeMode(PinColumn, QHeaderView::Fixed);
    m_sections->header()->resizeSection(PinColumn, 24);
    m_sections->setFrameShape(QFrame::NoFrame);
    m_sections->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sections->setUniformRowHeights(true);
    m_sections->setAnimated(false);
    m_sections->setExpandsOnDoubleClick(false);
    m_sections->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sections->setDragDropMode(QAbstractItemView::InternalMove);
    m_sections->setDefaultDropAction(Qt::MoveAction);
    m_sections->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sections->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_quickAccessItem = new QTreeWidgetItem(m_sections, {QStringLiteral("Quick access")});
    m_quickAccessItem->setIcon(NameColumn, Icons::icon(Icons::Kind::QuickAccess));
    m_quickAccessItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    m_quickAccessItem->setFirstColumnSpanned(true);
    m_thisPcItem = new QTreeWidgetItem(m_sections, {QStringLiteral("This PC")});
    m_thisPcItem->setIcon(NameColumn, Icons::icon(Icons::Kind::ThisPC));
    m_thisPcItem->setFlags(Qt::ItemIsEnabled);
    m_thisPcItem->setFirstColumnSpanned(true);
    // The drives are shown by the tree below, not as children, but the item still folds them.
    m_thisPcItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    m_quickAccessItem->setExpanded(true);
    m_thisPcItem->setExpanded(true);
    sections->quickAccessItem = m_quickAccessItem;
    sections->onPinsMoved = [this]() {
        updateSectionHeight();
        emitPinsChanged();
    };

    m_treeView->setModel(m_model);
    m_treeView->setRootIndex(QModelIndex());
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(false);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setExpandsOnDoubleClick(true);
    m_treeView->setFrameShape(QFrame::NoFrame);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setIconSize(m_sections->iconSize());

    for (int column = 1; column < m_model->columnCount(); ++column) {
        m_treeView->hideColumn(column);
    }

    // Indent the drive tree one level, so the drives line up as children of This PC.
    auto *treeLayout = new QHBoxLayout(m_treeContainer);
    treeLayout->setContentsMargins(m_sections->indentation(), 0, 0, 0);
    treeLayout->setSpacing(0);
    treeLayout->addWidget(m_treeView);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_sections);
    layout->addWidget(m_treeContainer, 1);
    layout->addStretch(); // takes the space while This PC is folded

    connect(m_treeView, &QTreeView::clicked, this, &FolderTreePanel::handleTreeClicked);
    connect(m_treeView, &QWidget::customContextMenuRequested, this, &FolderTreePanel::showTreeContextMenu);
    connect(m_sections, &QTreeWidget::itemClicked, this, &FolderTreePanel::handleSectionClicked);
    connect(m_sections, &QWidget::customContextMenuRequested, this, &FolderTreePanel::showSectionContextMenu);
    auto sectionFolded = [this](QTreeWidgetItem *item) {
        if (item == m_thisPcItem) {
            m_treeContainer->setVisible(m_thisPcItem->isExpanded());
        }
        updateSectionHeight();
    };
    connect(m_sections, &QTreeWidget::itemExpanded, this, sectionFolded);
    connect(m_sections, &QTreeWidget::itemCollapsed, this, sectionFolded);

    updateSectionHeight();
}

QStringList FolderTreePanel::defaultPinnedFolders()
{
    QStringList candidates = {
        qEnvironmentVariable("OneDriveCommercial"),
        qEnvironmentVariable("OneDriveConsumer"),
        qEnvironmentVariable("OneDrive"),
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
    };
    QStringList result;
    QStringList keys;
    for (const QString &path : std::as_const(candidates)) {
        if (path.isEmpty() || keys.contains(pathKey(path)) || !QDir(path).exists()) {
            continue;
        }
        keys.append(pathKey(path));
        result.append(QDir::toNativeSeparators(QDir::cleanPath(path)));
    }
    return result;
}

QStringList FolderTreePanel::pinnedFolders() const
{
    QStringList result;
    for (int row = 0; row < m_quickAccessItem->childCount(); ++row) {
        result.append(m_quickAccessItem->child(row)->data(NameColumn, FolderPathRole).toString());
    }
    return result;
}

void FolderTreePanel::setPinnedFolders(const QStringList &folderPaths)
{
    qDeleteAll(m_quickAccessItem->takeChildren());
    for (const QString &path : folderPaths) {
        addPinItem(path);
    }
    updateSectionHeight();
    if (!m_currentFolder.isEmpty()) {
        highlightFolder(m_currentFolder, true);
    }
}

bool FolderTreePanel::isPinned(const QString &folderPath) const
{
    return pinItem(folderPath) != nullptr;
}

void FolderTreePanel::pinFolder(const QString &folderPath)
{
    if (folderPath.isEmpty() || isPinned(folderPath)) {
        return;
    }
    addPinItem(folderPath);
    m_quickAccessItem->setExpanded(true);
    updateSectionHeight();
    emitPinsChanged();
}

void FolderTreePanel::unpinFolder(const QString &folderPath)
{
    QTreeWidgetItem *item = pinItem(folderPath);
    if (!item) {
        return;
    }
    const bool wasHighlighted = item->isSelected();
    delete item;
    updateSectionHeight();
    if (wasHighlighted) {
        highlightFolder(m_currentFolder, false);
    }
    emitPinsChanged();
}

bool FolderTreePanel::isQuickAccessExpanded() const
{
    return m_quickAccessItem->isExpanded();
}

void FolderTreePanel::setQuickAccessExpanded(bool expanded)
{
    m_quickAccessItem->setExpanded(expanded);
}

bool FolderTreePanel::isThisPcExpanded() const
{
    return m_thisPcItem->isExpanded();
}

void FolderTreePanel::setThisPcExpanded(bool expanded)
{
    m_thisPcItem->setExpanded(expanded);
    m_treeContainer->setVisible(expanded);
    updateSectionHeight();
}

void FolderTreePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateSectionHeight();
}

QTreeWidgetItem *FolderTreePanel::addPinItem(const QString &folderPath, int position)
{
    if (folderPath.isEmpty() || pinItem(folderPath)) {
        return nullptr;
    }
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(folderPath)));
    auto *item = new QTreeWidgetItem({pinLabel(native)});
    item->setIcon(NameColumn, pinIcon(native));
    item->setIcon(PinColumn, dimmedPinIcon());
    item->setToolTip(NameColumn, native);
    item->setToolTip(PinColumn, QStringLiteral("Pinned to Quick access"));
    item->setData(NameColumn, FolderPathRole, native);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    if (position < 0 || position > m_quickAccessItem->childCount()) {
        m_quickAccessItem->addChild(item);
    } else {
        m_quickAccessItem->insertChild(position, item);
    }
    return item;
}

QTreeWidgetItem *FolderTreePanel::pinItem(const QString &folderPath) const
{
    if (folderPath.isEmpty()) {
        return nullptr;
    }
    const QString key = pathKey(folderPath);
    for (int row = 0; row < m_quickAccessItem->childCount(); ++row) {
        QTreeWidgetItem *item = m_quickAccessItem->child(row);
        if (pathKey(item->data(NameColumn, FolderPathRole).toString()) == key) {
            return item;
        }
    }
    return nullptr;
}

void FolderTreePanel::movePin(QTreeWidgetItem *item, int offset)
{
    const int row = m_quickAccessItem->indexOfChild(item);
    const int newRow = row + offset;
    if (row < 0 || newRow < 0 || newRow >= m_quickAccessItem->childCount()) {
        return;
    }
    const bool selected = item->isSelected();
    m_quickAccessItem->takeChild(row);
    m_quickAccessItem->insertChild(newRow, item);
    if (selected) {
        m_sections->setCurrentItem(item);
    }
    emitPinsChanged();
}

void FolderTreePanel::emitPinsChanged()
{
    emit pinnedFoldersChanged(pinnedFolders());
}

// The sections list is as tall as its rows, so the drive tree starts right below it. Lots of
// pins scroll inside it rather than squeezing the drive tree to nothing.
void FolderTreePanel::updateSectionHeight()
{
    int rows = 2;
    if (m_quickAccessItem->isExpanded()) {
        rows += m_quickAccessItem->childCount();
    }
    const int rowHeight = qMax(m_sections->sizeHintForRow(0), 1);
    const int wanted = rows * rowHeight + 2 * m_sections->frameWidth();
    const int available = height() - 8;
    const int cap = m_thisPcItem->isExpanded() ? available / 2 : available;
    m_sections->setFixedHeight(qMax(qMin(wanted, cap), 2 * rowHeight));
}

void FolderTreePanel::handleTreeClicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString path = m_model->filePath(index);
    if (!path.isEmpty()) {
        selectFolder(path, false);
    }
}

void FolderTreePanel::handleSectionClicked(QTreeWidgetItem *item)
{
    if (!item) {
        return;
    }
    if (item == m_quickAccessItem || item == m_thisPcItem) {
        // Clicking a section heading folds it, as in Explorer.
        if (item == m_thisPcItem) {
            setThisPcExpanded(!item->isExpanded());
        } else {
            item->setExpanded(!item->isExpanded());
        }
        return;
    }
    selectFolder(item->data(NameColumn, FolderPathRole).toString(), true);
}

void FolderTreePanel::addFolderActions(QMenu *menu, const QString &folderPath)
{
    if (isPinned(folderPath)) {
        menu->addAction(Icons::icon(Icons::Kind::Unpin), QStringLiteral("Unpin from Quick access"), this, [this, folderPath]() {
            unpinFolder(folderPath);
        });
    } else {
        menu->addAction(Icons::icon(Icons::Kind::Pin), QStringLiteral("Pin to Quick access"), this, [this, folderPath]() {
            pinFolder(folderPath);
        });
    }
}

void FolderTreePanel::showSectionContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sections->itemAt(pos);
    if (item == m_thisPcItem) {
        return;
    }
    QMenu menu(this);
    if (item && item->parent() == m_quickAccessItem) {
        const QString path = item->data(NameColumn, FolderPathRole).toString();
        QAction *open = menu.addAction(QStringLiteral("Open"), this, [this, path]() {
            selectFolder(path, true);
        });
        menu.setDefaultAction(open);
        menu.addSeparator();
        addFolderActions(&menu, path);
        const int row = m_quickAccessItem->indexOfChild(item);
        QAction *up = menu.addAction(QStringLiteral("Move Up"), this, [this, item]() {
            movePin(item, -1);
        });
        up->setEnabled(row > 0);
        QAction *down = menu.addAction(QStringLiteral("Move Down"), this, [this, item]() {
            movePin(item, 1);
        });
        down->setEnabled(row < m_quickAccessItem->childCount() - 1);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Copy Path"), this, [path]() {
            QGuiApplication::clipboard()->setText(path);
        });
        menu.addAction(QStringLiteral("Open in File Explorer"), this, [path]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    } else {
        QAction *pinCurrent = menu.addAction(Icons::icon(Icons::Kind::Pin), QStringLiteral("Pin Current Folder to Quick access"), this, [this]() {
            pinFolder(m_currentFolder);
        });
        pinCurrent->setEnabled(!m_currentFolder.isEmpty() && !isPinned(m_currentFolder));
    }
    menu.exec(m_sections->viewport()->mapToGlobal(pos));
}

void FolderTreePanel::showTreeContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_treeView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString path = QDir::toNativeSeparators(m_model->filePath(index));
    if (path.isEmpty()) {
        return;
    }
    QMenu menu(this);
    QAction *open = menu.addAction(QStringLiteral("Open"), this, [this, path]() {
        selectFolder(path, false);
    });
    menu.setDefaultAction(open);
    menu.addSeparator();
    addFolderActions(&menu, path);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy Path"), this, [path]() {
        QGuiApplication::clipboard()->setText(path);
    });
    menu.addAction(QStringLiteral("Open in File Explorer"), this, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

void FolderTreePanel::showFolder(const QString &folderPath)
{
    if (folderPath.isEmpty() || pathKey(folderPath) == pathKey(m_currentFolder)) {
        return; // already shown, possibly picked here (keep whichever section it was picked in)
    }
    m_currentFolder = folderPath;
    highlightFolder(folderPath, true);
}

// Highlights the folder in exactly one place: its pin when there is one (and preferPin), else
// the drive tree.
void FolderTreePanel::highlightFolder(const QString &folderPath, bool preferPin)
{
    QTreeWidgetItem *pin = preferPin ? pinItem(folderPath) : nullptr;
    if (pin) {
        m_sections->setCurrentItem(pin);
        m_treeView->clearSelection();
        return;
    }
    m_sections->clearSelection();
    m_sections->setCurrentItem(nullptr); // or the old pin keeps its focus frame
    const QModelIndex index = m_model->index(folderPath);
    if (index.isValid()) {
        // setCurrentIndex doesn't emit clicked, so this doesn't reload the grid.
        m_treeView->setCurrentIndex(index);
        m_treeView->scrollTo(index); // also expands the parents
    }
}

void FolderTreePanel::selectFolder(const QString &folderPath, bool fromPin)
{
    if (folderPath.isEmpty()) {
        return;
    }
    m_currentFolder = folderPath;
    highlightFolder(folderPath, fromPin);
    emit folderSelected(folderPath);
}
