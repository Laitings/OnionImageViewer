#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;
class QToolButton;

// The transport bar under the viewer when the open image is part of an image sequence: first /
// previous / play / next / last, a scrub bar, the frame number and position, the playback frame
// rate (presets or a custom rate), loop, and "Real time", which skips frames that aren't loaded
// in time instead of slowing down. It only shows state and reports what the user does; MainWindow
// runs the playback. None of its controls take keyboard focus, so Space and the arrow keys keep
// working on the viewer.
class PlayerBar final : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerBar(QWidget *parent = nullptr);

    // The sequence's name (with its frame range) and how many frames it has.
    void setSequence(const QString &name, int frameCount);
    // The frame shown (0-based position in the sequence) and its number as written in the name.
    void setPosition(int position, const QString &frameNumber);
    void setPlaying(bool playing);
    // The rate playback actually reaches, shown while playing; 0 or less hides it.
    void setMeasuredFps(double fps);

    double fps() const { return m_fps; }
    void setFps(double fps);
    bool loop() const;
    void setLoop(bool on);
    bool realTime() const;
    void setRealTime(bool on);

    // "23.976", "24", "29.97": a frame rate as people write it.
    static QString fpsText(double fps);

signals:
    void playToggled();
    void stepRequested(int delta);
    void firstRequested();
    void lastRequested();
    // The user dragged or clicked the scrub bar to this position.
    void scrubbed(int position);
    void fpsChanged(double fps);
    void loopChanged(bool on);
    void realTimeChanged(bool on);

private:
    void chooseCustomFps();
    void selectFps(double fps);

    QLabel *m_nameLabel = nullptr;
    QToolButton *m_playButton = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_frameLabel = nullptr;
    QComboBox *m_fpsCombo = nullptr;
    QToolButton *m_loopButton = nullptr;
    QToolButton *m_realTimeButton = nullptr;
    QLabel *m_measuredLabel = nullptr;
    QString m_name;
    int m_frameCount = 0;
    double m_fps = 24.0;
    bool m_playing = false;
};
