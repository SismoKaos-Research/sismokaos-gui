#include "modules/classification/classificationmodule.h"

#include "core/ui.h"

#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include "analysis/label.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/classification/experimentspane.h"
#include "modules/classification/settingspane.h"
#include "sfc/experiments.h"
#include "sfc/run.h"

namespace skg {

namespace {
const QString kProject = QStringLiteral("classification/project");
const QString kSource = QStringLiteral("classification/source");
} // namespace

QWidget *ClassificationModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new ClassificationWidget(ctx, parent);
}

ClassificationWidget::ClassificationWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx),
      state_(std::make_shared<ClassificationState>()) {
  project_ = new QLineEdit(this);
  projectBrowse_ = new QPushButton(QStringLiteral("…"), this);
  projectBrowse_->setFixedWidth(32);
  source_ = new QLineEdit(this);
  sourceBrowse_ = new QPushButton(QStringLiteral("…"), this);
  sourceBrowse_->setFixedWidth(32);
  projectLabel_ = new QLabel(this);
  sourceLabel_ = new QLabel(this);
  status_ = new QLabel(this);
  status_->setWordWrap(true);
  dataInfo_ = new QLabel(this);
  dataInfo_->setWordWrap(true);
  auto line = [this](QWidget *a, QWidget *b) {
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(a, 1);
    l->addWidget(b);
    return w;
  };
  auto *form = new QFormLayout;
  form->addRow(projectLabel_, line(project_, projectBrowse_));
  form->addRow(sourceLabel_, line(source_, sourceBrowse_));
  form->addRow(QString(), dataInfo_);

  settingsPane_ = new SettingsPane(state_, this);
  experimentsPane_ = new ExperimentsPane(state_, this);
  steps_ = new QListWidget(this);
  for (const QString &st : sfc::allSteps()) {
    auto *item = new QListWidgetItem(st, steps_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
  }
  steps_->setMaximumHeight(170);
  save_ = new QPushButton(this);
  run_ = new QPushButton(this);
  stop_ = new QPushButton(this);
  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  auto *runBox = new QGroupBox(this);
  runBox->setTitle(tr("Run"));
  auto *rl = new QVBoxLayout(runBox);
  rl->addWidget(steps_);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(save_);
  buttons->addStretch(1);
  buttons->addWidget(run_);
  buttons->addWidget(stop_);
  rl->addLayout(buttons);
  rl->addWidget(status_);
  auto *runTab = new QWidget(this);
  auto *rtl = new QVBoxLayout(runTab);
  rtl->setContentsMargins(0, 0, 0, 0);
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(runBox);
  split->addWidget(log_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 2);
  rtl->addWidget(split);
  tabs_ = new QTabWidget(this);
  tabs_->addTab(settingsPane_, QString());
  tabs_->addTab(runTab, QString());
  tabs_->addTab(experimentsPane_, QString());
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(tabs_, 1);

  connect(projectBrowse_, &QPushButton::clicked, this,
          &ClassificationWidget::chooseProject);
  connect(project_, &QLineEdit::editingFinished, this,
          [this] { setProject(project_->text()); });
  connect(sourceBrowse_, &QPushButton::clicked, this,
          &ClassificationWidget::chooseSource);
  connect(source_, &QLineEdit::editingFinished, this,
          &ClassificationWidget::loadSource);
  connect(settingsPane_, &SettingsPane::edited, this, [this] {
    QString err;
    if (settingsPane_->apply(&err))
      rebuildDataset();
    else
      say(err, true);
  });
  connect(save_, &QPushButton::clicked, this,
          &ClassificationWidget::saveProject);
  connect(run_, &QPushButton::clicked, this, &ClassificationWidget::startRun);
  connect(stop_, &QPushButton::clicked, this, &ClassificationWidget::stop);
  retranslate();
  const QString saved = ctx_.settings->value(kProject).toString();
  if (!saved.isEmpty()) {
    project_->setText(saved);
    setProject(saved);
  }
  source_->setText(ctx_.settings->value(kSource).toString());
  if (!source_->text().isEmpty())
    loadSource();
  updateControls();
}

void ClassificationWidget::say(const QString &message, bool warning) {
  status_->setText(message);
  status_->setStyleSheet(warning ? QStringLiteral("color: #a00;") : QString());
}

void ClassificationWidget::chooseProject() {
  const QString start =
      project_->text().isEmpty() ? QDir::homePath() : project_->text();
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Project folder (project.toml lives here)"), start);
  if (dir.isEmpty())
    return;
  project_->setText(dir);
  setProject(dir);
}

void ClassificationWidget::setProject(const QString &dir) {
  state_->projectDir = dir.trimmed();
  state_->projectFile = state_->projectDir.isEmpty()
                            ? QString()
                            : QDir(state_->projectDir).filePath("project.toml");
  if (!state_->projectDir.isEmpty())
    ctx_.settings->setValue(kProject, state_->projectDir);
  if (!state_->projectFile.isEmpty() &&
      QFileInfo::exists(state_->projectFile)) {
    try {
      state_->settings = sfc::Settings::load(state_->projectFile);
      say(tr("Loaded %1").arg(state_->projectFile));
    } catch (const std::exception &e) {
      say(tr("Cannot read %1: %2")
              .arg(state_->projectFile, QString::fromUtf8(e.what())),
          true);
    }
  } else if (!state_->projectDir.isEmpty())
    say(tr("New project: the settings are the defaults until you save "
           "project.toml."));
  // sfc's own feature table, when the project has one
  const QString table = sfc::resultsDir(state_->projectDir, state_->settings) +
                        '/' + state_->settings.station +
                        QStringLiteral("_N_features_labeled.csv");
  if (source_->text().isEmpty() && QFileInfo::exists(table)) {
    source_->setText(table);
    loadSource();
  } else
    rebuildDataset();
  settingsPane_->refresh();
  experimentsPane_->refresh();
  updateControls();
}

void ClassificationWidget::chooseSource() {
  const QString start =
      source_->text().isEmpty()
          ? (state_->projectDir.isEmpty() ? QDir::homePath()
                                          : state_->projectDir)
          : QFileInfo(source_->text()).absolutePath();
  const QString p = QFileDialog::getOpenFileName(
      this, tr("Labelled windows or a feature table"), start,
      tr("sfc feature tables (*_features_labeled.csv);;fx feature tables "
         "(*.npy *.csv);;All files (*)"));
  if (p.isEmpty())
    return;
  source_->setText(p);
  loadSource();
}

void ClassificationWidget::loadSource() {
  const QString path = source_->text().trimmed();
  state_->sourcePath = path;
  state_->table.reset();
  state_->tableColumns.clear();
  state_->dataset.reset();
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    rebuildDataset();
    return;
  }
  ctx_.settings->setValue(kSource, path);
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  try {
    state_->sourceIsSfcCsv =
        path.endsWith(QLatin1String("_features_labeled.csv"));
    if (!state_->sourceIsSfcCsv) {
      state_->table = analysis::loadTable(path);
      for (const fx::NpyField &f : state_->table->fields)
        if (!f.dtype.contains('U') && f.name != QLatin1String("Zaman_Dk"))
          state_->tableColumns << f.name;
      // Default features for an fx table: the N component's own features.
      bool anyKnown = false;
      for (const QString &f : state_->settings.featureColumns)
        anyKnown = anyKnown || state_->tableColumns.contains(f);
      if (!anyKnown) {
        QStringList defaults;
        for (const QString &c : state_->tableColumns)
          if (c.startsWith(QLatin1String("N_")) &&
              !c.endsWith(QLatin1String("_DEV")) &&
              !c.endsWith(QLatin1String("_INTERP_FRAC")))
            defaults << c;
        if (!defaults.isEmpty())
          state_->settings.featureColumns = defaults;
      }
    }
  } catch (const std::exception &e) {
    say(tr("Cannot read %1: %2").arg(path, QString::fromUtf8(e.what())), true);
  }
  QGuiApplication::restoreOverrideCursor();
  settingsPane_->refresh();
  rebuildDataset();
}

