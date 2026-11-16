#include "PreferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , m_originalTheme(Theme::saved())
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(560, 360);

    m_sections = new QListWidget(this);
    m_sections->setFixedWidth(150);
    m_pages = new QStackedWidget(this);

    // One entry per page; add further settings pages here.
    m_sections->addItem(QStringLiteral("Appearance"));
    m_pages->addWidget(createAppearancePage());
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_sections->setCurrentRow(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *body = new QHBoxLayout;
    body->addWidget(m_sections);
    body->addWidget(m_pages, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);
}

QWidget *PreferencesDialog::createAppearancePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *heading = new QLabel(QStringLiteral("<b>Appearance</b>"), page);
    layout->addWidget(heading);

    m_themeCombo = new QComboBox(page);
    for (Theme::Id id : Theme::all()) {
        m_themeCombo->addItem(Theme::displayName(id), int(id));
    }
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(int(m_originalTheme)));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        Theme::apply(static_cast<Theme::Id>(m_themeCombo->currentData().toInt()));
    });

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Theme:"), m_themeCombo);
    layout->addLayout(form);

    auto *note = new QLabel(QStringLiteral("The theme is tried out as you pick it. "
                                           "\"Windows setting\" follows Windows' light or dark mode."),
                            page);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

void PreferencesDialog::accept()
{
    Theme::save(static_cast<Theme::Id>(m_themeCombo->currentData().toInt()));
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    Theme::apply(m_originalTheme);
    QDialog::reject();
}
