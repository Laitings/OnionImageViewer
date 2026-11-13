#include "SplitPane.h"

#include "Icons.h"
#include "ImageLoader.h"
#include "ImageViewport.h"
#include "NameFilter.h"
#include "PathBar.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const int PathRole = Qt::UserRole;
const int IsFolderRole = Qt::UserRole + 1;
// Same size as the main grid's small thumbnails, so both share the thumbnail cache.
constexpr int ThumbnailResolution = 128;
constexpr int IconDisplaySize = 96;
constexpr int ThumbnailScheduleDelayMs = 60;
constexpr int PrefetchAhead = 3;
// The main viewer keeps 1.5 GB of frames; the second pane needs less.
constexpr qint64 FrameBudgetBytes = qint64(512) * 1024 * 1024;
}

SplitPane::SplitPane(std::shared_ptr<const ImageLoader> loader, ThumbnailFunction thumbnails, ImageFilter isImage,
                     QWidget *parent)
    : QWidget(parent)
    , m_thumbnails(std::move(thumbnails))
    , m_isImage(std::move(isImage))
{
    m_frameLoader = new FrameLoader(std::move(loader), this);
    if constexpr (sizeof(void *) == 8) {
        m_frameLoader->setMemoryBudget(FrameBudgetBytes);
    }
    connect(m_frameLoader, &FrameLoader::frameReady, this, &SplitPane::showFrame);

    m_folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    m_fileIcon = style()->standardIcon(QStyle::SP_FileIcon);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setAutoFillBackground(true);
    auto *headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(4, 2, 2, 2);
    headerLayout->setSpacing(2);
    m_pathBar = new PathBar(m_header);
    const QSize buttonSize(m_pathBar->minimumHeight() + 4, m_pathBar->minimumHeight());
    auto makeButton = [&](const QString &tip) {
        auto *button = new QToolButton(m_header);
        button->setAutoRaise(true);
        button->setIconSize(QSize(18, 18));
        button->setFixedHeight(buttonSize.height());
        button->setMinimumWidth(buttonSize.width());
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(tip);
        return button;
    };
    m_upButton = makeButton(QStringLiteral("Up one folder (Backspace)"));
    m_upButton->setIcon(Icons::icon(Icons::Kind::Up));
    m_browseButton = makeButton(QStringLiteral("Pick another image from this pane's own file grid (Esc)"));
    m_browseButton->setIcon(Icons::icon(Icons::Kind::ViewIcons));
    m_closeButton = makeButton(QStringLiteral("Close the split view (V)"));
    m_closeButton->setText(QStringLiteral("✕"));
    headerLayout->addWidget(m_upButton);
    headerLayout->addWidget(m_pathBar, 1);
    m_search = new QLineEdit(m_header);
    m_search->setPlaceholderText(QStringLiteral("Search"));
    m_search->setAccessibleName(QStringLiteral("Search the split pane's folder"));
    m_search->addAction(Icons::icon(Icons::Kind::Search), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(QStringLiteral("Filter by name. Wildcards: *.psd, IMG_00??.dng, *.psd;*.tif"));
    m_search->setMaximumWidth(200);
    m_search->installEventFilter(this);
    headerLayout->addWidget(m_search);
    headerLayout->addWidget(m_browseButton);
    headerLayout->addWidget(m_closeButton);
    layout->addWidget(m_header);

    m_stack = new QStackedWidget(this);
    m_grid = new QListWidget(m_stack);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setUniformItemSizes(true);
    m_grid->setIconSize(QSize(IconDisplaySize, IconDisplaySize));
    m_grid->setGridSize(QSize(IconDisplaySize + 40, IconDisplaySize + 40));
    m_grid->setSpacing(6);
    m_grid->setWordWrap(true);
    m_grid->setTextElideMode(Qt::ElideRight);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->verticalScrollBar()->setSingleStep(IconDisplaySize / 2);
    m_viewport = new ImageViewport(m_stack);
    m_stack->addWidget(m_grid);
    m_stack->addWidget(m_viewport);
    layout->addWidget(m_stack, 1);

    m_thumbnailPool.setMaxThreadCount(2);
    m_thumbnailPool.setThreadPriority(QThread::LowPriority);
    m_thumbnailTimer.setSingleShot(true);
    m_thumbnailTimer.setInterval(ThumbnailScheduleDelayMs);
    connect(&m_thumbnailTimer, &QTimer::timeout, this, &SplitPane::scheduleVisibleThumbnails);
    for (QScrollBar *bar : {m_grid->verticalScrollBar(), m_grid->horizontalScrollBar()}) {
        connect(bar, &QScrollBar::valueChanged, &m_thumbnailTimer, qOverload<>(&QTimer::start));
        connect(bar, &QScrollBar::rangeChanged, &m_thumbnailTimer, qOverload<>(&QTimer::start));
    }

    connect(m_grid, &QListWidget::itemActivated, this, &SplitPane::openItem);
    connect(m_search, &QLineEdit::textChanged, this, [this]() {
        applySearch();
        if (!isBrowsing()) {
            showBrowser(); // typing a search means looking for another file
        }
    });
    connect(m_grid, &QListWidget::currentItemChanged, this, &SplitPane::selectionChanged);
    connect(m_pathBar, &PathBar::pathActivated, this, [this](const QString &path) {
        const QFileInfo info(path);
        if (info.isDir()) {
            browse(info.absoluteFilePath());
        } else if (info.isFile() && m_isImage(info.absoluteFilePath())) {
            browse(info.absolutePath(), info.absoluteFilePath());
            openItem(m_items.value(info.absoluteFilePath()));
        }
    });
    connect(m_upButton, &QToolButton::clicked, this, &SplitPane::goUp);
    connect(m_browseButton, &QToolButton::clicked, this, [this]() {
        if (isBrowsing() && m_index >= 0) {
            m_stack->setCurrentWidget(m_viewport);
            updateHeader();
            emit contentChanged();
        } else {
            showBrowser();
        }
    });
    connect(m_closeButton, &QToolButton::clicked, this, &SplitPane::closeRequested);
    updateHeader();
}

SplitPane::~SplitPane()
{
    m_thumbnailPool.clear();
    m_thumbnailPool.waitForDone();
}

void SplitPane::browse(const QString &folderPath, const QString &selectPath)
{
    const QDir folder(folderPath);
    if (folderPath.isEmpty() || !folder.exists()) {
        return;
    }
    const QString absolute = folder.absolutePath();
    if (absolute != m_folderPath) {
        m_folderPath = absolute;
        // A search is for one folder, like in Explorer.
        {
            const QSignalBlocker blocker(m_search);
            m_search->clear();
        }
        m_grid->clear();
        m_items.clear();
        m_imagePaths.clear();
        m_index = -1;
        resetThumbnails();

        // Only names and attributes; file contents are never read for the listing.
        const QFileInfoList folders = folder.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : folders) {
            auto *item = new QListWidgetItem(m_folderIcon, info.fileName(), m_grid);
            item->setData(PathRole, info.absoluteFilePath());
            item->setData(IsFolderRole, true);
            item->setToolTip(QDir::toNativeSeparators(info.absoluteFilePath()));
        }
        const QFileInfoList files = folder.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : files) {
            const QString path = info.absoluteFilePath();
            if (!m_isImage(path)) {
                continue;
            }
            auto *item = new QListWidgetItem(m_fileIcon, info.fileName(), m_grid);
            item->setData(PathRole, path);
            item->setData(IsFolderRole, false);
            item->setToolTip(QDir::toNativeSeparators(path));
            m_items.insert(path, item);
            m_imagePaths.append(path);
        }
        // The shown image may still be in this folder (e.g. browsing back to it).
        m_index = m_imagePaths.indexOf(m_currentPath);
    }
    m_pathBar->setPath(m_folderPath);

    QListWidgetItem *selected = m_items.value(selectPath.isEmpty() ? m_currentPath : selectPath);
    if (!selected && m_grid->count() > 0) {
        selected = m_grid->item(0);
    }
    if (selected) {
        m_grid->setCurrentItem(selected);
        m_grid->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
    }
    m_stack->setCurrentWidget(m_grid);
    m_thumbnailTimer.start();
    updateHeader();
    emit contentChanged();
}

