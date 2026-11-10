#include "studyui/studyinputs.h"
#include "study/store.h"
#include "study/runs.h"
#include "modules/features/featuresmodule.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <stdexcept>

#include "fx/engine.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/features/configpane.h"
#include "modules/features/resultspane.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace skg {

namespace {
const QString kSettingsData = QStringLiteral("features/dataDir");
const QString kSettingsOut = QStringLiteral("features/outDir");
const QString kSettingsConfig = QStringLiteral("features/configPath");
const QString kSettingsCalibration = QStringLiteral("features/calibrationFile");
} // namespace

QWidget *FeaturesModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new FeaturesWidget(ctx, parent);
}

FeaturesWidget::FeaturesWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  // --- top: where the data, the output and the config are -----------------
  dataDir_ = new QLineEdit(this);
  dataBrowse_ = new QPushButton(QStringLiteral("…"), this);
  dataBrowse_->setFixedWidth(32);
  outDir_ = new QLineEdit(this);
  outBrowse_ = new QPushButton(QStringLiteral("…"), this);
  outBrowse_->setFixedWidth(32);
  configPath_ = new QLineEdit(this);
  configLoad_ = new QPushButton(this);
  configSave_ = new QPushButton(this);
  dataLabel_ = new QLabel(this);
  outLabel_ = new QLabel(this);
  configLabel_ = new QLabel(this);
  auto *top = new QFormLayout;
  auto line = [this](QWidget *a, QWidget *b, QWidget *c = nullptr) {
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(a, 1);
    l->addWidget(b);
    if (c)
      l->addWidget(c);
    return w;
  };
  top->addRow(dataLabel_, line(dataDir_, dataBrowse_));
  top->addRow(outLabel_, line(outDir_, outBrowse_));
  top->addRow(configLabel_, line(configPath_, configLoad_, configSave_));

  // --- left: the config editor in a scroll area ----------------------------
  config_ = new ConfigPane(this);
  auto *scroll = new QScrollArea(this);
  scroll->setWidget(config_);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  // --- right: run controls, status, results --------------------------------
  reuseCalibration_ = new QCheckBox(this);
  calibrationFile_ = new QLineEdit(this);
  calibrationBrowse_ = new QPushButton(QStringLiteral("…"), this);
  calibrationBrowse_->setFixedWidth(32);
  bandFilter_ = new QLineEdit(this);
  bandsLabel_ = new QLabel(this);
  run_ = new QPushButton(this);
  preprocess_ = new QPushButton(this);
  dryRun_ = new QPushButton(this);
  stop_ = new QPushButton(this);
  runNote_ = new QLabel(this);
  runNote_->setWordWrap(true);
  auto *runBox = new QGroupBox(this);
  auto *form = new QFormLayout(runBox);
  form->addRow(QString(), reuseCalibration_);
  form->addRow(QString(), line(calibrationFile_, calibrationBrowse_));
  form->addRow(bandsLabel_, bandFilter_);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(run_);
  buttons->addWidget(preprocess_);
  buttons->addWidget(dryRun_);
  buttons->addStretch(1);
  buttons->addWidget(stop_);
  form->addRow(buttons);
  form->addRow(runNote_);
  status_ = new QPlainTextEdit(this);
  status_->setReadOnly(true);
  status_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  results_ = new ResultsPane(this);
  tabs_ = new QTabWidget(this);
  tabs_->addTab(status_, QString());
  tabs_->addTab(results_, QString());
  auto *right = new QWidget(this);
  auto *rl = new QVBoxLayout(right);
  rl->setContentsMargins(0, 0, 0, 0);
  rl->addWidget(runBox);
  rl->addWidget(tabs_, 1);

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(scroll);
  split->addWidget(right);
  split->setStretchFactor(0, 2);
  split->setStretchFactor(1, 3);
  ui::persistSplitter(split, ctx_.settings, QStringLiteral("features/split"));
  studyBar_ = new StudyInputsBar(ctx_.study, ctx_.settings,
                                 QStringLiteral("features/useStudy"),
                                 StudyInputsBar::Needs::CaseAndStation,
                                 study::kToolFeatures, this);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(studyBar_);
  layout->addLayout(top);
  layout->addWidget(split, 1);

  // --- behaviour ------------------------------------------------------------
  connect(dataBrowse_, &QPushButton::clicked, this,
          [this] { chooseDir(dataDir_); });
  connect(outBrowse_, &QPushButton::clicked, this,
          [this] { chooseDir(outDir_); });
  connect(dataDir_, &QLineEdit::editingFinished, this, [this] {
    ctx_.settings->setValue(kSettingsData, dataDir_->text());
    if (outDir_->text().isEmpty() && !dataDir_->text().isEmpty()) {
      outDir_->setText(
          QDir(dataDir_->text()).filePath(QStringLiteral("results")));
      emit outDir_->editingFinished();
    }
  });
  connect(outDir_, &QLineEdit::editingFinished, this, [this] {
    ctx_.settings->setValue(kSettingsOut, outDir_->text());
    results_->setDirectory(outDir_->text());
  });
  connect(configLoad_, &QPushButton::clicked, this,
          &FeaturesWidget::loadConfig);
  connect(configSave_, &QPushButton::clicked, this,
          &FeaturesWidget::saveConfig);
  connect(calibrationBrowse_, &QPushButton::clicked, this, [this] {
    const QString start =
        outDir_->text().isEmpty() ? QDir::homePath() : outDir_->text();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a calibration"), start,
        tr("Calibrations (*_calibration.json);;All files (*)"));
    if (!path.isEmpty()) {
      calibrationFile_->setText(path);
      reuseCalibration_->setChecked(true);
      ctx_.settings->setValue(kSettingsCalibration, path);
    }
  });
  connect(reuseCalibration_, &QCheckBox::toggled, this,
          &FeaturesWidget::updateControls);
  connect(run_, &QPushButton::clicked, this, [this] { start(Mode::Run); });
  connect(preprocess_, &QPushButton::clicked, this,
          [this] { start(Mode::PreprocessOnly); });
  connect(dryRun_, &QPushButton::clicked, this,
          [this] { start(Mode::DryRun); });
  connect(stop_, &QPushButton::clicked, this, &FeaturesWidget::stop);

  retranslate();
  dataDir_->setText(ctx_.settings->value(kSettingsData).toString());
  outDir_->setText(ctx_.settings->value(kSettingsOut).toString());
  configPath_->setText(ctx_.settings->value(kSettingsConfig).toString());
  calibrationFile_->setText(
      ctx_.settings->value(kSettingsCalibration).toString());
  if (!configPath_->text().isEmpty() && QFileInfo::exists(configPath_->text()))
    config_->setConfig(fx::Config::load(configPath_->text()));
  results_->setDirectory(outDir_->text());
  connect(studyBar_, &StudyInputsBar::inputsChanged, this,
          &FeaturesWidget::applyStudyInputs);
  connect(studyBar_, &StudyInputsBar::recipeRequested, this,
          [this] { studyBar_->saveRecipe(recipeSettings()); });
  applyStudyInputs();
  updateControls();
}

