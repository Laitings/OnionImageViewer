#include "ImageViewport.h"

#include <QColor>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

namespace {
constexpr double MinZoom = 0.02;
constexpr double MaxZoom = 64.0;
constexpr int CheckerSize = 16;
constexpr int SmoothRescaleDelayMs = 120;

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
    setAttribute(Qt::WA_OpaquePaintEvent);

    // One 2x2-cell tile drawn with a texture brush instead of thousands of fillRect calls.
    m_checkerTile = QPixmap(CheckerSize * 2, CheckerSize * 2);
    {
        QPainter tilePainter(&m_checkerTile);
        const QColor light(96, 96, 96);
        const QColor dark(72, 72, 72);
        tilePainter.fillRect(0, 0, CheckerSize * 2, CheckerSize * 2, dark);
        tilePainter.fillRect(0, 0, CheckerSize, CheckerSize, light);
        tilePainter.fillRect(CheckerSize, CheckerSize, CheckerSize, CheckerSize, light);
    }

    // While the user is resizing or zooming, paint with a fast transform and only build the
    // high quality downscaled copy once things have settled.
    m_smoothRescaleTimer.setSingleShot(true);
    m_smoothRescaleTimer.setInterval(SmoothRescaleDelayMs);
    connect(&m_smoothRescaleTimer, &QTimer::timeout, this, [this]() {
        rebuildScaledCache();
        update();
    });
}

void ImageViewport::setImage(const QImage &image)
{
    // Convert once to a format QPainter can blit directly, so painting never has to
    // convert pixel formats on the fly.
    if (image.isNull()) {
        m_image = QImage();
    } else if (image.hasAlphaChannel()) {
        m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
        m_image = image.convertToFormat(QImage::Format_RGB32);
    }
    invalidateScaledCache();

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
    const double newZoom = clampedZoom(qMin(widthScale, heightScale));
    if (!qFuzzyCompare(newZoom, m_zoom)) {
        m_zoom = newZoom;
        invalidateScaledCache();
    }
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
    invalidateScaledCache();
    m_pan = QPointF();
    clampPan();
    update();
}

void ImageViewport::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(31, 31, 31));

    if (m_image.isNull()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open an image to view it"));
        return;
    }

    const QRectF target = imageRect();
    const QRectF visibleTarget = target.intersected(QRectF(event->rect()));
    if (visibleTarget.isEmpty()) {
        return;
    }

    drawCheckerboard(painter, visibleTarget.toAlignedRect());

    // Downscaled view with an up-to-date cache: draw the pre-scaled pixmap 1:1, which is cheap.
    if (m_zoom < 1.0 && !m_scaledCache.isNull() && qFuzzyCompare(m_scaledCacheZoom, m_zoom)) {
        const QPointF topLeft = target.topLeft();
        const QRectF source = visibleTarget.translated(-topLeft);
        painter.drawPixmap(visibleTarget, m_scaledCache, source);
        return;
    }

    // Otherwise only transform the part of the source image that is actually visible.
    const QRectF source(
        (visibleTarget.left() - target.left()) / m_zoom,
        (visibleTarget.top() - target.top()) / m_zoom,
        visibleTarget.width() / m_zoom,
        visibleTarget.height() / m_zoom);

    const bool interactive = m_smoothRescaleTimer.isActive() || m_panning;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 8.0 && !interactive);
    painter.drawImage(visibleTarget, m_image, source);

    if (m_zoom < 1.0 && !m_smoothRescaleTimer.isActive()) {
        m_smoothRescaleTimer.start();
    }
}

void ImageViewport::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_image.isNull()) {
        m_smoothRescaleTimer.start();
    }
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
        update();
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
    invalidateScaledCache();
    m_smoothRescaleTimer.start();
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
    painter.setBrushOrigin(0, 0);
    painter.fillRect(rect, QBrush(m_checkerTile));
    painter.restore();
}

void ImageViewport::invalidateScaledCache()
{
    m_scaledCache = QPixmap();
    m_scaledCacheZoom = 0.0;
}

void ImageViewport::rebuildScaledCache()
{
    if (m_image.isNull() || m_zoom >= 1.0) {
        invalidateScaledCache();
        return;
    }

    const QSize scaledSize(
        qMax(1, qRound(static_cast<double>(m_image.width()) * m_zoom)),
        qMax(1, qRound(static_cast<double>(m_image.height()) * m_zoom)));

    m_scaledCache = QPixmap::fromImage(m_image.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    m_scaledCacheZoom = m_zoom;
}
