#pragma once

#include <QImage>
#include <QPoint>
#include <QPixmap>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class ImageViewport final : public QWidget
{
    Q_OBJECT

public:
    // What to show of the image: all of it, one channel as grey, or the luminance.
    enum class Channel { Rgb, Red, Green, Blue, Alpha, Luma };

    explicit ImageViewport(QWidget *parent = nullptr);

    // Display aids that only change how the picture is drawn (the pixel readout is unaffected).
    void setChannel(Channel channel);
    Channel channel() const { return m_channel; }
    // Marks pixels that are clipped on screen: red where a channel is at full white, blue where
    // all channels are at black.
    void setClippingShown(bool shown);
    bool isClippingShown() const { return m_showClipping; }

    void setImage(const QImage &image, bool keepView = false);
    bool hasImage() const;

    // A/B wipe: the compare image (any size; it is stretched over the main image) is shown left of
    // a draggable divider, the main image right of it. Labels are drawn in the top corners.
    void setCompareImage(const QImage &image, const QString &leftLabel, const QString &rightLabel);
    void setWipeEnabled(bool enabled);
    bool isWipeEnabled() const { return m_wipeEnabled; }

    // Text drawn in the top-left corner (e.g. the frame number).
    void setOverlayText(const QString &text);

    // Image pixel under a viewport position, or (-1, -1) outside the image.
    QPoint imagePixelAt(const QPointF &viewportPoint) const;

signals:
    // The image pixel under the mouse, or (-1, -1) when the mouse is outside the image.
    void hoveredPixelChanged(const QPoint &pixel);

public slots:
    void fitToWindow();
    void zoomActualSize();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF imageRect() const;
    void clampPan();
    void setZoomAroundPoint(double zoom, const QPointF &viewportPoint);
    void drawCheckerboard(QPainter &painter, const QRect &rect);
    void invalidateScaledCache();
    void rebuildScaledCache();
    void drawMainImage(QPainter &painter, const QRectF &target, const QRectF &visibleTarget);
    void drawLabel(QPainter &painter, const QString &text, Qt::Alignment corner);
    double wipeX() const;
    bool nearWipeDivider(const QPointF &point) const;
    void updateHoveredPixel(const QPointF &point);
    QImage applyLook(const QImage &image) const;
    void reapplyLook();

    QPixmap m_checkerTile;
    QPixmap m_scaledCache;
    double m_scaledCacheZoom = 0.0;
    QTimer m_smoothRescaleTimer;

    QImage m_original; // as given to setImage, converted for painting
    QImage m_image;    // m_original with the channel/clipping look applied
    Channel m_channel = Channel::Rgb;
    bool m_showClipping = false;
    double m_zoom = 1.0;
    QPointF m_pan;
    bool m_fitMode = true;
    bool m_panning = false;
    QPoint m_lastMousePos;

    QImage m_compareOriginal;
    QImage m_compareImage;
    QString m_leftLabel;
    QString m_rightLabel;
    bool m_wipeEnabled = false;
    bool m_draggingWipe = false;
    double m_wipePosition = 0.5; // fraction of the image width
    QString m_overlayText;
    QPoint m_hoveredPixel = QPoint(-1, -1);
};
