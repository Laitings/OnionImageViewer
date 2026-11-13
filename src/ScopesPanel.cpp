#include "ScopesPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace {

// QSettings keys (the panel keeps its own choices).
const QString SettingsSource = QStringLiteral("scopes/source");
const QString SettingsHistogram = QStringLiteral("scopes/histogram");
const QString SettingsWaveform = QStringLiteral("scopes/waveform");

const QColor ChannelColors[4] = {QColor(255, 70, 70), QColor(70, 230, 70), QColor(90, 130, 255), QColor(220, 220, 220)};
const QColor Background(17, 17, 17);
const QColor GridColor(255, 255, 255, 38);
const QColor HighlightGridColor(255, 200, 80, 90);
const QColor LabelColor(160, 160, 160);

} // namespace

// One scope: the histogram (values left to right) or the waveform (image columns left to right,
// values bottom to top). It paints from the panel's last result.
class ScopeView final : public QWidget
{
public:
    enum class Kind { Histogram, Waveform };
    // Histogram modes: 0 RGB, 1 luma. Waveform modes: 0 luma, 1 RGB parade, 2 RGB overlay.

    ScopeView(Kind kind, QWidget *parent)
        : QWidget(parent)
        , m_kind(kind)
    {
        setMinimumHeight(90);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setResult(const Scopes::Result *result) { m_result = result; update(); }
    void setMode(int mode) { m_mode = mode; update(); }
    void setMessage(const QString &message) { m_message = message; update(); }
    // Values (R, G, B, luma) of the hovered pixel and where it is across the image (0..1).
    void setHover(bool shown, const std::array<float, 4> &values, float across)
    {
        m_hover = shown;
        m_hoverValues = values;
        m_hoverAcross = across;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Background);
        if (!m_result || !m_result->valid) {
            painter.setPen(LabelColor);
            painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
            return;
        }
        if (m_kind == Kind::Histogram) {
            paintHistogram(painter);
        } else {
            paintWaveform(painter);
        }
    }

private:
    // Which channels (0 R, 1 G, 2 B, 3 luma) a mode shows.
    QList<int> channels() const
    {
        const bool luma = m_kind == Kind::Histogram ? m_mode == 1 : m_mode == 0;
        return luma ? QList<int>{3} : QList<int>{0, 1, 2};
    }