void FeaturesWidget::chooseDir(QLineEdit *target) {
  const QString start =
      target->text().isEmpty() ? QDir::homePath() : target->text();
  const QString dir =
      QFileDialog::getExistingDirectory(this, tr("Choose a directory"), start);
  if (dir.isEmpty())
    return;
  target->setText(dir);
  emit target->editingFinished();
}

void FeaturesWidget::loadConfig() {
  const QString start =
      configPath_->text().isEmpty()
          ? (dataDir_->text().isEmpty() ? QDir::homePath() : dataDir_->text())
          : QFileInfo(configPath_->text()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Load a config"), start,
                                   tr("Config files (*.json);;All files (*)"));
  if (path.isEmpty())
    return;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Config"), tr("Cannot read %1.").arg(path));
    return;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) {
    QMessageBox::warning(this, tr("Config"),
                         tr("%1 is not a JSON object.").arg(path));
    return;
  }
  config_->setConfig(fx::Config::fromJson(doc.object()));
  configPath_->setText(path);
  ctx_.settings->setValue(kSettingsConfig, path);
  say(tr("Loaded %1").arg(path));
}

void FeaturesWidget::saveConfig() {
  QString path = configPath_->text();
  if (path.isEmpty()) {
    const QString start =
        outDir_->text().isEmpty() ? QDir::homePath() : outDir_->text();
    path = QFileDialog::getSaveFileName(this, tr("Save the config"),
                                        QDir(start).filePath("config.json"),
                                        tr("Config files (*.json)"));
    if (path.isEmpty())
      return;
  }
  if (!config_->config().save(path)) {
    QMessageBox::warning(this, tr("Config"), tr("Cannot write %1.").arg(path));
    return;
  }
  configPath_->setText(path);
  ctx_.settings->setValue(kSettingsConfig, path);
  say(tr("Saved %1").arg(path));
}

