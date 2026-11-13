#pragma once

#include "BatchRename.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <atomic>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeView;
class BatchRenamePreview;

// The Batch Rename dialog: a name pattern with tokens, a counter, find and replace (optionally a
// regular expression) and case changes, with a live old -> new preview that marks every name
// that can't be used. Rename stays disabled while any name has a problem. The dialog only plans;
// MainWindow carries out moves() with BatchRename::apply and keeps them for undo.
class BatchRenameDialog final : public QDialog
{
    Q_OBJECT

public:
    // paths: the selected files and folders, in grid order (which sets the counter order).
    // keepFrameNumbers in initial is replaced by a guess from the selection.
    BatchRenameDialog(const QStringList &paths, const BatchRename::Options &initial, QWidget *parent = nullptr);
    ~BatchRenameDialog() override;

    BatchRename::Options options() const;
    // The renames to make, for the rows that change. Only call after the dialog was accepted.
    QList<BatchRename::Move> moves() const;

private:
    void setOptions(const BatchRename::Options &values);
    void schedulePreview();
    void updatePreview();
    // Reads the Exif dates on a background thread, the first time the pattern uses {date}.
    void startReadingDates();

    QList<BatchRename::Source> m_sources;
    QSet<QString> m_existing;
    QList<BatchRename::Result> m_results;
    QLineEdit *m_pattern = nullptr;
    QSpinBox *m_counterStart = nullptr;
    QSpinBox *m_counterStep = nullptr;
    QSpinBox *m_counterPadding = nullptr;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_matchCase = nullptr;
    QComboBox *m_nameCase = nullptr;
    QComboBox *m_extensionCase = nullptr;
    QCheckBox *m_keepFrames = nullptr;
    QTreeView *m_view = nullptr;
    BatchRenamePreview *m_preview = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_renameButton = nullptr;
    QTimer m_previewTimer;
    QThreadPool m_datePool;
    std::atomic<bool> m_stopReading{false};
    bool m_datesStarted = false;
    int m_datesToRead = 0;
    int m_datesRead = 0;
};