    void paintHistogram(QPainter &painter)
    {
        const QFontMetrics metrics(font());
        const QRectF plot = QRectF(rect()).adjusted(8, 6, -8, -(metrics.height() + 6));
        const Scopes::Axis &axis = m_result->axis;

        // Scale to the tallest bin, but leave out the two end bins (clipped blacks or whites) and
        // let single spikes (flat backgrounds, graphics) run off the top rather than flatten
        // everything else: at most a few times the average filled bin.
        const QList<int> shown = channels();
        quint32 tallest = 0;
        double filledSum = 0.0;
        int filled = 0;
        for (int c : shown) {
            const auto &bins = m_result->histogram[size_t(c)];
            for (int i = 1; i < Scopes::Bins - 1; ++i) {
                const quint32 count = bins[size_t(i)];
                tallest = std::max(tallest, count);
                if (count > 0) {
                    filledSum += double(count);
                    ++filled;
                }
            }
        }
        double scale = filled > 0 ? std::min(double(tallest), 6.0 * filledSum / filled) : 0.0;
        if (scale <= 0.0) {
            for (int c : shown) {
                const auto &bins = m_result->histogram[size_t(c)];
                scale = std::max(scale, double(*std::max_element(bins.begin(), bins.end())));
            }
        }
        scale = std::max(scale, 1.0);

        painter.setRenderHint(QPainter::Antialiasing);
        double labelsEnd = -1e9; // right edge of the last label drawn, to skip overlapping ones
        for (const Scopes::Axis::Tick &tick : axis.ticks()) {
            const double x = plot.left() + double(tick.position) * plot.width();
            painter.setPen(QPen(tick.highlight ? HighlightGridColor : GridColor, 1));
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            if (!tick.label.isEmpty() && !tick.highlight) {
                const double w = metrics.horizontalAdvance(tick.label);
                const double left = std::clamp(x - w / 2, 0.0, double(width()) - w);
                if (left < labelsEnd + 6) {
                    continue;
                }
                painter.setPen(LabelColor);
                painter.drawText(QPointF(left, plot.bottom() + metrics.ascent() + 3), tick.label);
                labelsEnd = left + w;
            }
        }

        const bool rgb = shown.size() > 1;
        if (rgb) {
            painter.setCompositionMode(QPainter::CompositionMode_Plus);
        }
        for (int c : shown) {
            const auto &bins = m_result->histogram[size_t(c)];
            QPainterPath path(QPointF(plot.left(), plot.bottom()));
            for (int i = 0; i < Scopes::Bins; ++i) {
                const double barHeight = std::min(1.0, double(bins[size_t(i)]) / scale) * plot.height();
                const double x0 = plot.left() + plot.width() * i / Scopes::Bins;
                const double x1 = plot.left() + plot.width() * (i + 1) / Scopes::Bins;
                path.lineTo(x0, plot.bottom() - barHeight);
                path.lineTo(x1, plot.bottom() - barHeight);
            }
            path.lineTo(plot.right(), plot.bottom());
            path.closeSubpath();
            QColor fill = ChannelColors[c];
            fill.setAlpha(rgb ? 200 : 150);
            painter.fillPath(path, fill);
            if (!rgb) {
                painter.setPen(QPen(ChannelColors[c], 1));
                painter.drawPath(path);
            }
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        if (m_hover) {
            for (int c : shown) {
                const double x = plot.left() + double(axis.position(m_hoverValues[size_t(c)])) * plot.width();
                painter.setPen(QPen(rgb ? ChannelColors[c] : Qt::white, 1, Qt::DashLine));
                painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            }
        }
    }

    void paintWaveform(QPainter &painter)
    {
        const QFontMetrics metrics(font());
        const Scopes::Axis &axis = m_result->axis;
        const QList<Scopes::Axis::Tick> ticks = axis.ticks();
        int labelWidth = 0;
        for (const Scopes::Axis::Tick &tick : ticks) {
            labelWidth = std::max(labelWidth, metrics.horizontalAdvance(tick.label));
        }
        const QRectF plot = QRectF(rect()).adjusted(labelWidth + 12, metrics.height() / 2 + 2, -8,
                                                    -(metrics.height() / 2 + 2));

        const QImage &image = m_mode == 0 ? m_result->lumaWaveform
                            : m_mode == 1 ? m_result->parade
                                          : m_result->rgbWaveform;
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(plot, image);

        painter.setRenderHint(QPainter::Antialiasing);
        for (const Scopes::Axis::Tick &tick : ticks) {
            const double y = plot.bottom() - double(tick.position) * plot.height();
            painter.setPen(QPen(tick.highlight ? HighlightGridColor : GridColor, 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            if (tick.highlight) {
                // Inside the plot on the right, where it can't collide with the axis labels.
                painter.setPen(HighlightGridColor.lighter(180));
                const double w = metrics.horizontalAdvance(tick.label);
                painter.drawText(QPointF(plot.right() - 4 - w, y - 3), tick.label);
            } else if (!tick.label.isEmpty()) {
                painter.setPen(LabelColor);
                const double w = metrics.horizontalAdvance(tick.label);
                painter.drawText(QPointF(plot.left() - 6 - w, y + metrics.ascent() / 2.0 - 1), tick.label);
            }
        }
        const int panes = m_mode == 1 ? 3 : 1;
        const double paneWidth = plot.width() / panes;
        painter.setPen(QPen(GridColor, 1));
        for (int i = 1; i < panes; ++i) {
            const double x = plot.left() + paneWidth * i;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        if (m_hover) {
            for (int pane = 0; pane < panes; ++pane) {
                const double x = plot.left() + paneWidth * (pane + double(m_hoverAcross));
                painter.setPen(QPen(QColor(255, 255, 255, 110), 1, Qt::DashLine));
                painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
                const QList<int> marks = m_mode == 0 ? QList<int>{3} : m_mode == 1 ? QList<int>{pane} : QList<int>{0, 1, 2};
                for (int c : marks) {
                    const double y = plot.bottom() - double(axis.position(m_hoverValues[size_t(c)])) * plot.height();
                    painter.setPen(QPen(m_mode == 0 ? QColor(Qt::white) : ChannelColors[c], 2));
                    painter.drawLine(QPointF(x - 7, y), QPointF(x + 7, y));
                }
            }
        }
    }

    Kind m_kind;
    int m_mode = 0;
    const Scopes::Result *m_result = nullptr;
    QString m_message;
    bool m_hover = false;
    std::array<float, 4> m_hoverValues {};
    float m_hoverAcross = 0.0f;
};

ScopesPanel::ScopesPanel(QWidget *parent)
    : QWidget(parent)
{
    m_sourceBox = new QComboBox(this);
    m_sourceBox->addItem(QStringLiteral("Display"));
    m_sourceBox->addItem(QStringLiteral("Stored values"));
    m_sourceBox->setToolTip(QStringLiteral(
        "Display: the picture as shown, with the color view or LUT (matches the readout's display values).\n"
        "Stored values: the file's own values; scene-linear float images (EXR) get a scale in stops."));
    m_histogramBox = new QComboBox(this);
    m_histogramBox->addItem(QStringLiteral("RGB"));
    m_histogramBox->addItem(QStringLiteral("Luma"));
    m_histogramBox->setToolTip(QStringLiteral("Histogram channels"));
    m_waveformBox = new QComboBox(this);
    m_waveformBox->addItem(QStringLiteral("Luma"));
    m_waveformBox->addItem(QStringLiteral("RGB parade"));
    m_waveformBox->addItem(QStringLiteral("RGB overlay"));
    m_waveformBox->setToolTip(QStringLiteral("Waveform type"));

    QSettings settings;
    m_sourceBox->setCurrentIndex(std::clamp(settings.value(SettingsSource, 0).toInt(), 0, 1));
    m_histogramBox->setCurrentIndex(std::clamp(settings.value(SettingsHistogram, 0).toInt(), 0, 1));
    m_waveformBox->setCurrentIndex(std::clamp(settings.value(SettingsWaveform, 1).toInt(), 0, 2));

    m_histogramView = new ScopeView(ScopeView::Kind::Histogram, this);
    m_waveformView = new ScopeView(ScopeView::Kind::Waveform, this);
    m_histogramView->setResult(&m_result);
    m_waveformView->setResult(&m_result);
    m_histogramView->setMode(m_histogramBox->currentIndex());
    m_waveformView->setMode(m_waveformBox->currentIndex());

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_histogramView);
    splitter->addWidget(m_waveformView);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setForegroundRole(QPalette::PlaceholderText);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(6);
    controls->addWidget(m_sourceBox, 1);
    controls->addWidget(new QLabel(QStringLiteral("Histogram"), this));
    controls->addWidget(m_histogramBox);
    controls->addWidget(new QLabel(QStringLiteral("Waveform"), this));
    controls->addWidget(m_waveformBox);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(controls);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_infoLabel);

    connect(m_sourceBox, &QComboBox::currentIndexChanged, this, [this]() {
        saveSettings();
        schedule();
    });
    connect(m_histogramBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        saveSettings();
        m_histogramView->setMode(index);
    });
    connect(m_waveformBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        saveSettings();
        m_waveformView->setMode(index);
    });

