#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

class ImageViewport final : public QWidget
{
    Q_OBJECT

public:
    explicit ImageViewport(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    bool hasImage() const;

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

private:
    QRectF imageRect() const;
    void clampPan();
    void setZoomAroundPoint(double zoom, const QPointF &viewportPoint);
    void drawCheckerboard(QPainter &painter, const QRect &rect);

    QImage m_image;
    double m_zoom = 1.0;
    QPointF m_pan;
    bool m_fitMode = true;
    bool m_panning = false;
    QPoint m_lastMousePos;
};
