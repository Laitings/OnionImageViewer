#include "OverlayButton.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVariantAnimation>

namespace {
constexpr int IdleHideMs = 1800;
}

OverlayButton::OverlayButton(Icon icon, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
    , m_fade(new QVariantAnimation(this))
    , m_idleTimer(new QTimer(this))
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setAttribute(Qt::WA_TransparentForMouseEvents, true); // until it is shown
    resize(sizeHint());

    m_fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_opacity = value.toReal();
        // An invisible button must not catch clicks meant for the image.
        setAttribute(Qt::WA_TransparentForMouseEvents, m_opacity < 0.05 || !isEnabled());
        update();
    });

    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(IdleHideMs);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        if (!m_hovered) {
            fadeTo(0.0, 450);
        }
    });
}

QSize OverlayButton::sizeHint() const
{
    // Close is a circle; the side buttons are tall pills, easier to hit.
    return m_icon == Icon::Close ? QSize(36, 36) : QSize(40, 72);
}

void OverlayButton::reveal()
{
    if (!isEnabled()) {
        return;
    }
    if (m_opacity < 1.0 && (m_fade->state() != QAbstractAnimation::Running || m_fade->endValue().toReal() < 1.0)) {
        fadeTo(1.0, 140);
    }
    m_idleTimer->start();
}

void OverlayButton::conceal()
{
    m_idleTimer->stop();
    fadeTo(0.0, 200);
}

void OverlayButton::fadeTo(qreal opacity, int durationMs)
{
    m_fade->stop();
    m_fade->setStartValue(m_opacity);
    m_fade->setEndValue(opacity);
    m_fade->setDuration(durationMs);
    m_fade->start();
}

void OverlayButton::enterEvent(QEnterEvent *event)
{
    QAbstractButton::enterEvent(event);
    m_hovered = true;
    reveal();
    update();
}

void OverlayButton::leaveEvent(QEvent *event)
{
    QAbstractButton::leaveEvent(event);
    m_hovered = false;
    m_idleTimer->start();
    update();
}

void OverlayButton::changeEvent(QEvent *event)
{
    QAbstractButton::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        // A disabled button (first or last image) is not drawn and must not block the image.
        setAttribute(Qt::WA_TransparentForMouseEvents, !isEnabled() || m_opacity < 0.05);
        update();
    }
}

void OverlayButton::paintEvent(QPaintEvent *)
{
    if (m_opacity <= 0.0 || !isEnabled()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    // Background: dark glass, a bit stronger on hover and press, with a faint edge so it reads on
    // dark images too.
    const int alpha = isDown() ? 215 : (m_hovered ? 185 : 120);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = qMin(box.width(), box.height()) / 2.0;
    painter.setPen(QPen(QColor(255, 255, 255, m_hovered ? 70 : 40), 1.0));
    painter.setBrush(QColor(12, 12, 12, alpha));
    painter.drawRoundedRect(box, radius, radius);

    // Icon: thin white strokes with round ends.
    QPen pen(QColor(255, 255, 255, m_hovered ? 255 : 225), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QPointF c = box.center();
    QPainterPath path;
    switch (m_icon) {
    case Icon::Close: {
        const qreal s = 5.5;
        path.moveTo(c + QPointF(-s, -s));
        path.lineTo(c + QPointF(s, s));
        path.moveTo(c + QPointF(s, -s));
        path.lineTo(c + QPointF(-s, s));
        break;
    }
    case Icon::Previous:
    case Icon::Next: {
        const qreal w = 5.0;
        const qreal h = 9.0;
        const qreal dir = m_icon == Icon::Next ? 1.0 : -1.0;
        path.moveTo(c + QPointF(-w * dir * 0.5, -h));
        path.lineTo(c + QPointF(w * dir * 0.5 + dir * 1.0, 0));
        path.lineTo(c + QPointF(-w * dir * 0.5, h));
        break;
    }
    }
    painter.drawPath(path);
}