void FeaturesWidget::applyStudyInputs() {
  const study::Study *s = studyBar_->study();
  const study::StationRef *st = studyBar_->currentStation();
  // Held to the study: the station's folder is not edited here.
  const bool held = s && st;
  dataDir_->setReadOnly(held);
  dataBrowse_->setEnabled(!held);
  if (!s)
    return;
  fx::Config cfg = config_->config();
  // A study with no catalog of its own leaves the page's catalog alone:
  // the built-in list of large earthquakes cannot calibrate anything.
  if (!s->catalog.isEmpty())
    cfg.calibration.catalog = s->catalogPath();
  if (st) {
    cfg.station = st->code;
    cfg.calibration.stationLat = st->lat;
    cfg.calibration.stationLon = st->lon;
    const QString dir = s->dataDirFor(*st);
    if (dataDir_->text() != dir) {
      dataDir_->setText(dir);
      ctx_.settings->setValue(kSettingsData, dir);
    }
  }
  config_->setConfig(cfg);
}

QVariantMap FeaturesWidget::recipeSettings() const {
  fx::Config cfg = config_->config();
  cfg.dataDir.clear();
  cfg.outputRoot.clear();
  cfg.calibrationFile.clear();
  cfg.calibration.catalog.clear(); // a run uses the study's
  cfg.calibration.stationCoords.clear();
  cfg.calibration.stationLat.reset();
  cfg.calibration.stationLon.reset();
  return {{QStringLiteral("config_json"),
           QString::fromUtf8(
               QJsonDocument(cfg.toJson()).toJson(QJsonDocument::Indented))}};
}

fx::Config FeaturesWidget::assemble() const {
  fx::Config cfg = config_->config();
  cfg.dataDir = dataDir_->text().trimmed();
  cfg.outputRoot = outDir_->text().trimmed();
  if (reuseCalibration_->isChecked())
    cfg.calibrationFile = calibrationFile_->text().trimmed();
  // The station list embedded with the tdvms port (AFAD's) stands in when
  // none is given and the position is not set directly.
  if (cfg.calibration.stationCoords.isEmpty() &&
      !(cfg.calibration.stationLat && cfg.calibration.stationLon))
    cfg.calibration.stationCoords = QStringLiteral(":/tdvms/data/stations.csv");
  cfg.bandFilter.clear();
  for (const QString &b : bandFilter_->text().split(',', Qt::SkipEmptyParts))
    cfg.bandFilter << b.trimmed();
  return cfg;
}

void FeaturesWidget::say(const QString &line) {
  status_->appendPlainText(line);
  tabs_->setCurrentWidget(status_);
}