void ClassificationWidget::rebuildDataset() {
  state_->dataset.reset();
  state_->dataLog.clear();
  const sfc::Settings &s = state_->settings;
  if (state_->sourcePath.isEmpty() || !QFileInfo::exists(state_->sourcePath)) {
    dataInfo_->setText(
        tr("Choose the labelled windows: sfc's "
           "<STATION>_N_features_labeled.csv, or a feature table "
           "from the Features tab, which the class table around the event then "
           "labels."));
    updateControls();
    return;
  }
  try {
    if (state_->sourceIsSfcCsv) {
      state_->dataset = sfc::loadSfcCsv(state_->sourcePath, s);
      state_->dataLog << tr("sfc table: %n window(s)", nullptr,
                            state_->dataset->rows());
    } else if (state_->table) {
      const QMap<QDate, int> perDay = sfc::fxWindowsPerDay(*state_->table);
      int typical = 0;
      for (int n : perDay)
        typical = std::max(typical, n);
      const sfc::DaySelection sel =
          sfc::selectDays(s, perDay, int(typical * s.minDayHours / 24.0));
      state_->dataLog << sel.log;
      state_->dataset =
          sfc::fromFxTable(*state_->table, s, sel, s.featureColumns);
      state_->dataLog << tr("%n selected day(s) -> %1 windows", nullptr,
                            sel.rows.size())
                             .arg(state_->dataset->rows());
    }
  } catch (const std::exception &e) {
    dataInfo_->setText(
        tr("Cannot build the dataset: %1").arg(QString::fromUtf8(e.what())));
    updateControls();
    return;
  }
  if (state_->dataset) {
    const auto &d = *state_->dataset;
    QMap<int, QSet<QDate>> days;
    for (int r = 0; r < d.rows(); ++r)
      days[d.label[size_t(r)]].insert(d.date[size_t(r)]);
    QStringList perLabel;
    for (auto it = days.begin(); it != days.end(); ++it)
      perLabel << QStringLiteral("%1: %2").arg(it.key()).arg(it.value().size());
    dataInfo_->setText(
        tr("%n window(s), %1 features; days per label %2", nullptr, d.rows())
            .arg(d.nFeatures())
            .arg(perLabel.join(", ")));
    if (!state_->dataLog.isEmpty())
      dataInfo_->setToolTip(state_->dataLog.join('\n'));
  }
  updateControls();
}

