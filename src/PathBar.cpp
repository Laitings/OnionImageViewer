#include "PathBar.h"

#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QStackedLayout>
#include <QToolButton>

PathBar::PathBar(QWidget *parent)
    : QWidget(parent)
{
    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_crumbArea = new QWidget(this);
    m_crumbArea->setCursor(Qt::IBeamCursor);
    m_crumbArea->setToolTip(QStringLiteral("Click a folder to go there, or click here to type a path (Alt+D)"));
    m_crumbArea->installEventFilter(this);
    m_crumbLayout = new QHBoxLayout(m_crumbArea);
    m_crumbLayout->setContentsMargins(6, 2, 6, 2);
    m_crumbLayout->setSpacing(0);

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("Type or paste a folder or image path"));
    m_edit->installEventFilter(this);
    connect(m_edit, &QLineEdit::returnPressed, this, [this]() {
        finishEditing(true);
    });

    m_stack->addWidget(m_crumbArea);
    m_stack->addWidget(m_edit);
    setFixedHeight(m_edit->sizeHint().height() + 4);
    rebuildCrumbs();
}

void PathBar::setPath(const QString &path)
{
    if (path == m_path) {
        return;
    }
    m_path = path;
    rebuildCrumbs();
}

QList<PathBar::Segment> PathBar::splitPath(const QString &path)
{
    QList<Segment> segments;
    const QString slashed = QDir::fromNativeSeparators(path);
    const bool unc = slashed.startsWith(QStringLiteral("//"));
    QString clean = QDir::cleanPath(slashed);
    if (unc && !clean.startsWith(QStringLiteral("//"))) {
        clean.prepend(QLatin1Char('/')); // cleanPath keeps the UNC prefix only on Windows
    }
    if (clean.isEmpty() || clean == QStringLiteral(".")) {
        return segments;
    }

    QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString current;
    if (unc) {
        // UNC: \\server\share is the root.
        const QString server = parts.value(0);
        const QString share = parts.value(1);
        current = QStringLiteral("//") + server + (share.isEmpty() ? QString() : QStringLiteral("/") + share);
        segments.append({QDir::toNativeSeparators(current), current});
        parts = parts.mid(share.isEmpty() ? 1 : 2);
    } else if (clean.size() >= 2 && clean.at(1) == QLatin1Char(':')) {
        current = clean.left(2) + QStringLiteral("/");
        segments.append({clean.left(2), current});
        parts = parts.mid(1);
    } else if (clean.startsWith(QLatin1Char('/'))) {
        current = QStringLiteral("/");
        segments.append({QStringLiteral("/"), current});
    }

    for (const QString &part : parts) {
        current = current.isEmpty() || current.endsWith(QLatin1Char('/')) ? current + part : current + QStringLiteral("/") + part;
        segments.append({part, current});
    }
    return segments;
}

void PathBar::rebuildCrumbs()
{
    while (QLayoutItem *item = m_crumbLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_crumbButtons.clear();

    m_ellipsisButton = new QToolButton(m_crumbArea);
    m_ellipsisButton->setText(QStringLiteral("…"));
    m_ellipsisButton->setAutoRaise(true);
    m_ellipsisButton->setCursor(Qt::ArrowCursor);
    m_ellipsisButton->setToolTip(QStringLiteral("Show the full path"));
    connect(m_ellipsisButton, &QToolButton::clicked, this, &PathBar::startEditing);
    m_crumbLayout->addWidget(m_ellipsisButton);

    const QList<Segment> segments = splitPath(m_path);
    for (int i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            auto *separator = new QLabel(QStringLiteral("›"), m_crumbArea);
            separator->setContentsMargins(2, 0, 2, 0);
            separator->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
            m_crumbLayout->addWidget(separator);
        }
        auto *button = new QToolButton(m_crumbArea);
        button->setText(segments[i].label);
        button->setAutoRaise(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(QDir::toNativeSeparators(segments[i].path));
        const QString target = segments[i].path;
        connect(button, &QToolButton::clicked, this, [this, target]() {
            emit pathActivated(target);
        });
        if (i == segments.size() - 1) {
            QFont font = button->font();
            font.setBold(true);
            button->setFont(font);
        }
        m_crumbLayout->addWidget(button);
        m_crumbButtons.append(button);
    }
    if (segments.isEmpty()) {
        auto *hint = new QLabel(QStringLiteral("No folder selected"), m_crumbArea);
        hint->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
        m_crumbLayout->addWidget(hint);
    }
    m_crumbLayout->addStretch(1);
    fitCrumbs();
}

// Hides leading segments (behind "…") until the rest fits.
void PathBar::fitCrumbs()
{
    if (!m_ellipsisButton) {
        return;
    }
    const int available = width() - 16;
    int needed = 0;
    QList<int> widths;
    for (QToolButton *button : std::as_const(m_crumbButtons)) {
        widths.append(button->sizeHint().width() + 14); // + separator
        needed += widths.last();
    }
    int hidden = 0;
    const int ellipsisWidth = m_ellipsisButton->sizeHint().width();
    while (needed > available && hidden < m_crumbButtons.size() - 1) {
        needed -= widths.at(hidden);
        ++hidden;
        if (hidden == 1) {
            needed += ellipsisWidth;
        }
    }
    m_ellipsisButton->setVisible(hidden > 0);
    // Each button but the first is preceded by its separator label in the layout.
    for (int i = 0; i < m_crumbButtons.size(); ++i) {
        const bool visible = i >= hidden;
        m_crumbButtons[i]->setVisible(visible);
        const int index = m_crumbLayout->indexOf(m_crumbButtons[i]);
        if (i > 0 && index > 0) {
            if (QWidget *separator = m_crumbLayout->itemAt(index - 1)->widget()) {
                separator->setVisible(visible && i > hidden);
            }
        }
    }
}

void PathBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    fitCrumbs();
}

void PathBar::startEditing()
{
    m_edit->setText(QDir::toNativeSeparators(m_path));
    m_stack->setCurrentWidget(m_edit);
    m_edit->setFocus();
    m_edit->selectAll();
}

void PathBar::finishEditing(bool accept)
{
    const QString text = m_edit->text().trimmed().remove(QLatin1Char('"'));
    m_stack->setCurrentWidget(m_crumbArea);
    if (accept && !text.isEmpty()) {
        emit pathActivated(QDir::fromNativeSeparators(text));
    }
}

bool PathBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_crumbArea && event->type() == QEvent::MouseButtonPress) {
        startEditing();
        return true;
    }
    if (watched == m_edit) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            finishEditing(false);
            return true;
        }
        if (event->type() == QEvent::FocusOut && m_stack->currentWidget() == m_edit) {
            finishEditing(false);
        }
    }
    return QWidget::eventFilter(watched, event);
}