void FeaturesWidget::start(Mode mode) {
  if (running_)
    return;
  const fx::Config cfg = assemble();
  if (cfg.dataDir.isEmpty() || !QDir(cfg.dataDir).exists()) {
    QMessageBox::warning(this, tr("Features"),
                         tr("Choose the data directory first."));
    return;
  }
  if (cfg.outputRoot.isEmpty()) {
    QMessageBox::warning(this, tr("Features"),
                         tr("Choose the output directory first."));
    return;
  }
  QList<fx::Config> runs;
  try {
    runs = cfg.bandRuns();
  } catch (const std::invalid_argument &e) {
    QMessageBox::warning(this, tr("Features"), QString::fromUtf8(e.what()));
    return;
  }
  QStringList problems = cfg.validate();
  if (!cfg.bands.isEmpty())
    for (const fx::Config &run : runs)
      for (const QString &p : run.validate())
        problems << tr("band %1: %2").arg(run.band, p);
  if (!problems.isEmpty()) {
    QMessageBox::warning(this, tr("Invalid configuration"),
                         problems.join('\n'));
    return;
  }
  for (const QString &w : cfg.warnings())
    say(tr("Warning: %1").arg(w));
  if (mode == Mode::DryRun) {
    say(tr("Dry run: %n run(s)", nullptr, runs.size()));
    for (const fx::Config &run : runs)
      say(QStringLiteral(
              "  %1: %2-%3 Hz at %4 Hz, %5 s windows every %6 s -> %7")
              .arg(run.band.isEmpty() ? tr("(single run)") : run.band)
              .arg(run.freqmin)
              .arg(run.freqmax)
              .arg(run.fs)
              .arg(run.winSec)
              .arg(run.stepSec)
              .arg(run.outputRoot));
    return;
  }
  if (mode == Mode::Run && cfg.calibrationFile.isEmpty() &&
      cfg.calibration.catalog.isEmpty()) {
    QMessageBox::warning(
        this, tr("Features"),
        tr("A full run calibrates the chaos parameters on quiet periods, which "
           "needs the "
           "earthquake catalog (and the station list or position). Set them in "
           "the "
           "calibration group, or reuse a previous run's calibration."));
    return;
  }

  const bool preprocessOnly = mode == Mode::PreprocessOnly;
  const QString name =
      (preprocessOnly ? tr("preprocess %1") : tr("features %1"))
          .arg(cfg.station);
  const QString desc = tr("%n run(s) over %1 into %2", nullptr, runs.size())
                           .arg(cfg.dataDir, cfg.outputRoot);
  QPointer<FeaturesWidget> self(this);
  auto *job = new TaskJob(
      name, QStringLiteral("sismokaos"), desc, [=](TaskContext &ctx) -> int {
        QStringList failed;
        const int total = runs.size();
        for (int i = 0; i < total; ++i) {
          if (ctx.cancelled())
            break;
          const fx::Config &run = runs[i];
          if (!run.band.isEmpty())
            ctx.log(QStringLiteral("=== %1 (%2/%3): %4-%5 Hz at %6 Hz ===")
                        .arg(run.band)
                        .arg(i + 1)
                        .arg(total)
                        .arg(run.freqmin)
                        .arg(run.freqmax)
                        .arg(run.fs));
          fx::RunHooks hooks;
          hooks.log = [&](const QString &m) { ctx.log(m); };
          hooks.shouldStop = [&] { return ctx.cancelled(); };
          hooks.phase = [&](const QString &p) {
            ctx.log(QStringLiteral(" %1").arg(p));
            ctx.progress(0.0, p);
          };
          hooks.progress = [&](int done, int all) {
            if (all > 0)
              ctx.progress(double(done) / all,
                           QStringLiteral("%1/%2").arg(done).arg(all));
          };
          try {
            const fx::RunResult r = preprocessOnly
                                        ? fx::runPreprocessOnly(run, hooks)
                                        : fx::runPipeline(run, hooks);
            if (r.stopped)
              ctx.log(tr("stopped"));
            else if (preprocessOnly)
              ctx.log(tr("wrote %1").arg(r.rawF32));
            else if (!r.featuresNpy.isEmpty())
              ctx.log(tr("wrote %1 (%n window(s))", nullptr, r.windows)
                          .arg(r.featuresNpy));
          } catch (const std::exception &e) {
            ctx.log(tr("Pipeline error: %1").arg(QString::fromUtf8(e.what())));
            failed << (run.band.isEmpty() ? tr("run") : run.band);
          }
          QMetaObject::invokeMethod(
              self.data(),
              [self] {
                if (self)
                  self->results_->refresh();
              },
              Qt::QueuedConnection);
        }
        if (!failed.isEmpty()) {
          ctx.log(tr("Failed: %1").arg(failed.join(", ")));
          return 1;
        }
        return 0;
      });
  running_ = ctx_.jobs->add(job);
  connect(running_, &Job::lineReceived, this, &FeaturesWidget::say);
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running) {
      running_ = nullptr;
      results_->refresh();
      updateControls();
    }
  });
  updateControls();
}

