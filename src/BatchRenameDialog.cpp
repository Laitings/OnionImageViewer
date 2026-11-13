#include "BatchRenameDialog.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeView>

// The old -> new table. A model rather than a QTreeWidget, so that thousands of frames refresh
// instantly on every key press.
class BatchRenamePreview final : public QAbstractTableModel
{
public:
    explicit BatchRenamePreview(QObject *parent)
        : QAbstractTableModel(parent)
    {
    }

    void setResults(const QList<BatchRename::Result> *results)
    {
        beginResetModel();
        m_results = results;
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() || !m_results ? 0 : int(m_results->size());
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 3;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return QVariant();
        }
        switch (section) {
        case 0: return QStringLiteral("Old name");
        case 1: return QStringLiteral("New name");
        default: return QStringLiteral("Status");
        }
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!m_results || !index.isValid() || index.row() >= m_results->size()) {
            return QVariant();
        }
        const BatchRename::Result &result = m_results->at(index.row());
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            switch (index.column()) {
            case 0: return result.oldName;
            case 1: return result.newName;
            default: return statusText(result);
            }
        }
        if (role == Qt::ForegroundRole) {
            if (result.status == BatchRename::Status::Problem) {
                return QColor(255, 120, 110);
            }
            if (result.status == BatchRename::Status::Unchanged) {
                return QColor(140, 140, 140);
            }
        }
        return QVariant();
    }

private:
    static QString statusText(const BatchRename::Result &result)
    {
        switch (result.status) {
        case BatchRename::Status::Problem:
            return result.problem;
        case BatchRename::Status::Unchanged:
            return QStringLiteral("Unchanged");
        case BatchRename::Status::Rename:
            break;
        }
        return result.sequence >= 0 ? QStringLiteral("Sequence %1, frame number kept").arg(result.sequence + 1) : QString();
    }

    const QList<BatchRename::Result> *m_results = nullptr;
};

namespace {

void addCaseItems(QComboBox *box, bool title)
{
    box->addItem(QStringLiteral("Keep"), int(BatchRename::CaseChange::Keep));
    box->addItem(QStringLiteral("lowercase"), int(BatchRename::CaseChange::Lower));
    box->addItem(QStringLiteral("UPPERCASE"), int(BatchRename::CaseChange::Upper));
    if (title) {
        box->addItem(QStringLiteral("Title Case"), int(BatchRename::CaseChange::Title));
    }
}

void selectData(QComboBox *box, int value)
{
    const int index = box->findData(value);
    box->setCurrentIndex(index < 0 ? 0 : index);
}

} // namespace

