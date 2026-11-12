#pragma once

#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QStackedLayout;
class QToolButton;

// Explorer-style path bar: the current folder as clickable segments ("breadcrumbs"). Click the
// empty space (or press Alt+D / F4 in the window) to type or paste a path instead.
class PathBar final : public QWidget
{
    Q_OBJECT

public:
    explicit PathBar(QWidget *parent = nullptr);

    void setPath(const QString &path);
    QString path() const { return m_path; }

public slots:
    void startEditing();

signals:
    // A segment was clicked or a path was typed: a folder to open, or a file to show.
    void pathActivated(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Segment
    {
        QString label;
        QString path;
    };
    static QList<Segment> splitPath(const QString &path);
    void rebuildCrumbs();
    void fitCrumbs();
    void finishEditing(bool accept);

    QString m_path;
    QStackedLayout *m_stack = nullptr;
    QWidget *m_crumbArea = nullptr;
    QHBoxLayout *m_crumbLayout = nullptr;
    QToolButton *m_ellipsisButton = nullptr;
    QList<QToolButton *> m_crumbButtons;
    QLineEdit *m_edit = nullptr;
};
