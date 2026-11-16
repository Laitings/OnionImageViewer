#pragma once

#include "Theme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QStackedWidget;

// The Preferences window: a list of sections on the left and the chosen section on the right.
// To add a setting, put its key in Preferences.h, a control on a page here (read in the page's
// create function, written in accept()) and its effect in MainWindow::applyPreferences().
// A theme is tried out as soon as it is picked and Cancel puts the previous one back; everything
// else is saved when OK is pressed.
class PreferencesDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;
    void reject() override;

private:
    void addPage(const QString &title, QWidget *page);
    QWidget *createAppearancePage();
    QWidget *createViewerPage();
    QWidget *createGridPage();
    QWidget *createStartupPage();
    QWidget *createFilesPage();
    QWidget *createFormatsPage();
    QWidget *createPerformancePage();
    QWidget *createUpdatesPage();
    void updateCacheSize();
    void testGhostscript();

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;

    QComboBox *m_themeCombo = nullptr;
    Theme::Id m_originalTheme = Theme::Id::System;

    QComboBox *m_alphaCombo = nullptr;
    QComboBox *m_openZoomCombo = nullptr;
    QComboBox *m_wheelCombo = nullptr;

    QComboBox *m_gridViewCombo = nullptr;
    QSpinBox *m_thumbnailSize = nullptr;
    QCheckBox *m_showHidden = nullptr;

    QComboBox *m_startupCombo = nullptr;
    QComboBox *m_startupPinnedCombo = nullptr;

    QComboBox *m_conflictCombo = nullptr;

    QLineEdit *m_ghostscriptPath = nullptr;
    QLabel *m_ghostscriptResult = nullptr;

    QSpinBox *m_cacheLimit = nullptr;
    QLabel *m_cacheSize = nullptr;

    QCheckBox *m_checkUpdates = nullptr;
};
