#include "PreferencesDialog.h"

#include "ImageViewport.h"
#include "Preferences.h"
#include "ThumbnailCache.h"
#include "VectorReader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// Pinned folders as MainWindow stores them (the key is owned by MainWindow.cpp).
const QString SettingsPinnedFolders = QStringLiteral("folders/pinned");

constexpr int MinThumbnailSize = 64;  // keep in step with MainWindow.cpp
constexpr int MaxThumbnailSize = 256;
constexpr int ThumbnailStep = 16;

// A page: a bold heading, a form with the controls, and a stretch under it.
struct PageParts {
    QWidget *page;
    QFormLayout *form;
    QVBoxLayout *layout;
};

PageParts makePage(QWidget *parent, const QString &heading)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(heading), page));
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    return {page, form, layout};
}

QLabel *note(QWidget *parent, const QString &text)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    return label;
}

void selectData(QComboBox *combo, const QVariant &value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

QString megabytesText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
    , m_originalTheme(Theme::saved())
{
    setWindowTitle(QStringLiteral("Preferences"));
    resize(680, 440);

    m_sections = new QListWidget(this);
    m_sections->setFixedWidth(150);
    m_pages = new QStackedWidget(this);

    // One entry per page; add further settings pages here.
    addPage(QStringLiteral("Appearance"), createAppearancePage());
    addPage(QStringLiteral("Viewer"), createViewerPage());
    addPage(QStringLiteral("Grid"), createGridPage());
    addPage(QStringLiteral("Startup"), createStartupPage());
    addPage(QStringLiteral("Files"), createFilesPage());
    addPage(QStringLiteral("Formats"), createFormatsPage());
    addPage(QStringLiteral("Performance"), createPerformancePage());
    addPage(QStringLiteral("Updates"), createUpdatesPage());
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

void PreferencesDialog::addPage(const QString &title, QWidget *page)
{
    m_sections->addItem(title);
    m_pages->addWidget(page);
}

QWidget *PreferencesDialog::createAppearancePage()
{
    const PageParts parts = makePage(this, QStringLiteral("Appearance"));

    m_themeCombo = new QComboBox(parts.page);
    for (Theme::Id id : Theme::all()) {
        m_themeCombo->addItem(Theme::displayName(id), int(id));
    }
    selectData(m_themeCombo, int(m_originalTheme));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        Theme::apply(static_cast<Theme::Id>(m_themeCombo->currentData().toInt()));
    });
    parts.form->addRow(QStringLiteral("Theme:"), m_themeCombo);

    parts.layout->addWidget(note(parts.page, QStringLiteral("The theme is tried out as you pick it. "
                                                            "\"Windows setting\" follows Windows' light or dark mode.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createViewerPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Viewer"));
    const QSettings settings;

    m_alphaCombo = new QComboBox(parts.page);
    m_alphaCombo->addItem(QStringLiteral("Alpha (checkerboard)"), int(ImageViewport::AlphaBackground::Checker));
    m_alphaCombo->addItem(QStringLiteral("Black"), int(ImageViewport::AlphaBackground::Black));
    m_alphaCombo->addItem(QStringLiteral("Grey"), int(ImageViewport::AlphaBackground::Grey));
    m_alphaCombo->addItem(QStringLiteral("White"), int(ImageViewport::AlphaBackground::White));
    selectData(m_alphaCombo, settings.value(Preferences::AlphaBackground, int(ImageViewport::AlphaBackground::Checker)).toInt());
    parts.form->addRow(QStringLiteral("Behind transparent pixels:"), m_alphaCombo);

    m_openZoomCombo = new QComboBox(parts.page);
    m_openZoomCombo->addItem(QStringLiteral("100% if it fits, otherwise fit to window"), QStringLiteral("auto"));
    m_openZoomCombo->addItem(QStringLiteral("Always fit to window"), QStringLiteral("fit"));
    m_openZoomCombo->addItem(QStringLiteral("Always 100%"), QStringLiteral("actual"));
    selectData(m_openZoomCombo, settings.value(Preferences::OpenZoom, QStringLiteral("auto")).toString());
    parts.form->addRow(QStringLiteral("Zoom when an image opens:"), m_openZoomCombo);

    m_wheelCombo = new QComboBox(parts.page);
    m_wheelCombo->addItem(QStringLiteral("Zooms (Ctrl + wheel the same)"), QStringLiteral("zoom"));
    m_wheelCombo->addItem(QStringLiteral("Goes to the next or previous image (Ctrl + wheel zooms)"), QStringLiteral("step"));
    selectData(m_wheelCombo, settings.value(Preferences::WheelAction, QStringLiteral("zoom")).toString());
    parts.form->addRow(QStringLiteral("Mouse wheel over an image:"), m_wheelCombo);

    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createGridPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Grid"));
    const QSettings settings;

    m_gridViewCombo = new QComboBox(parts.page);
    m_gridViewCombo->addItem(QStringLiteral("Icons"), QStringLiteral("icons"));
    m_gridViewCombo->addItem(QStringLiteral("List"), QStringLiteral("list"));
    m_gridViewCombo->addItem(QStringLiteral("Details"), QStringLiteral("details"));
    m_gridViewCombo->addItem(QStringLiteral("Tiles"), QStringLiteral("tiles"));
    selectData(m_gridViewCombo, settings.value(Preferences::GridView, QStringLiteral("icons")).toString());
    parts.form->addRow(QStringLiteral("View of the files:"), m_gridViewCombo);

    m_thumbnailSize = new QSpinBox(parts.page);
    m_thumbnailSize->setRange(MinThumbnailSize, MaxThumbnailSize);
    m_thumbnailSize->setSingleStep(ThumbnailStep);
    m_thumbnailSize->setSuffix(QStringLiteral(" px"));
    m_thumbnailSize->setValue(settings.value(Preferences::ThumbnailSize, 128).toInt());
    parts.form->addRow(QStringLiteral("Thumbnail size (Icons view):"), m_thumbnailSize);

    m_showHidden = new QCheckBox(QStringLiteral("Show hidden files and folders"), parts.page);
    m_showHidden->setChecked(settings.value(Preferences::ShowHidden, false).toBool());
    parts.form->addRow(QString(), m_showHidden);

    parts.layout->addWidget(note(parts.page, QStringLiteral("These are also the view and size you set with the slider and the "
                                                            "right-click menu; the grid remembers the last ones used.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createStartupPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Startup"));
    const QSettings settings;

    m_startupCombo = new QComboBox(parts.page);
    m_startupCombo->addItem(QStringLiteral("Nothing (start empty)"), QStringLiteral("none"));
    m_startupCombo->addItem(QStringLiteral("The folder I had open last time"), QStringLiteral("last"));
    m_startupCombo->addItem(QStringLiteral("A pinned folder"), QStringLiteral("pinned"));
    m_startupCombo->addItem(QStringLiteral("My Pictures folder"), QStringLiteral("pictures"));
    selectData(m_startupCombo, settings.value(Preferences::StartupFolder, QStringLiteral("none")).toString());
    parts.form->addRow(QStringLiteral("Open at startup:"), m_startupCombo);

    m_startupPinnedCombo = new QComboBox(parts.page);
    const QStringList pinned = settings.value(SettingsPinnedFolders).toStringList();
    for (const QString &folder : pinned) {
        m_startupPinnedCombo->addItem(QDir::toNativeSeparators(folder), folder);
    }
    if (pinned.isEmpty()) {
        m_startupPinnedCombo->addItem(QStringLiteral("(no pinned folders yet)"), QString());
    }
    selectData(m_startupPinnedCombo, settings.value(Preferences::StartupPinned).toString());
    parts.form->addRow(QStringLiteral("Pinned folder:"), m_startupPinnedCombo);
    const auto updatePinned = [this]() {
        m_startupPinnedCombo->setEnabled(m_startupCombo->currentData().toString() == QLatin1String("pinned"));
    };
    connect(m_startupCombo, &QComboBox::currentIndexChanged, this, updatePinned);
    updatePinned();

    parts.layout->addWidget(note(parts.page, QStringLiteral("Pin folders from the folder panel's Quick access. "
                                                            "Opening an image from Explorer always shows that image.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createFilesPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Files"));
    const QSettings settings;

    m_conflictCombo = new QComboBox(parts.page);
    m_conflictCombo->addItem(QStringLiteral("Ask me each time"), QStringLiteral("ask"));
    m_conflictCombo->addItem(QStringLiteral("Keep both (the new one gets a number)"), QStringLiteral("keepBoth"));
    m_conflictCombo->addItem(QStringLiteral("Skip the file"), QStringLiteral("skip"));
    selectData(m_conflictCombo, settings.value(Preferences::PasteConflict, QStringLiteral("ask")).toString());
    parts.form->addRow(QStringLiteral("When pasting or moving onto an existing name:"), m_conflictCombo);

    parts.layout->addWidget(note(parts.page, QStringLiteral("Batch Convert and Batch Rename never replace existing files, so they "
                                                            "have nothing to ask. Replacing is only done when you pick Replace "
                                                            "in the question.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createFormatsPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Formats"));
    const QSettings settings;

    auto *row = new QWidget(parts.page);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    m_ghostscriptPath = new QLineEdit(settings.value(Preferences::GhostscriptPath).toString(), row);
    m_ghostscriptPath->setPlaceholderText(QStringLiteral("Look for it automatically"));
    auto *browse = new QPushButton(QStringLiteral("Browse..."), row);
    auto *test = new QPushButton(QStringLiteral("Test"), row);
    rowLayout->addWidget(m_ghostscriptPath, 1);
    rowLayout->addWidget(browse);
    rowLayout->addWidget(test);
    parts.form->addRow(QStringLiteral("Ghostscript program:"), row);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Ghostscript program"), QString(),
                                                          QStringLiteral("Programs (*.exe);;All files (*)"));
        if (!file.isEmpty()) {
            m_ghostscriptPath->setText(QDir::toNativeSeparators(file));
        }
    });
    connect(test, &QPushButton::clicked, this, &PreferencesDialog::testGhostscript);

    m_ghostscriptResult = note(parts.page, QString());
    parts.layout->addWidget(m_ghostscriptResult);
    parts.layout->addWidget(note(parts.page, QStringLiteral("Ghostscript draws EPS files. Leave this empty to find it on the PATH or in "
                                                            "C:\\Program Files\\gs. Use gswin64c.exe, not gswin64.exe.")));
    parts.layout->addWidget(note(parts.page, QStringLiteral("EXR and DPX exposure and gamma defaults are not offered: the app has no "
                                                            "exposure or gamma control to set a default for yet.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createPerformancePage()
{
    const PageParts parts = makePage(this, QStringLiteral("Performance"));
    const QSettings settings;

    m_cacheLimit = new QSpinBox(parts.page);
    m_cacheLimit->setRange(0, 100000);
    m_cacheLimit->setSingleStep(100);
    m_cacheLimit->setSuffix(QStringLiteral(" MB"));
    m_cacheLimit->setSpecialValueText(QStringLiteral("No limit"));
    m_cacheLimit->setValue(settings.value(Preferences::ThumbnailCacheMb, 0).toInt());
    parts.form->addRow(QStringLiteral("Thumbnail cache, at most:"), m_cacheLimit);

    m_cacheSize = new QLabel(parts.page);
    auto *clear = new QPushButton(QStringLiteral("Clear cache"), parts.page);
    auto *cacheRow = new QWidget(parts.page);
    auto *cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    cacheLayout->addWidget(m_cacheSize, 1);
    cacheLayout->addWidget(clear);
    parts.form->addRow(QStringLiteral("In use now:"), cacheRow);
    connect(clear, &QPushButton::clicked, this, [this]() {
        ThumbnailCache::clear();
        updateCacheSize();
    });
    updateCacheSize();

    parts.layout->addWidget(note(parts.page, QStringLiteral("Thumbnails of folders are saved so they show quickly next time. When "
                                                            "the cache is over the limit, the oldest thumbnails are deleted when "
                                                            "the app starts and when you press OK. Cleared thumbnails are made "
                                                            "again when needed.")));
    parts.layout->addStretch(1);
    return parts.page;
}

QWidget *PreferencesDialog::createUpdatesPage()
{
    const PageParts parts = makePage(this, QStringLiteral("Updates"));
    const QSettings settings;

    m_checkUpdates = new QCheckBox(QStringLiteral("Check for a new version when the app starts"), parts.page);
    m_checkUpdates->setChecked(settings.value(Preferences::CheckUpdatesOnStartup, true).toBool());
    parts.form->addRow(QString(), m_checkUpdates);

    parts.layout->addWidget(note(parts.page, QStringLiteral("You can always check by hand from Help > Check for Updates. "
                                                            "Only the version number is fetched from GitHub; nothing is installed.")));
    parts.layout->addStretch(1);
    return parts.page;
}

void PreferencesDialog::updateCacheSize()
{
    m_cacheSize->setText(megabytesText(ThumbnailCache::sizeBytes()));
}

void PreferencesDialog::testGhostscript()
{
    QString program = m_ghostscriptPath->text().trimmed();
    if (program.isEmpty()) {
        program = Vector::ghostscriptPath();
    }
    if (program.isEmpty()) {
        m_ghostscriptResult->setText(QStringLiteral("Ghostscript was not found."));
        return;
    }
    if (!QFileInfo::exists(program)) {
        m_ghostscriptResult->setText(QStringLiteral("There is no file at %1.").arg(QDir::toNativeSeparators(program)));
        return;
    }
    QProcess process;
    process.start(program, {QStringLiteral("--version")});
    if (!process.waitForFinished(8000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        m_ghostscriptResult->setText(QStringLiteral("%1 did not answer like Ghostscript.").arg(QDir::toNativeSeparators(program)));
        return;
    }
    const QString version = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    m_ghostscriptResult->setText(QStringLiteral("Works: Ghostscript %1 (%2)").arg(version, QDir::toNativeSeparators(program)));
}

void PreferencesDialog::accept()
{
    Theme::save(static_cast<Theme::Id>(m_themeCombo->currentData().toInt()));

    QSettings settings;
    settings.setValue(Preferences::AlphaBackground, m_alphaCombo->currentData().toInt());
    settings.setValue(Preferences::OpenZoom, m_openZoomCombo->currentData().toString());
    settings.setValue(Preferences::WheelAction, m_wheelCombo->currentData().toString());
    settings.setValue(Preferences::GridView, m_gridViewCombo->currentData().toString());
    settings.setValue(Preferences::ThumbnailSize, m_thumbnailSize->value());
    settings.setValue(Preferences::ShowHidden, m_showHidden->isChecked());
    settings.setValue(Preferences::StartupFolder, m_startupCombo->currentData().toString());
    settings.setValue(Preferences::StartupPinned, m_startupPinnedCombo->currentData().toString());
    settings.setValue(Preferences::PasteConflict, m_conflictCombo->currentData().toString());
    settings.setValue(Preferences::GhostscriptPath, m_ghostscriptPath->text().trimmed());
    settings.setValue(Preferences::ThumbnailCacheMb, m_cacheLimit->value());
    settings.setValue(Preferences::CheckUpdatesOnStartup, m_checkUpdates->isChecked());
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    Theme::apply(m_originalTheme);
    QDialog::reject();
}