    m_pool.setMaxThreadCount(1);
    m_timer.setSingleShot(true);
    m_timer.setInterval(40); // while scrubbing or playing, follow along without queueing up work
    connect(&m_timer, &QTimer::timeout, this, &ScopesPanel::startJob);
    clear();
}

ScopesPanel::~ScopesPanel()
{
    // The job posts its result back to this panel; let it finish first.
    m_pool.waitForDone();
}

void ScopesPanel::setImages(const QImage &source, const QImage &display, ColorTransform::Input input,
                            const QString &viewName)
{
    m_source = source;
    m_display = display;
    m_input = input;
    m_viewName = viewName;
    schedule();
}

void ScopesPanel::clear()
{
    m_source = QImage();
    m_display = QImage();
    m_dirty = false;
    m_timer.stop();
    m_result = Scopes::Result();
    const QString message = QStringLiteral("Open an image to see its histogram and waveform.");
    m_histogramView->setMessage(message);
    m_waveformView->setMessage(QString());
    m_infoLabel->clear();
    updateHover();
}

void ScopesPanel::setHoveredPixel(const QPoint &pixel)
{
    m_hoveredPixel = pixel;
    if (isVisible()) {
        updateHover();
    }
}

void ScopesPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_dirty) {
        m_timer.start();
    }
}