void FeaturesWidget::stop() {
  if (running_)
    running_->cancel();
}

void FeaturesWidget::updateControls() {
  const bool idle = running_ == nullptr;
  run_->setEnabled(idle);
  preprocess_->setEnabled(idle);
  dryRun_->setEnabled(idle);
  stop_->setEnabled(!idle);
  calibrationFile_->setEnabled(reuseCalibration_->isChecked());
  calibrationBrowse_->setEnabled(reuseCalibration_->isChecked());
  runNote_->setText(idle ? tr("Run: calibrate on quiet periods, then extract "
                              "features per band. "
                              "Preprocess: filtered, decimated E/N/Z as "
                              "float32 only. The log goes to "
                              "the jobs panel and the Status tab.")
                         : tr("Running. Stop finishes the window in progress "
                              "and writes nothing."));
}

void FeaturesWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void FeaturesWidget::retranslate() {
  dataLabel_->setText(tr("Waveform folder"));
  outLabel_->setText(tr("Output directory"));
  configLabel_->setText(tr("Config"));
  configLoad_->setText(tr("Load…"));
  configSave_->setText(tr("Save"));
  dataDir_->setPlaceholderText(tr("miniSEED files and zips of one station"));
  outDir_->setPlaceholderText(tr("where the results go"));
  configPath_->setPlaceholderText(tr("config.json (Rust or Python layout)"));
  reuseCalibration_->setText(tr("reuse a previous run's calibration"));
  bandsLabel_->setText(tr("Bands to run"));
  bandFilter_->setPlaceholderText(tr("comma-separated names; empty = all"));
  run_->setText(tr("Run"));
  ui::setPrimary(run_);
  status_->setPlaceholderText(tr("The run log appears here."));
  preprocess_->setText(tr("Preprocess only"));
  dryRun_->setText(tr("Dry run"));
  stop_->setText(tr("Stop"));
  tabs_->setTabText(0, tr("Status"));
  tabs_->setTabText(1, tr("Results"));

  ui::setHelp({dataLabel_, dataDir_, dataBrowse_},
              tr("The folder with the miniSEED files of one station (E, N "
                 "and Z components). Zip archives in it are unpacked for "
                 "the run; other files are skipped."));
  ui::setHelp({outLabel_, outDir_, outBrowse_},
              tr("Where the feature tables and the run's records are "
                 "written. Empty: a folder named results under the data "
                 "directory."));
  ui::setHelp({configLabel_, configPath_},
              tr("A config.json holding all the settings on this page. "
                 "Load… reads one, Save writes the settings shown. Without "
                 "a file the defaults are used."));
  configLoad_->setToolTip(ui::helpText(
      tr("Reads a config.json into the settings, in the layout of the "
         "Rust tool or of the Python tool.")));
  configSave_->setToolTip(ui::helpText(
      tr("Writes the settings currently shown to the config file.")));
  ui::setHelp(reuseCalibration_,
              tr("Takes the chaos parameters from the calibration file of "
                 "an earlier run (same station, same sampling rate) "
                 "instead of calibrating again. The catalog is then not "
                 "needed."));
  ui::setHelp({calibrationFile_, calibrationBrowse_},
              tr("The <name>_calibration.json of the earlier run whose "
                 "calibration is reused."));
  ui::setHelp(bandFilter_,
              tr("With bands in the settings: run only the bands named "
                 "here, separated by commas. Empty runs all of them."));
  run_->setToolTip(ui::helpText(
      tr("Calibrates the chaos parameters on quiet periods, then extracts "
         "the features window by window, for each band.")));
  preprocess_->setToolTip(ui::helpText(
      tr("Only filters and decimates the three components and writes them "
         "as one float32 stream, for training on waveforms instead of "
         "features.")));
  dryRun_->setToolTip(ui::helpText(
      tr("Checks the settings and lists the runs that would happen, "
         "without reading the data.")));
  stop_->setToolTip(ui::helpText(
      tr("Ends the job after the window in progress. Nothing partial is "
         "written.")));
  updateControls();
}

} // namespace skg