void ClassificationWidget::saveProject() {
  QString err;
  if (!settingsPane_->apply(&err)) {
    say(err, true);
    return;
  }
  if (state_->projectFile.isEmpty()) {
    say(tr("Choose the project folder first."), true);
    return;
  }
  QDir().mkpath(state_->projectDir);
  if (state_->settings.save(state_->projectFile))
    say(tr("Saved %1").arg(state_->projectFile));
  else
    say(tr("Cannot write %1").arg(state_->projectFile), true);
  experimentsPane_->refresh();
}

void ClassificationWidget::startRun() {
  if (running_)
    return;
  QString err;
  if (!settingsPane_->apply(&err)) {
    say(err, true);
    return;
  }
  rebuildDataset();
  if (state_->projectDir.isEmpty()) {
    say(tr("Choose the project folder first."), true);
    return;
  }
  if (!state_->dataset || state_->dataset->rows() == 0) {
    say(tr("No labelled windows to train on."), true);
    return;
  }
  QStringList steps;
  for (int i = 0; i < steps_->count(); ++i)
    if (steps_->item(i)->checkState() == Qt::Checked)
      steps << steps_->item(i)->text();
  if (steps.isEmpty()) {
    say(tr("Tick at least one step."), true);
    return;
  }
  const sfc::Settings s = state_->settings;
  sfc::Dataset d = *state_->dataset;
  QStringList prepLog;
  try {
    sfc::prepareClasses(d, s, &prepLog);
  } catch (const std::exception &e) {
    say(QString::fromUtf8(e.what()), true);
    return;
  }
  if (d.rows() == 0) {
    say(tr("Every window has a NaN feature; nothing to train on."), true);
    return;
  }
  // The dataset in sfc's own layout beside the results, so the Python tool can
  // read it too.
  if (!state_->sourceIsSfcCsv) {
    const QString dir = sfc::resultsDir(state_->projectDir, s);
    QDir().mkpath(dir);
    sfc::saveSfcCsv(dir + '/' + s.station +
                        QStringLiteral("_N_features_labeled.csv"),
                    *state_->dataset, s);
  }
  log_->clear();
  for (const QString &l : state_->dataLog + prepLog)
    log_->appendPlainText(l);
  const QString projectDir = state_->projectDir;
  QPointer<ClassificationWidget> self(this);
  auto *job = new TaskJob(
      tr("sfc run %1").arg(s.runName), QStringLiteral("sfc"),
      tr("%1 on %n window(s)", nullptr, d.rows()).arg(steps.join(", ")),
      [=](TaskContext &ctx) -> int {
        sfc::RunHooks hooks;
        hooks.log = [&](const QString &m) { ctx.log(m); };
        hooks.shouldStop = [&] { return ctx.cancelled(); };
        hooks.progress = [&](int done, int total, const QString &what) {
          if (total > 0)
            ctx.progress(double(done) / total, what);
        };
        const auto outcomes = sfc::runSteps(projectDir, s, d, steps, hooks);
        bool ok = true;
        for (const sfc::StepOutcome &o : outcomes)
          ok = ok && o.status == QLatin1String("OK");
        return ok ? 0 : 1;
      });
  running_ = ctx_.jobs->add(job);
  connect(running_, &Job::lineReceived, this,
          [this](const QString &l) { log_->appendPlainText(l); });
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running) {
      running_ = nullptr;
      experimentsPane_->refresh();
      updateControls();
    }
  });
  tabs_->setCurrentIndex(1);
  updateControls();
}

void ClassificationWidget::stop() {
  if (running_)
    running_->cancel();
}

void ClassificationWidget::updateControls() {
  const bool idle = running_ == nullptr;
  run_->setEnabled(idle && state_->dataset && !state_->projectDir.isEmpty());
  stop_->setEnabled(!idle);
  save_->setEnabled(!state_->projectDir.isEmpty());
}

void ClassificationWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void ClassificationWidget::retranslate() {
  projectLabel_->setText(tr("Project folder"));
  sourceLabel_->setText(tr("Labelled windows"));
  project_->setPlaceholderText(
      tr("the folder with project.toml; results/ is written under it"));
  source_->setPlaceholderText(tr("<STATION>_N_features_labeled.csv from sfc, "
                                 "or an fx feature table (.npy/.csv)"));
  save_->setText(tr("Save project.toml"));
  run_->setText(tr("Run"));
  ui::setPrimary(run_);
  log_->setPlaceholderText(tr("The run log appears here."));
  stop_->setText(tr("Stop"));
  tabs_->setTabText(0, tr("Settings"));
  tabs_->setTabText(1, tr("Run"));
  tabs_->setTabText(2, tr("Experiments"));
}

} // namespace skg