void SplitPane::showBrowser()
{
    browse(m_folderPath.isEmpty() && !m_currentPath.isEmpty() ? QFileInfo(m_currentPath).absolutePath() : m_folderPath);
}

QString SplitPane::selectedPath() const
{
    if (isBrowsing()) {
        if (const QListWidgetItem *item = m_grid->currentItem()) {
            return item->data(PathRole).toString();
        }
        return m_folderPath;
    }
    return m_currentPath;
}

void SplitPane::reclaimHeader()
{
    if (m_header->parentWidget() != this) {
        static_cast<QVBoxLayout *>(layout())->insertWidget(0, m_header);
    }
    setHeaderInline(false);
    m_header->show();
}

void SplitPane::setHeaderInline(bool inline_)
{
    // In the address bar row the bar has that row's height; on top of the pane it gets a little air.
    m_header->layout()->setContentsMargins(4, inline_ ? 0 : 2, 2, inline_ ? 0 : 2);
}

void SplitPane::applySearch()
{
    const NameFilter filter(m_search->text());
    const bool searching = !m_search->text().trimmed().isEmpty();
    QListWidgetItem *firstShown = nullptr;
    for (int i = 0; i < m_grid->count(); ++i) {
        QListWidgetItem *item = m_grid->item(i);
        const bool hidden = searching && !filter.matches(item->text());
        item->setHidden(hidden);
        if (!hidden && !firstShown) {
            firstShown = item;
        }
    }
    if (QListWidgetItem *current = m_grid->currentItem(); (!current || current->isHidden()) && firstShown) {
        m_grid->setCurrentItem(firstShown);
    }
    m_thumbnailTimer.start();
}