BatchRenameDialog::BatchRenameDialog(const QStringList &paths, const BatchRename::Options &initial, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Batch Rename"));
    m_datePool.setMaxThreadCount(1);

    QSet<QString> folders;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }
        BatchRename::Source source;
        source.path = info.absoluteFilePath();
        source.isFolder = info.isDir();
        source.modified = info.lastModified();
        m_sources.append(source);
        folders.insert(info.absolutePath());
    }
    // One listing per folder, so the preview can check new names against what is there without
    // touching the disk on every key press.
    for (const QString &folder : folders) {
        const QStringList names = QDir(folder).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &name : names) {
            m_existing.insert(BatchRename::key(folder, name));
        }
    }

    auto *form = new QFormLayout(this);
    form->addRow(new QLabel(QStringLiteral("Rename %1 selected item(s)").arg(m_sources.size()), this));

    m_pattern = new QLineEdit(this);
    m_pattern->setToolTip(QStringLiteral("The new name without the extension (the extension is always kept).\n"
                                         "{name} old name, {n} counter, {n:4} counter with 4 digits,\n"
                                         "{date} date taken (Exif, else file date), {date:yyyyMMdd} with a format,\n"
                                         "{modified} file date, {frame} frame number, {folder} folder name, {ext} extension"));
    auto *insert = new QToolButton(this);
    insert->setText(QStringLiteral("Insert"));
    insert->setPopupMode(QToolButton::InstantPopup);
    auto *tokens = new QMenu(insert);
    const QList<QPair<QString, QString>> tokenList = {
        {QStringLiteral("{name}"), QStringLiteral("Old name")},
        {QStringLiteral("{n}"), QStringLiteral("Counter")},
        {QStringLiteral("{date}"), QStringLiteral("Date taken (yyyy-MM-dd)")},
        {QStringLiteral("{date:yyyyMMdd_HHmmss}"), QStringLiteral("Date and time taken")},
        {QStringLiteral("{modified}"), QStringLiteral("File date")},
        {QStringLiteral("{frame}"), QStringLiteral("Frame number")},
        {QStringLiteral("{folder}"), QStringLiteral("Folder name")},
        {QStringLiteral("{ext}"), QStringLiteral("Extension")},
    };
    for (const auto &token : tokenList) {
        const QString text = token.first;
        tokens->addAction(QStringLiteral("%1\t%2").arg(token.second, text), this, [this, text]() {
            m_pattern->insert(text);
            m_pattern->setFocus();
        });
    }
    insert->setMenu(tokens);
    auto *patternRow = new QHBoxLayout;
    patternRow->addWidget(m_pattern, 1);
    patternRow->addWidget(insert);
    form->addRow(QStringLiteral("Name"), patternRow);

    m_counterStart = new QSpinBox(this);
    m_counterStart->setRange(0, 99999999);
    m_counterStep = new QSpinBox(this);
    m_counterStep->setRange(1, 100000);
    m_counterPadding = new QSpinBox(this);
    m_counterPadding->setRange(1, 12);
    auto *counterRow = new QHBoxLayout;
    counterRow->addWidget(new QLabel(QStringLiteral("Start"), this));
    counterRow->addWidget(m_counterStart);
    counterRow->addSpacing(12);
    counterRow->addWidget(new QLabel(QStringLiteral("Step"), this));
    counterRow->addWidget(m_counterStep);
    counterRow->addSpacing(12);
    counterRow->addWidget(new QLabel(QStringLiteral("Digits"), this));
    counterRow->addWidget(m_counterPadding);
    counterRow->addStretch(1);
    form->addRow(QStringLiteral("Counter {n}"), counterRow);

    m_remove = new QCheckBox(QStringLiteral("From"), this);
    m_remove->setToolTip(QStringLiteral("Remove the characters from one position to another (both included) in the new name.\n"
                                        "The extension and the frame number of a sequence are never touched.\n"
                                        "From the end, 1 to 1 removes the last character."));
    m_removeFrom = new QSpinBox(this);
    m_removeTo = new QSpinBox(this);
    for (QSpinBox *spin : {m_removeFrom, m_removeTo}) {
        spin->setRange(1, 255);
        spin->setEnabled(false);
        connect(m_remove, &QCheckBox::toggled, spin, &QWidget::setEnabled);
    }
    m_removeSide = new QComboBox(this);
    m_removeSide->addItem(QStringLiteral("counted from the start"), false);
    m_removeSide->addItem(QStringLiteral("counted from the end"), true);
    m_removeSide->setEnabled(false);
    connect(m_remove, &QCheckBox::toggled, m_removeSide, &QWidget::setEnabled);
    auto *removeRow = new QHBoxLayout;
    removeRow->addWidget(m_remove);
    removeRow->addWidget(m_removeFrom);
    removeRow->addWidget(new QLabel(QStringLiteral("to"), this));
    removeRow->addWidget(m_removeTo);
    removeRow->addWidget(m_removeSide);
    removeRow->addStretch(1);
    form->addRow(QStringLiteral("Remove characters"), removeRow);

    m_find = new QLineEdit(this);
    m_find->setClearButtonEnabled(true);
    m_replace = new QLineEdit(this);
    m_replace->setClearButtonEnabled(true);
    m_replace->setToolTip(QStringLiteral("With a regular expression, \\1 \\2 … insert the captured groups"));
    form->addRow(QStringLiteral("Find"), m_find);
    form->addRow(QStringLiteral("Replace with"), m_replace);
    m_regex = new QCheckBox(QStringLiteral("Regular expression"), this);
    m_matchCase = new QCheckBox(QStringLiteral("Match case"), this);
    auto *findOptions = new QHBoxLayout;
    findOptions->addWidget(m_regex);
    findOptions->addWidget(m_matchCase);
    findOptions->addStretch(1);
    form->addRow(QString(), findOptions);

    m_nameCase = new QComboBox(this);
    addCaseItems(m_nameCase, true);
    m_extensionCase = new QComboBox(this);
    addCaseItems(m_extensionCase, false);
    auto *caseRow = new QHBoxLayout;
    caseRow->addWidget(m_nameCase);
    caseRow->addSpacing(12);
    caseRow->addWidget(new QLabel(QStringLiteral("Extension"), this));
    caseRow->addWidget(m_extensionCase);
    caseRow->addStretch(1);
    form->addRow(QStringLiteral("Case"), caseRow);

    m_keepFrames = new QCheckBox(QStringLiteral("Keep frame numbers of image sequences"), this);
    m_keepFrames->setToolTip(QStringLiteral("Files like A001_C002.1001.dpx keep their frame number at the end, and each\n"
                                            "sequence counts as one item for {n}. {name} is the part before the frame number."));
    form->addRow(QString(), m_keepFrames);

    m_preview = new BatchRenamePreview(this);
    m_view = new QTreeView(this);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::NoSelection);
    m_view->setModel(m_preview);
    m_view->header()->setStretchLastSection(true);
    m_view->setMinimumHeight(220);
    form->addRow(m_view);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    form->addRow(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, this);
    m_renameButton = buttons->button(QDialogButtonBox::Ok);
    m_renameButton->setText(QStringLiteral("Rename"));
    buttons->button(QDialogButtonBox::Reset)->setToolTip(QStringLiteral("Back to the default settings"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this]() {
        BatchRename::Options defaults;
        defaults.keepFrameNumbers = m_keepFrames->isChecked();
        setOptions(defaults);
    });

    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(60);
    connect(&m_previewTimer, &QTimer::timeout, this, &BatchRenameDialog::updatePreview);
    for (QLineEdit *edit : {m_pattern, m_find, m_replace}) {
        connect(edit, &QLineEdit::textChanged, this, &BatchRenameDialog::schedulePreview);
    }
    for (QSpinBox *spin : {m_counterStart, m_counterStep, m_counterPadding, m_removeFrom, m_removeTo}) {
        connect(spin, &QSpinBox::valueChanged, this, &BatchRenameDialog::schedulePreview);
    }
    for (QCheckBox *check : {m_regex, m_matchCase, m_keepFrames, m_remove}) {
        connect(check, &QCheckBox::toggled, this, &BatchRenameDialog::schedulePreview);
    }
    for (QComboBox *box : {m_nameCase, m_extensionCase, m_removeSide}) {
        connect(box, &QComboBox::currentIndexChanged, this, &BatchRenameDialog::schedulePreview);
    }

    BatchRename::Options start = initial;
    start.keepFrameNumbers = BatchRename::looksLikeFrameSequence(paths);
    setOptions(start);
    updatePreview();
    m_view->header()->resizeSection(0, 260);
    m_view->header()->resizeSection(1, 260);
    resize(820, 640);
    m_pattern->setFocus();
    m_pattern->selectAll();
}

