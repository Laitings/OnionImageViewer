#include "ImageViewport.h"

#include <QColor>
#include <QFontMetrics>
#include <QPen>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

#include <cmath>

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
    setMouseTracking(true); // for the pixel readout and the wipe divider cursor
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

void ImageViewport::setImage(const QImage &image, bool keepView)
{
    // Same picture shown differently (e.g. a new color view): keep zoom and position.
    const bool sameSize = !m_image.isNull() && image.size() == m_image.size();
    // Convert once to a format QPainter can blit directly, so painting never has to
    // convert pixel formats on the fly.
    if (image.isNull()) {
        m_original = QImage();
    } else if (image.hasAlphaChannel()) {
        m_original = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    } else {
        m_original = image.convertToFormat(QImage::Format_RGB32);
    }
    m_image = applyLook(m_original);
    invalidateScaledCache();

    if (keepView && sameSize) {
        update();
        return;
    }

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

void ImageViewport::setCompareImage(const QImage &image, const QString &leftLabel, const QString &rightLabel)
{
    m_compareOriginal = image.isNull() || image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    m_compareImage = applyLook(m_compareOriginal);
    m_leftLabel = leftLabel;
    m_rightLabel = rightLabel;
    update();
}

void ImageViewport::setChannel(Channel channel)
{
    if (m_channel != channel) {
        m_channel = channel;
        reapplyLook();
    }
}

void ImageViewport::setClippingShown(bool shown)
{
    if (m_showClipping != shown) {
        m_showClipping = shown;
        reapplyLook();
    }
}

void ImageViewport::reapplyLook()
{
    m_image = applyLook(m_original);
    m_compareImage = applyLook(m_compareOriginal);
    invalidateScaledCache();
    update();
}

QImage ImageViewport::applyLook(const QImage &image) const
{
    if (image.isNull() || (m_channel == Channel::Rgb && !m_showClipping)) {
        return image;
    }
    const bool hasAlpha = image.hasAlphaChannel();
    QImage result(image.size(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *in = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        QRgb *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = hasAlpha ? qUnpremultiply(in[x]) : in[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            if (m_showClipping) {
                if (r >= 255 || g >= 255 || b >= 255) {
                    out[x] = qRgb(255, 0, 0);
                    continue;
                }
                if (r <= 0 && g <= 0 && b <= 0) {
                    out[x] = qRgb(0, 64, 255);
                    continue;
                }
            }
            int grey = 0;
            switch (m_channel) {
            case Channel::Rgb:
                out[x] = qRgb(r, g, b);
                continue;
            case Channel::Red:
                grey = r;
                break;
            case Channel::Green:
                grey = g;
                break;
            case Channel::Blue:
                grey = b;
                break;
            case Channel::Alpha:
                grey = hasAlpha ? qAlpha(in[x]) : 255;
                break;
            case Channel::Luma:
                grey = (r * 54 + g * 183 + b * 19 + 128) >> 8; // Rec.709 weights
                break;
            }
            out[x] = qRgb(grey, grey, grey);
        }
    }
    return result;
}

void ImageViewport::setWipeEnabled(bool enabled)
{
    if (m_wipeEnabled == enabled) {
        return;
    }
    m_wipeEnabled = enabled;
    m_draggingWipe = false;
    update();
}

void ImageViewport::setOverlayText(const QString &text)
{
    if (m_overlayText != text) {
        m_overlayText = text;
        update();
    }
}

QPoint ImageViewport::imagePixelAt(const QPointF &viewportPoint) const
{
    if (m_image.isNull()) {
        return QPoint(-1, -1);
    }
    const QRectF target = imageRect();
    const QPointF local = (viewportPoint - target.topLeft()) / m_zoom;
    const int x = static_cast<int>(std::floor(local.x()));
    const int y = static_cast<int>(std::floor(local.y()));
    if (x < 0 || y < 0 || x >= m_image.width() || y >= m_image.height()) {
        return QPoint(-1, -1);
    }
    return QPoint(x, y);
}

double ImageViewport::wipeX() const
{
    const QRectF target = imageRect();
    return target.left() + target.width() * m_wipePosition;
}

bool ImageViewport::nearWipeDivider(const QPointF &point) const
{
    return m_wipeEnabled && !m_compareImage.isNull() && std::abs(point.x() - wipeX()) <= 6.0
        && imageRect().adjusted(0, -8, 0, 8).contains(QPointF(wipeX(), point.y()));
}

void ImageViewport::updateHoveredPixel(const QPointF &point)
{
    const QPoint pixel = imagePixelAt(point);
    if (pixel != m_hoveredPixel) {
        m_hoveredPixel = pixel;
        emit hoveredPixelChanged(pixel);
    }
}

void ImageViewport::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_hoveredPixel != QPoint(-1, -1)) {
        m_hoveredPixel = QPoint(-1, -1);
        emit hoveredPixelChanged(m_hoveredPixel);
    }
}

