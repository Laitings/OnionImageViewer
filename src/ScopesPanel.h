#pragma once

#include "ColorTransform.h"
#include "Scopes.h"

#include <QImage>
#include <QPoint>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class ScopeView;

// Side panel with a histogram and a waveform (luma, RGB parade or RGB overlay) of the image in
// the viewer. By default the scopes read the picture as displayed, with the color view or LUT
// applied, so they match the "display" values of the pixel readout; "Stored values" reads the
// file's own values instead (float images that are scene linear get a scale in stops).
// The scopes are computed on a background thread from a downsampled image, and only while the
// panel is visible. The hovered pixel is marked on both scopes.
class ScopesPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit ScopesPanel(QWidget *parent = nullptr);
    ~ScopesPanel() override;

    // The image in the viewer: source as read (stored values) and display as rendered with the
    // current view, which viewName describes. input is the EXR input setting (resolved per image).
    void setImages(const QImage &source, const QImage &display, ColorTransform::Input input,
                   const QString &viewName);
    // No image (back in the file browser).
    void clear();
    // The image pixel under the mouse in the viewer, or (-1, -1).
    void setHoveredPixel(const QPoint &pixel);

protected:
    void showEvent(QShowEvent *event) override;

private:
    enum class Source { Display, Stored };

    void schedule();
    void startJob();
    void applyResult(const Scopes::Result &result);
    void updateHover();
    void saveSettings() const;

    QComboBox *m_sourceBox = nullptr;
    QComboBox *m_histogramBox = nullptr;
    QComboBox *m_waveformBox = nullptr;
    ScopeView *m_histogramView = nullptr;
    ScopeView *m_waveformView = nullptr;
    QLabel *m_infoLabel = nullptr;

    QImage m_source;
    QImage m_display;
    ColorTransform::Input m_input = ColorTransform::Input::Auto;
    QString m_viewName;
    QPoint m_hoveredPixel = QPoint(-1, -1);

    Scopes::Result m_result;
    QThreadPool m_pool;
    QTimer m_timer;
    bool m_dirty = false;   // the image or source changed since the last job started
    bool m_running = false; // a job is computing
};
