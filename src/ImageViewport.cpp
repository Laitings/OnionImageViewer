#include "ImageViewport.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

namespace {
constexpr double MinZoom = 0.02;
constexpr double MaxZoom = 64.0;
constexpr int CheckerSize = 16;

double clampedZoom(double zoom)
{
    return qBound(MinZoom, zoom, MaxZoom);
}
}

ImageViewport::ImageViewport(QWidget *parent)
    : QWidget(parent)
{
    setBackgroundRole(QPalette::Base);
    setMouseTracking(false);
    setCursor(Qt::ArrowCursor);
}

void ImageViewport::setImage(const QImage &image)
{
    m_image = image;
    if (m_image.isNull()) {
        fitToWindow();
        return;
    }

    if (m_image.width() <= width() && m_image.height() <= height()) {
        zoomActualSize();
    } else {
        fitToWindow();
    }
}

bool ImageViewport::hasImage() const
{
    return !m_image.isNull();
}

void ImageViewport::fitToWindow()
{
    m_fitMode = true;
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        m_zoom = 1.0;
        m_pan = QPointF();
        update();
        return;
    }

    const double widthScale = static_cast<double>(width()) / static_cast<double>(m_image.width());
    const double heightScale = static_cast<double>(height()) / static_cast<double>(m_image.height());
    m_zoom = clampedZoom(qMin(widthScale, heightScale));
    m_pan = QPointF();
    update();
}

void ImageViewport::zoomActualSize()
{
    if (m_image.isNull()) {
        return;
    }

    m_fitMode = false;
    m_zoom = 1.0;
    m_pan = QPointF();
    clampPan();
    update();
}

void ImageViewport::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(31, 31, 31));

    if (m_image.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open an image to view it"));
        return;
    }

    const QRectF target = imageRect();
    drawCheckerboard(painter, target.toAlignedRect().intersected(rect()));

    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 8.0);
    painter.drawImage(target, m_image);
}

void ImageViewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_fitMode) {
        fitToWindow();
    } else {
        clampPan();
    }
}

void ImageViewport::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }

    const QPoint angleDelta = event->angleDelta();
    if (angleDelta.y() == 0) {
        event->ignore();
        return;
    }

    const double steps = static_cast<double>(angleDelta.y()) / 120.0;
    const double factor = qPow(1.2, steps);
    setZoomAroundPoint(m_zoom * factor, event->position());
    event->accept();
}

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_image.isNull()) {
        m_panning = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        m_pan += QPointF(delta);
        clampPan();
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

QRectF ImageViewport::imageRect() const
{
    if (m_image.isNull()) {
        return QRectF();
    }

    const QSizeF scaledSize(
        static_cast<double>(m_image.width()) * m_zoom,
        static_cast<double>(m_image.height()) * m_zoom);

    const QPointF centered(
        (static_cast<double>(width()) - scaledSize.width()) * 0.5,
        (static_cast<double>(height()) - scaledSize.height()) * 0.5);

    return QRectF(centered + m_pan, scaledSize);
}

void ImageViewport::clampPan()
{
    if (m_image.isNull()) {
        m_pan = QPointF();
        return;
    }

    const QSizeF scaledSize(
        static_cast<double>(m_image.width()) * m_zoom,
        static_cast<double>(m_image.height()) * m_zoom);

    const double maxX = qMax(0.0, (scaledSize.width() - static_cast<double>(width())) * 0.5);
    const double maxY = qMax(0.0, (scaledSize.height() - static_cast<double>(height())) * 0.5);
    m_pan.setX(qBound(-maxX, m_pan.x(), maxX));
    m_pan.setY(qBound(-maxY, m_pan.y(), maxY));
}

void ImageViewport::setZoomAroundPoint(double zoom, const QPointF &viewportPoint)
{
    const double oldZoom = m_zoom;
    const double newZoom = clampedZoom(zoom);
    if (qFuzzyCompare(oldZoom, newZoom)) {
        return;
    }

    const QPointF viewportCenter(width() * 0.5, height() * 0.5);
    const QPointF imageSpacePoint = (viewportPoint - viewportCenter - m_pan) / oldZoom;

    m_fitMode = false;
    m_zoom = newZoom;
    m_pan = viewportPoint - viewportCenter - imageSpacePoint * m_zoom;
    clampPan();
    update();
}

void ImageViewport::drawCheckerboard(QPainter &painter, const QRect &rect)
{
    if (rect.isEmpty()) {
        return;
    }

    painter.save();
    painter.setClipRect(rect);

    const QColor light(96, 96, 96);
    const QColor dark(72, 72, 72);

    for (int y = rect.top() - (rect.top() % CheckerSize); y <= rect.bottom(); y += CheckerSize) {
        for (int x = rect.left() - (rect.left() % CheckerSize); x <= rect.right(); x += CheckerSize) {
            const bool alternate = ((x / CheckerSize) + (y / CheckerSize)) % 2 == 0;
            painter.fillRect(QRect(x, y, CheckerSize, CheckerSize), alternate ? light : dark);
        }
    }

    painter.restore();
}