void ImageViewport::drawLabel(QPainter &painter, const QString &text, Qt::Alignment corner)
{
    if (text.isEmpty()) {
        return;
    }
    QFont font = painter.font();
    font.setBold(true);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const QSize size(metrics.horizontalAdvance(text) + 16, metrics.height() + 8);
    // The window puts its close button in the top-right corner, so keep clear of it there.
    const int rightInset = corner.testFlag(Qt::AlignTop) ? 56 : 10;
    const int x = corner.testFlag(Qt::AlignRight) ? width() - size.width() - rightInset : 10;
    const int y = corner.testFlag(Qt::AlignBottom) ? height() - size.height() - 10 : 10;
    const QRect box(QPoint(x, y), size);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(QColor(235, 235, 235));
    painter.drawText(box, Qt::AlignCenter, text);
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

    const bool wipe = m_wipeEnabled && !m_compareImage.isNull();
    if (!wipe) {
        drawMainImage(painter, target, visibleTarget);
    } else {
        const double divider = wipeX();
        const QRectF left = visibleTarget.intersected(QRectF(QPointF(target.left(), target.top()), QPointF(divider, target.bottom())));
        const QRectF right = visibleTarget.intersected(QRectF(QPointF(divider, target.top()), QPointF(target.right(), target.bottom())));
        if (!right.isEmpty()) {
            painter.save();
            painter.setClipRect(right);
            drawMainImage(painter, target, right);
            painter.restore();
        }
        if (!left.isEmpty()) {
            // The compare image is stretched over the main image's rectangle.
            const double sx = m_compareImage.width() / target.width();
            const double sy = m_compareImage.height() / target.height();
            const QRectF source((left.left() - target.left()) * sx, (left.top() - target.top()) * sy,
                                left.width() * sx, left.height() * sy);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 8.0 && !m_panning && !m_draggingWipe);
            painter.drawImage(left, m_compareImage, source);
        }
        painter.setPen(QPen(QColor(255, 255, 255, 220), 1.5));
        painter.drawLine(QPointF(divider, target.top()), QPointF(divider, target.bottom()));
        painter.setBrush(QColor(255, 255, 255, 220));
        painter.setPen(Qt::NoPen);
        const double handleY = qBound(target.top() + 12.0, height() * 0.5, target.bottom() - 12.0);
        painter.drawEllipse(QPointF(divider, handleY), 7.0, 7.0);
        drawLabel(painter, m_leftLabel, Qt::AlignTop | Qt::AlignLeft);
        drawLabel(painter, m_rightLabel, Qt::AlignTop | Qt::AlignRight);
    }

    if (!m_overlayText.isEmpty()) {
        drawLabel(painter, m_overlayText, wipe ? (Qt::AlignBottom | Qt::AlignLeft) : (Qt::AlignTop | Qt::AlignLeft));
    }

    // Say when the picture isn't shown as is, so a grey or false-colored image isn't mistaken
    // for the real thing.
    QStringList look;
    switch (m_channel) {
    case Channel::Rgb:
        break;
    case Channel::Red:
        look << QStringLiteral("Red");
        break;
    case Channel::Green:
        look << QStringLiteral("Green");
        break;
    case Channel::Blue:
        look << QStringLiteral("Blue");
        break;
    case Channel::Alpha:
        look << QStringLiteral("Alpha");
        break;
    case Channel::Luma:
        look << QStringLiteral("Luma");
        break;
    }
    if (m_showClipping) {
        look << QStringLiteral("Clipping");
    }
    if (!look.isEmpty()) {
        drawLabel(painter, look.join(QStringLiteral(" · ")), Qt::AlignBottom | Qt::AlignRight);
    }
}

void ImageViewport::drawMainImage(QPainter &painter, const QRectF &target, const QRectF &visibleTarget)
{
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
    if (event->button() == Qt::LeftButton && nearWipeDivider(event->position())) {
        m_draggingWipe = true;
        event->accept();
        return;
    }
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
    if (m_draggingWipe) {
        const QRectF target = imageRect();
        if (target.width() > 0) {
            m_wipePosition = qBound(0.0, (event->position().x() - target.left()) / target.width(), 1.0);
            update();
        }
        event->accept();
        return;
    }
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        m_pan += QPointF(delta);
        clampPan();
        update();
        updateHoveredPixel(event->position());
        event->accept();
        return;
    }

    setCursor(nearWipeDivider(event->position()) ? Qt::SplitHCursor : Qt::ArrowCursor);
    updateHoveredPixel(event->position());
    QWidget::mouseMoveEvent(event);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_draggingWipe) {
        m_draggingWipe = false;
        update();
        event->accept();
        return;
    }
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