BatchRenameDialog::~BatchRenameDialog()
{
    m_stopReading = true;
    m_datePool.waitForDone();
}

void BatchRenameDialog::setOptions(const BatchRename::Options &values)
{
    m_pattern->setText(values.pattern);
    m_counterStart->setValue(values.counterStart);
    m_counterStep->setValue(values.counterStep);
    m_counterPadding->setValue(values.counterPadding);
    m_find->setText(values.find);
    m_replace->setText(values.replace);
    m_regex->setChecked(values.regex);
    m_matchCase->setChecked(values.matchCase);
    selectData(m_nameCase, int(values.nameCase));
    selectData(m_extensionCase, int(values.extensionCase));
    m_keepFrames->setChecked(values.keepFrameNumbers);
    m_remove->setChecked(values.removeEnabled);
    m_removeFrom->setValue(values.removeFrom);
    m_removeTo->setValue(values.removeTo);
    m_removeSide->setCurrentIndex(values.removeFromEnd ? 1 : 0);
}

BatchRename::Options BatchRenameDialog::options() const
{
    BatchRename::Options values;
    values.pattern = m_pattern->text();
    values.counterStart = m_counterStart->value();
    values.counterStep = m_counterStep->value();
    values.counterPadding = m_counterPadding->value();
    values.find = m_find->text();
    values.replace = m_replace->text();
    values.regex = m_regex->isChecked();
    values.matchCase = m_matchCase->isChecked();
    values.nameCase = static_cast<BatchRename::CaseChange>(m_nameCase->currentData().toInt());
    values.extensionCase = static_cast<BatchRename::CaseChange>(m_extensionCase->currentData().toInt());
    values.keepFrameNumbers = m_keepFrames->isChecked();
    values.removeEnabled = m_remove->isChecked();
    values.removeFrom = m_removeFrom->value();
    values.removeTo = m_removeTo->value();
    values.removeFromEnd = m_removeSide->currentIndex() == 1;
    return values;
}