void ScopesPanel::schedule()
{
    if (m_source.isNull()) {
        return;
    }
    m_dirty = true;
    if (isVisible()) {
        m_timer.start();
    }
}

void ScopesPanel::startJob()
{
    if (!m_dirty || m_source.isNull()) {
        return;
    }
    if (m_running) {
        return; // applyResult() starts the next one
    }
    m_dirty = false;
    m_running = true;

    const bool stored = m_sourceBox->currentIndex() == int(Source::Stored);
    const QImage image = stored ? m_source : m_display;
    const ColorTransform::Input input = ColorTransform::resolveInput(m_input, m_source);
    const bool sceneLinear = stored
        && (input == ColorTransform::Input::Linear || input == ColorTransform::Input::Aces2065
            || input == ColorTransform::Input::AcesCg);
    const QString description = stored
        ? QStringLiteral("Stored values · %1").arg(ColorTransform::describeSource(m_source, m_input))
        : QStringLiteral("Display · %1").arg(m_viewName);

    // The destructor waits for the job, so the panel outlives it; a result still queued when the
    // panel goes away is dropped with it.
    m_pool.start([this, image, sceneLinear, description]() {
        Scopes::Result result = Scopes::compute(image, sceneLinear);
        result.description = description;
        QMetaObject::invokeMethod(this, [this, result]() {
            applyResult(result);
        }, Qt::QueuedConnection);
    });
}

void ScopesPanel::applyResult(const Scopes::Result &result)
{
    m_running = false;
    if (m_source.isNull()) {
        return; // cleared while computing
    }
    m_result = result;
    QString info = result.description;
    if (result.sampleStep > 1) {
        info += QStringLiteral(" · sampled every %1 px").arg(result.sampleStep);
    }
    m_infoLabel->setText(info);
    m_histogramView->update();
    m_waveformView->update();
    updateHover();
    if (m_dirty && isVisible()) {
        m_timer.start();
    }
}

void ScopesPanel::updateHover()
{
    const bool stored = m_sourceBox->currentIndex() == int(Source::Stored);
    const QImage &image = stored ? m_source : m_display;
    std::array<float, 4> values {};
    const bool shown = m_result.valid && !image.isNull() && image.rect().contains(m_hoveredPixel);
    float across = 0.0f;
    if (shown) {
        Scopes::readPixel(image, m_hoveredPixel.x(), m_hoveredPixel.y(), values.data());
        values[3] = 0.2126f * values[0] + 0.7152f * values[1] + 0.0722f * values[2];
        across = (float(m_hoveredPixel.x()) + 0.5f) / float(image.width());
    }
    m_histogramView->setHover(shown, values, across);
    m_waveformView->setHover(shown, values, across);
}

void ScopesPanel::saveSettings() const
{
    QSettings settings;
    settings.setValue(SettingsSource, m_sourceBox->currentIndex());
    settings.setValue(SettingsHistogram, m_histogramBox->currentIndex());
    settings.setValue(SettingsWaveform, m_waveformBox->currentIndex());
}
