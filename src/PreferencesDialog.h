#pragma once

#include "Theme.h"

#include <QDialog>

class QComboBox;
class QListWidget;
class QStackedWidget;

// The Preferences window: a list of sections on the left and the chosen section on the right, so
// later settings can get their own page. For now there is one section, Appearance, with the theme.
// A theme is tried out as soon as it is picked; Cancel puts the previous one back.
class PreferencesDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    void accept() override;
    void reject() override;

private:
    QWidget *createAppearancePage();

    QListWidget *m_sections = nullptr;
    QStackedWidget *m_pages = nullptr;
    QComboBox *m_themeCombo = nullptr;
    Theme::Id m_originalTheme = Theme::Id::System;
};