QList<BatchRename::Move> BatchRenameDialog::moves() const
{
    QList<BatchRename::Move> moves;
    for (int i = 0; i < m_results.size() && i < m_sources.size(); ++i) {
        if (m_results.at(i).status == BatchRename::Status::Rename) {
            const QString folder = QFileInfo(m_sources.at(i).path).absolutePath();
            moves.append({m_sources.at(i).path, folder + QLatin1Char('/') + m_results.at(i).newName});
        }
    }
    return moves;
}

void BatchRenameDialog::schedulePreview()
{
    m_previewTimer.start();
}

void BatchRenameDialog::updatePreview()
{
    const BatchRename::Options current = options();
    const bool needsDates = BatchRename::usesDateTaken(current.pattern);
    if (needsDates) {
        startReadingDates();
    }
    m_results = BatchRename::plan(m_sources, current, m_existing);
    m_preview->setResults(&m_results);

    int renames = 0;
    int unchanged = 0;
    int problems = 0;
    int firstProblem = -1;
    for (int i = 0; i < m_results.size(); ++i) {
        switch (m_results.at(i).status) {
        case BatchRename::Status::Rename: ++renames; break;
        case BatchRename::Status::Unchanged: ++unchanged; break;
        case BatchRename::Status::Problem:
            if (firstProblem < 0) {
                firstProblem = i;
            }
            ++problems;
            break;
        }
    }
    const bool reading = needsDates && m_datesRead < m_datesToRead;
    QStringList parts;
    parts.append(QStringLiteral("%1 to rename").arg(renames));
    if (unchanged > 0) {
        parts.append(QStringLiteral("%1 unchanged").arg(unchanged));
    }
    if (problems > 0) {
        parts.append(QStringLiteral("<span style=\"color:#ff786e\">%1 with problems: fix the names in red to rename</span>").arg(problems));
    }
    QString text = parts.join(QStringLiteral(", ")) + QLatin1Char('.');
    if (reading) {
        text += QStringLiteral(" Reading dates taken… %1 of %2").arg(m_datesRead).arg(m_datesToRead);
    }
    m_summary->setText(text);
    m_renameButton->setEnabled(renames > 0 && problems == 0 && !reading);
    if (firstProblem >= 0) {
        m_view->scrollTo(m_preview->index(firstProblem, 0));
    }
}

void BatchRenameDialog::startReadingDates()
{
    if (m_datesStarted) {
        return;
    }
    m_datesStarted = true;
    QList<QPair<int, QString>> files;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (!m_sources.at(i).isFolder) {
            files.append({i, m_sources.at(i).path});
        }
    }
    m_datesToRead = int(files.size());
    m_datePool.start([this, files]() {
        QList<QPair<int, QDateTime>> batch;
        const auto deliver = [this, &batch]() {
            QMetaObject::invokeMethod(this, [this, batch]() {
                for (const auto &entry : batch) {
                    m_sources[entry.first].taken = entry.second;
                }
                m_datesRead += int(batch.size());
                schedulePreview();
            }, Qt::QueuedConnection);
            batch.clear();
        };
        for (const auto &file : files) {
            if (m_stopReading) {
                return;
            }
            batch.append({file.first, BatchRename::readDateTaken(file.second)});
            if (batch.size() == 32) {
                deliver();
            }
        }
        deliver();
    });
}
