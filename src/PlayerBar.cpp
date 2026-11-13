#include "PlayerBar.h"

#include "Icons.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QProxyStyle>
#include <QToolButton>

#include <cmath>

namespace {

constexpr int CustomFpsData = -1;

// The usual film, video and web rates. NTSC rates are the exact 1000/1001 fractions.
const double PresetRates[] = {
    12.0, 15.0, 24000.0 / 1001.0, 24.0, 25.0, 30000.0 / 1001.0, 30.0, 48.0, 50.0, 60000.0 / 1001.0, 60.0,
};

bool sameRate(double a, double b)
{
    return std::abs(a - b) < 0.0005;
}

// Makes a left click on a slider jump straight to that spot and keep dragging from there, like a
// video player's scrub bar, instead of paging towards it.
class ScrubStyle final : public QProxyStyle
{
public:
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons) {
            return Qt::LeftButton;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QToolButton *makeButton(QWidget *parent, Icons::Kind icon, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(Icons::icon(icon));
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(4);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setMinimumWidth(60);
    m_nameLabel->setMaximumWidth(280);
    m_nameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    QToolButton *firstButton = makeButton(this, Icons::Kind::FirstFrame, QStringLiteral("First frame (Home)"));
    QToolButton *previousButton = makeButton(this, Icons::Kind::PreviousFrame, QStringLiteral("Previous frame (Left)"));
    m_playButton = makeButton(this, Icons::Kind::Play, QStringLiteral("Play (Space)"));
    QToolButton *nextButton = makeButton(this, Icons::Kind::NextFrame, QStringLiteral("Next frame (Right)"));
    QToolButton *lastButton = makeButton(this, Icons::Kind::LastFrame, QStringLiteral("Last frame (End)"));
    for (QToolButton *button : {previousButton, nextButton}) {
        button->setAutoRepeat(true); // hold to step
    }
    connect(firstButton, &QToolButton::clicked, this, &PlayerBar::firstRequested);
    connect(previousButton, &QToolButton::clicked, this, [this]() { emit stepRequested(-1); });
    connect(m_playButton, &QToolButton::clicked, this, &PlayerBar::playToggled);
    connect(nextButton, &QToolButton::clicked, this, [this]() { emit stepRequested(1); });
    connect(lastButton, &QToolButton::clicked, this, &PlayerBar::lastRequested);

    m_slider = new QSlider(Qt::Horizontal, this);
    auto *scrubStyle = new ScrubStyle;
    scrubStyle->setParent(m_slider);
    m_slider->setStyle(scrubStyle);
    m_slider->setFocusPolicy(Qt::NoFocus);
    m_slider->setMinimum(0);
    m_slider->setMaximum(0);
    m_slider->setToolTip(QStringLiteral("Drag or click to scrub through the sequence"));
    connect(m_slider, &QSlider::valueChanged, this, &PlayerBar::scrubbed);

    m_frameLabel = new QLabel(this);
    m_frameLabel->setToolTip(QStringLiteral("Frame number, and position in the sequence"));
    m_frameLabel->setTextFormat(Qt::RichText);

    m_fpsCombo = new QComboBox(this);
    m_fpsCombo->setFocusPolicy(Qt::NoFocus);
    m_fpsCombo->setToolTip(QStringLiteral("Playback speed in frames per second"));
    m_fpsCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (const double rate : PresetRates) {
        m_fpsCombo->addItem(QStringLiteral("%1 fps").arg(fpsText(rate)), rate);
    }
    m_fpsCombo->addItem(QStringLiteral("Custom…"), CustomFpsData);
    connect(m_fpsCombo, &QComboBox::activated, this, [this](int index) {
        const double rate = m_fpsCombo->itemData(index).toDouble();
        if (rate <= 0) {
            chooseCustomFps();
            return;
        }
        if (!sameRate(rate, m_fps)) {
            m_fps = rate;
            emit fpsChanged(m_fps);
        }
    });

    m_loopButton = makeButton(this, Icons::Kind::Loop, QStringLiteral("Loop: start again after the last frame"));
    m_loopButton->setCheckable(true);
    m_loopButton->setChecked(true);
    connect(m_loopButton, &QToolButton::toggled, this, &PlayerBar::loopChanged);

    m_realTimeButton = new QToolButton(this);
    m_realTimeButton->setText(QStringLiteral("Real time"));
    m_realTimeButton->setCheckable(true);
    m_realTimeButton->setAutoRaise(true);
    m_realTimeButton->setFocusPolicy(Qt::NoFocus);
    m_realTimeButton->setToolTip(QStringLiteral(
        "On: keep to the frame rate and skip frames that don't load in time.\n"
        "Off: show every frame, and play slower when the disk can't keep up."));
    connect(m_realTimeButton, &QToolButton::toggled, this, &PlayerBar::realTimeChanged);

    m_measuredLabel = new QLabel(this);
    m_measuredLabel->setToolTip(QStringLiteral("The frame rate playback is reaching"));
    m_measuredLabel->setMinimumWidth(m_measuredLabel->fontMetrics().horizontalAdvance(QStringLiteral("888.8 fps")));

    layout->addWidget(m_nameLabel);
    layout->addSpacing(6);
    layout->addWidget(firstButton);
    layout->addWidget(previousButton);
    layout->addWidget(m_playButton);
    layout->addWidget(nextButton);
    layout->addWidget(lastButton);
    layout->addSpacing(6);
    layout->addWidget(m_slider, 1);
    layout->addSpacing(6);
    layout->addWidget(m_frameLabel);
    layout->addSpacing(6);
    layout->addWidget(m_fpsCombo);
    layout->addWidget(m_loopButton);
    layout->addWidget(m_realTimeButton);
    layout->addWidget(m_measuredLabel);

    selectFps(m_fps);
}

void PlayerBar::setSequence(const QString &name, int frameCount)
{
    m_name = name;
    m_frameCount = frameCount;
    m_nameLabel->setToolTip(name);
    m_nameLabel->setText(m_nameLabel->fontMetrics().elidedText(name, Qt::ElideMiddle, m_nameLabel->maximumWidth()));
    const QSignalBlocker blocker(m_slider);
    m_slider->setMaximum(qMax(0, frameCount - 1));
    m_slider->setPageStep(qMax(1, frameCount / 10));
    // Room for the widest position text, so the bar doesn't jump around while playing.
    const QString widest = QString(QString::number(frameCount).size() + 6, QLatin1Char('8'));
    m_frameLabel->setMinimumWidth(m_frameLabel->fontMetrics().horizontalAdvance(widest + QStringLiteral("  /  ")) + 8);
}

void PlayerBar::setPosition(int position, const QString &frameNumber)
{
    {
        const QSignalBlocker blocker(m_slider);
        if (!m_slider->isSliderDown()) {
            m_slider->setValue(position);
        }
    }
    const QString dim = palette().color(QPalette::PlaceholderText).name();
    m_frameLabel->setText(QStringLiteral("<b>%1</b>&nbsp;&nbsp;<span style='color:%2'>%3&nbsp;/&nbsp;%4</span>")
                              .arg(frameNumber.toHtmlEscaped(), dim)
                              .arg(position + 1)
                              .arg(m_frameCount));
}

void PlayerBar::setPlaying(bool playing)
{
    m_playing = playing;
    m_playButton->setIcon(Icons::icon(playing ? Icons::Kind::Pause : Icons::Kind::Play));
    m_playButton->setToolTip(playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)"));
    if (!playing) {
        setMeasuredFps(0);
    }
}

void PlayerBar::setMeasuredFps(double fps)
{
    if (fps <= 0) {
        m_measuredLabel->clear();
        return;
    }
    // Amber when playback falls clearly behind the chosen rate.
    const bool behind = fps < m_fps * 0.95;
    m_measuredLabel->setText(QStringLiteral("%1 fps").arg(fps, 0, 'f', 1));
    m_measuredLabel->setStyleSheet(behind ? QStringLiteral("color: #e0a030;") : QString());
}

void PlayerBar::setFps(double fps)
{
    if (fps <= 0) {
        return;
    }
    m_fps = fps;
    selectFps(fps);
}

bool PlayerBar::loop() const
{
    return m_loopButton->isChecked();
}

void PlayerBar::setLoop(bool on)
{
    m_loopButton->setChecked(on);
}

bool PlayerBar::realTime() const
{
    return m_realTimeButton->isChecked();
}

void PlayerBar::setRealTime(bool on)
{
    m_realTimeButton->setChecked(on);
}

QString PlayerBar::fpsText(double fps)
{
    QString text = QString::number(fps, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

void PlayerBar::chooseCustomFps()
{
    bool ok = false;
    const double rate = QInputDialog::getDouble(window(), QStringLiteral("Playback Speed"),
                                                QStringLiteral("Frames per second:"), m_fps, 0.1, 1000.0, 3, &ok);
    if (ok && rate > 0) {
        m_fps = rate;
        selectFps(rate);
        emit fpsChanged(m_fps);
    } else {
        selectFps(m_fps); // put the combo box back on the current rate
    }
}

// Selects the rate in the combo box, adding it before "Custom…" when it isn't one of the presets.
void PlayerBar::selectFps(double fps)
{
    const QSignalBlocker blocker(m_fpsCombo);
    for (int i = 0; i < m_fpsCombo->count(); ++i) {
        if (sameRate(m_fpsCombo->itemData(i).toDouble(), fps)) {
            m_fpsCombo->setCurrentIndex(i);
            return;
        }
    }
    const int custom = m_fpsCombo->count() - 1;
    m_fpsCombo->insertItem(custom, QStringLiteral("%1 fps").arg(fpsText(fps)), fps);
    m_fpsCombo->setCurrentIndex(custom);
}
