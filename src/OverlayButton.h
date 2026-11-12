#pragma once

#include <QAbstractButton>

class QTimer;
class QVariantAnimation;

// A translucent round button drawn over the image (close, previous, next), with a vector icon so
// it looks sharp at any screen scale. It fades in while the mouse moves over the viewer and out
// again when the mouse rests (see reveal()).
class OverlayButton final : public QAbstractButton
{
    Q_OBJECT

public:
    enum class Icon { Close, Previous, Next };

    OverlayButton(Icon icon, QWidget *parent = nullptr);

    // Show the button now; it fades out again after a while unless the mouse is on it.
    void reveal();
    // Hide it right away (e.g. when playback starts).
    void conceal();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void fadeTo(qreal opacity, int durationMs);

    Icon m_icon;
    qreal m_opacity = 0.0;
    bool m_hovered = false;
    QVariantAnimation *m_fade = nullptr;
    QTimer *m_idleTimer = nullptr;
};
