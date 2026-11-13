#include "Icons.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QtMath>

#include <cmath>

namespace Icons {
namespace {

class LineIconEngine final : public QIconEngine
{
public:
    explicit LineIconEngine(Kind kind)
        : m_kind(kind)
    {
    }

    QIconEngine *clone() const override { return new LineIconEngine(m_kind); }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QApplication::palette();
        QColor color = palette.color(QPalette::Active, QPalette::ButtonText);
        if (mode == QIcon::Disabled) {
            color.setAlphaF(0.35f);
        } else if (mode == QIcon::Selected) {
            color = palette.color(QPalette::Active, QPalette::HighlightedText);
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // Draw on a 24 x 24 grid, centred in the target square.
        const qreal side = qMin(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        QPen pen(color, side <= 18 ? 1.9 : 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        draw(painter, color);
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

private:
    void draw(QPainter *p, const QColor &color) const
    {
        QPainterPath path;
        switch (m_kind) {
        case Kind::Back:
            path.moveTo(19, 12);
            path.lineTo(5, 12);
            path.moveTo(11, 6);
            path.lineTo(5, 12);
            path.lineTo(11, 18);
            p->drawPath(path);
            break;
        case Kind::Up:
            path.moveTo(12, 19);
            path.lineTo(12, 5);
            path.moveTo(6, 11);
            path.lineTo(12, 5);
            path.lineTo(18, 11);
            p->drawPath(path);
            break;
        case Kind::FolderPanel: {
            p->drawRoundedRect(QRectF(3.5, 4.5, 17, 15), 2.5, 2.5);
            p->drawLine(QPointF(9.5, 4.5), QPointF(9.5, 19.5));
            QColor fill = color;
            fill.setAlphaF(color.alphaF() * 0.35);
            p->save();
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(QRectF(4.3, 5.3, 4.6, 13.4), 1.5, 1.5);
            p->restore();
            break;
        }
        case Kind::Open:
            // A folder.
            path.moveTo(3.5, 7);
            path.quadTo(3.5, 5, 5.5, 5);
            path.lineTo(9.5, 5);
            path.lineTo(11.5, 7.5);
            path.lineTo(18.5, 7.5);
            path.quadTo(20.5, 7.5, 20.5, 9.5);
            path.lineTo(20.5, 17);
            path.quadTo(20.5, 19, 18.5, 19);
            path.lineTo(5.5, 19);
            path.quadTo(3.5, 19, 3.5, 17);
            path.closeSubpath();
            path.moveTo(3.5, 10.5);
            path.lineTo(20.5, 10.5);
            p->drawPath(path);
            break;
        case Kind::Search:
            p->drawEllipse(QPointF(10, 10), 6, 6);
            p->drawLine(QPointF(14.25, 14.25), QPointF(20, 20));
            break;
        case Kind::FitToWindow:
            // Four corners pointing out.
            for (const QPointF &corner : {QPointF(4, 4), QPointF(20, 4), QPointF(20, 20), QPointF(4, 20)}) {
                const qreal dx = corner.x() < 12 ? 1 : -1;
                const qreal dy = corner.y() < 12 ? 1 : -1;
                path.moveTo(corner + QPointF(0, dy * 5));
                path.lineTo(corner);
                path.lineTo(corner + QPointF(dx * 5, 0));
            }
            p->drawPath(path);
            p->drawRect(QRectF(8.5, 9.5, 7, 5));
            break;
        case Kind::ActualSize: {
            // "1:1" in strokes.
            path.moveTo(5, 8);
            path.lineTo(7.5, 6);
            path.lineTo(7.5, 18);
            path.moveTo(15.5, 8);
            path.lineTo(18, 6);
            path.lineTo(18, 18);
            p->drawPath(path);
            p->save();
            p->setPen(Qt::NoPen);
            p->setBrush(color);
            p->drawEllipse(QPointF(12, 10), 1.15, 1.15);
            p->drawEllipse(QPointF(12, 15), 1.15, 1.15);
            p->restore();
            break;
        }
        case Kind::Metadata:
            p->drawEllipse(QPointF(12, 12), 8.5, 8.5);
            p->drawLine(QPointF(12, 11), QPointF(12, 16.5));
            p->save();
            p->setPen(Qt::NoPen);
            p->setBrush(color);
            p->drawEllipse(QPointF(12, 7.8), 1.15, 1.15);
            p->restore();
            break;
        case Kind::Color:
            // Three overlapping circles, like light mixing.
            p->drawEllipse(QPointF(12, 8.8), 5, 5);
            p->drawEllipse(QPointF(8.8, 14.4), 5, 5);
            p->drawEllipse(QPointF(15.2, 14.4), 5, 5);
            break;
        case Kind::ViewIcons:
            for (const QPointF &topLeft : {QPointF(4, 4), QPointF(13, 4), QPointF(4, 13), QPointF(13, 13)}) {
                p->drawRoundedRect(QRectF(topLeft, QSizeF(7, 7)), 1.2, 1.2);
            }
            break;
        case Kind::ViewList:
            for (qreal y : {6.0, 12.0, 18.0}) {
                p->drawRect(QRectF(4, y - 1.5, 3, 3));
                p->drawLine(QPointF(10, y), QPointF(20, y));
            }
            break;
        case Kind::ViewDetails:
            for (qreal y : {5.5, 10.0, 14.5, 19.0}) {
                p->drawLine(QPointF(4, y), QPointF(12, y));
                p->drawLine(QPointF(15, y), QPointF(20, y));
            }
            break;
        case Kind::ViewTiles:
            for (qreal y : {4.0, 13.0}) {
                p->drawRoundedRect(QRectF(4, y, 7, 7), 1.2, 1.2);
                p->drawLine(QPointF(14, y + 2), QPointF(20, y + 2));
                p->drawLine(QPointF(14, y + 5), QPointF(18, y + 5));
            }
            break;
        case Kind::QuickAccess: {
            QPainterPath star;
            for (int i = 0; i < 10; ++i) {
                const qreal radius = (i % 2 == 0) ? 8.6 : 3.7;
                const qreal angle = -M_PI / 2 + i * M_PI / 5;
                const QPointF point(12 + radius * std::cos(angle), 12.8 + radius * std::sin(angle));
                if (i == 0) {
                    star.moveTo(point);
                } else {
                    star.lineTo(point);
                }
            }
            star.closeSubpath();
            QColor fill = color;
            fill.setAlphaF(color.alphaF() * 0.35);
            p->setBrush(fill);
            p->drawPath(star);
            break;
        }
        case Kind::ThisPC:
            p->drawRoundedRect(QRectF(3.5, 4.5, 17, 11.5), 1.8, 1.8);
            p->drawLine(QPointF(12, 16), QPointF(12, 19.5));
            p->drawLine(QPointF(8.5, 19.5), QPointF(15.5, 19.5));
            break;
        case Kind::Pin:
        case Kind::Unpin:
            // A push pin, upright; Unpin crosses it out.
            path.moveTo(8.5, 4);
            path.lineTo(15.5, 4);
            path.moveTo(10, 4);
            path.lineTo(10, 9.5);
            path.lineTo(7, 13.5);
            path.lineTo(17, 13.5);
            path.lineTo(14, 9.5);
            path.lineTo(14, 4);
            path.moveTo(12, 13.5);
            path.lineTo(12, 20.5);
            if (m_kind == Kind::Unpin) {
                path.moveTo(4.5, 4.5);
                path.lineTo(19.5, 19.5);
            }
            p->drawPath(path);
            break;
        }
    }

    Kind m_kind;
};

} // namespace

QIcon icon(Kind kind)
{
    return QIcon(new LineIconEngine(kind));
}

} // namespace Icons