bool SplitPane::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape) {
            m_search->clear();
            m_grid->setFocus(Qt::ShortcutFocusReason);
            return true;
        }
        // Down or Enter from the search field moves on to the results.
        if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter) {
            m_grid->setFocus(Qt::ShortcutFocusReason);
            if (key != Qt::Key_Down) {
                openItem(m_grid->currentItem());
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool SplitPane::isBrowsing() const
{
    return m_stack->currentWidget() == m_grid;
}

void SplitPane::setRender(ColorTransform::View view, ColorTransform::Input input)
{
    if (view == m_view && input == m_input) {
        return;
    }
    m_view = view;
    m_input = input;
    m_frameLoader->setRender(view, input);
    if (!m_source.isNull()) {
        m_display = ColorTransform::toDisplay(m_source, view, input);
        m_viewport->setImage(m_display, true);
    }
    resetThumbnails();
    m_thumbnailTimer.start();
}

QString SplitPane::infoText() const
{
    if (isBrowsing() || m_source.isNull()) {
        return QStringLiteral("Split pane: %1 images in %2")
            .arg(m_imagePaths.size())
            .arg(QDir::toNativeSeparators(m_folderPath));
    }
    QStringList parts;
    parts.append(QStringLiteral("Split pane: %1").arg(QFileInfo(m_currentPath).fileName()));
    parts.append(QStringLiteral("%1 × %2").arg(m_source.width()).arg(m_source.height()));
    const QString source = ColorTransform::describeSource(m_source, m_input);
    if (!source.isEmpty()) {
        parts.append(source);
    }
    if (m_index >= 0) {
        parts.append(QStringLiteral("%1 / %2").arg(m_index + 1).arg(m_imagePaths.size()));
    }
    return parts.join(QStringLiteral("   ·   "));
}

void SplitPane::setActive(bool active)
{
    m_active = active;
    m_viewport->setHighlighted(active);
    QPalette headerPalette = palette();
    if (active) {
        QColor accent = palette().color(QPalette::Highlight);
        accent.setAlpha(110);
        headerPalette.setColor(QPalette::Window, accent);
    }
    m_header->setPalette(headerPalette);
}

void SplitPane::focusContent()
{
    if (isBrowsing()) {
        m_grid->setFocus(Qt::OtherFocusReason);
    } else {
        m_viewport->setFocus(Qt::OtherFocusReason);
    }
}

bool SplitPane::handleKey(QKeyEvent *event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
    if (isBrowsing()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            openItem(m_grid->currentItem());
            return true;
        case Qt::Key_Backspace:
            goUp();
            return true;
        case Qt::Key_Escape:
            if (m_index >= 0 && !m_source.isNull()) {
                m_stack->setCurrentWidget(m_viewport);
                updateHeader();
                emit contentChanged();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        if (m_index < 0) {
            return true;
        }
        const int step = (shift ? 10 : 1) * (event->key() == Qt::Key_Right ? 1 : -1);
        showImage(qBound(0, m_index + step, int(m_imagePaths.size()) - 1), false);
        return true;
    }
    case Qt::Key_Home:
        showImage(0, false);
        return true;
    case Qt::Key_End:
        showImage(int(m_imagePaths.size()) - 1, false);
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        showBrowser();
        focusContent();
        return true;
    default:
        return false;
    }
}

void SplitPane::openItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const QString path = item->data(PathRole).toString();
    if (item->data(IsFolderRole).toBool()) {
        browse(path);
        return;
    }
    const int index = m_imagePaths.indexOf(path);
    if (index >= 0) {
        showImage(index, true);
    }
}

void SplitPane::goUp()
{
    QDir folder(m_folderPath);
    const QString child = m_folderPath;
    if (!m_folderPath.isEmpty() && folder.cdUp()) {
        browse(folder.absolutePath());
        // Select the folder we came from, like Explorer does.
        for (int i = 0; i < m_grid->count(); ++i) {
            if (m_grid->item(i)->data(PathRole).toString() == child) {
                m_grid->setCurrentRow(i);
                m_grid->scrollToItem(m_grid->item(i), QAbstractItemView::PositionAtCenter);
                break;
            }
        }
    }
}

void SplitPane::showImage(int index, bool fresh)
{
    if (index < 0 || index >= m_imagePaths.size()) {
        return;
    }
    if (index == m_index && !fresh && !isBrowsing()) {
        return;
    }
    m_index = index;
    m_freshOpen = fresh || m_source.isNull();
    const QString path = m_imagePaths.at(index);
    if (!m_frameLoader->isCached(path)) {
        m_viewport->setOverlayText(QStringLiteral("%1  …").arg(QFileInfo(path).fileName()));
    }
    m_stack->setCurrentWidget(m_viewport);
    updateHeader();
    m_frameLoader->request(path);

    QStringList ahead;
    for (int i = 1; i <= PrefetchAhead; ++i) {
        for (int candidate : {index + i, index - i}) {
            if (candidate >= 0 && candidate < m_imagePaths.size()) {
                ahead.append(m_imagePaths.at(candidate));
            }
        }
    }
    m_frameLoader->prefetch(ahead);
    if (QListWidgetItem *item = m_items.value(path)) {
        m_grid->setCurrentItem(item);
    }
    emit contentChanged();
}

void SplitPane::showFrame(const FrameLoader::Frame &frame, bool requested)
{
    if (!requested || m_index < 0 || m_index >= m_imagePaths.size() || frame.path != m_imagePaths.at(m_index)) {
        return;
    }
    if (!frame.error.isEmpty()) {
        m_viewport->setOverlayText(QStringLiteral("%1: %2").arg(QFileInfo(frame.path).fileName(), frame.error));
        return;
    }
    m_currentPath = frame.path;
    m_source = frame.source;
    m_display = frame.display;
    const bool fresh = m_freshOpen;
    m_freshOpen = false;
    m_viewport->setImage(frame.display, !fresh);
    m_viewport->setOverlayText(QFileInfo(frame.path).fileName());
    emit contentChanged();
    if (fresh) {
        emit imageOpened();
    }
}

void SplitPane::scheduleVisibleThumbnails()
{
    if (!m_grid->isVisible() || m_grid->count() == 0) {
        return;
    }
    const QRect area = m_grid->viewport()->rect();
    for (int i = 0; i < m_grid->count(); ++i) {
        QListWidgetItem *item = m_grid->item(i);
        if (item->data(IsFolderRole).toBool() || m_requested.contains(item->data(PathRole).toString())) {
            continue;
        }
        if (m_grid->visualItemRect(item).intersects(area)) {
            requestThumbnail(item);
        }
    }
}

void SplitPane::requestThumbnail(QListWidgetItem *item)
{
    const QString path = item->data(PathRole).toString();
    m_requested.insert(path);
    const int generation = m_thumbnailGeneration;
    const ThumbnailFunction thumbnails = m_thumbnails;
    const ColorTransform::View view = m_view;
    const ColorTransform::Input input = m_input;
    m_thumbnailPool.start([this, thumbnails, path, generation, view, input]() {
        bool pending = false;
        const QImage thumbnail = thumbnails(path, view, input, QSize(ThumbnailResolution, ThumbnailResolution), &pending);
        // Queued to the pane; dropped if the pane is gone by then.
        QMetaObject::invokeMethod(this, [this, path, generation, thumbnail]() {
            applyThumbnail(path, generation, thumbnail);
        }, Qt::QueuedConnection);
    });
}

void SplitPane::applyThumbnail(const QString &filePath, int generation, const QImage &thumbnail)
{
    if (generation != m_thumbnailGeneration || thumbnail.isNull()) {
        return;
    }
    if (QListWidgetItem *item = m_items.value(filePath)) {
        item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    }
}

void SplitPane::resetThumbnails()
{
    m_thumbnailPool.clear();
    m_requested.clear();
    ++m_thumbnailGeneration;
    for (QListWidgetItem *item : std::as_const(m_items)) {
        item->setIcon(m_fileIcon);
    }
}

void SplitPane::updateHeader()
{
    const bool browsing = isBrowsing();
    m_upButton->setEnabled(!m_folderPath.isEmpty() && !QDir(m_folderPath).isRoot());
    m_browseButton->setEnabled(!browsing || (m_index >= 0 && !m_source.isNull()));
    m_browseButton->setToolTip(browsing ? QStringLiteral("Back to the image (Esc)")
                                        : QStringLiteral("Pick another image from this pane's own file grid (Esc)"));
}
