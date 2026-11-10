#include "modules/earlywarning/earlywarningmodule.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>

#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "tools/toolregistry.h"

namespace skg {

namespace {
const QString kTool = QStringLiteral("ayzek");
const QString kKey = QStringLiteral("earlywarning/");

// A spin box whose minimum reads "default" and means "not given".
void defaultAtMinimum(QDoubleSpinBox *s, double min, double max, double step,
                      int decimals) {
  s->setDecimals(decimals);
  s->setRange(min, max);
  s->setSingleStep(step);
  s->setValue(min);
}
double orDefault(const QDoubleSpinBox *s) {
  return s->value() <= s->minimum() ? -1 : s->value();
}
} // namespace

// ---- module

QWidget *EarlyWarningModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new EarlyWarningWidget(ctx, parent);
}

// ---- events model

AyzekEventsModel::AyzekEventsModel(QObject *parent)
    : QAbstractTableModel(parent) {}

void AyzekEventsModel::clear() {
  beginResetModel();
  rows_.clear();
  endResetModel();
}

void AyzekEventsModel::append(const ayzek::Event &e) {
  const int n = int(rows_.size());
  beginInsertRows({}, n, n);
  rows_.push_back(e);
  endInsertRows();
}

int AyzekEventsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : int(rows_.size());
}
int AyzekEventsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant AyzekEventsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole)
    return {};
  const ayzek::Event &e = rows_[size_t(index.row())];
  switch (index.column()) {
  case Tag:
    return e.tag;
  case Alarm:
    return e.id < 0 ? QString() : QString::number(e.id);
  case Time:
    return e.time;
  case Text:
    return e.text;
  }
  return {};
}

QVariant AyzekEventsModel::headerData(int section, Qt::Orientation o,
                                      int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case Tag:
    return tr("Kind");
  case Alarm:
    return tr("Alarm");
  case Time:
    return tr("Time (UTC)");
  case Text:
    return tr("Message");
  }
  return {};
}

// ---- widget

EarlyWarningWidget::EarlyWarningWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  auto *root = new QVBoxLayout(this);

  // Installation row.
  auto *installRow = new QHBoxLayout;
  installLabel_ = new QLabel(this);
  install_ = new QLabel(this);
  install_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  install_->setWordWrap(true);
  installBrowse_ = new QPushButton(this);
  installReset_ = new QPushButton(this);
  installRow->addWidget(installLabel_);
  installRow->addWidget(install_, 1);
  installRow->addWidget(installBrowse_);
  installRow->addWidget(installReset_);
  root->addLayout(installRow);

  tabs_ = new QTabWidget(this);
  root->addWidget(tabs_, 1);

  // Run tab.
  auto *runPage = new QWidget(tabs_);
  auto *runLayout = new QVBoxLayout(runPage);
  auto *modeRow = new QHBoxLayout;
  modeLabel_ = new QLabel(runPage);
  mode_ = new QComboBox(runPage);
  mode_->addItem(QString(), int(ayzek::Mode::Replay));
  mode_->addItem(QString(), int(ayzek::Mode::Live));
  modeRow->addWidget(modeLabel_);
  modeRow->addWidget(mode_);
  modeRow->addStretch(1);
  runLayout->addLayout(modeRow);

  auto *columns = new QHBoxLayout;
  runLayout->addLayout(columns, 1);

  replayBox_ = new QGroupBox(runPage);
  auto *replayForm = new QFormLayout(replayBox_);
  files_ = new QListWidget(replayBox_);
  files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  auto *fileButtons = new QHBoxLayout;
  addFiles_ = new QPushButton(replayBox_);
  removeFiles_ = new QPushButton(replayBox_);
  fileButtons->addWidget(addFiles_);
  fileButtons->addWidget(removeFiles_);
  fileButtons->addStretch(1);
  replayForm->addRow(files_);
  replayForm->addRow(fileButtons);
  speedLabel_ = new QLabel(replayBox_);
  speed_ = new QDoubleSpinBox(replayBox_);
  speed_->setDecimals(1);
  speed_->setRange(0, 10000);
  speed_->setValue(1);
  replayForm->addRow(speedLabel_, speed_);
  fromLabel_ = new QLabel(replayBox_);
  from_ = new QLineEdit(replayBox_);
  from_->setPlaceholderText(QStringLiteral("2025-11-10T18:20:00"));
  replayForm->addRow(fromLabel_, from_);
  catalogLabel_ = new QLabel(replayBox_);
  auto *catalogRow = new QHBoxLayout;
  catalog_ = new QLineEdit(replayBox_);
  catalogBrowse_ = new QPushButton(replayBox_);
  catalogRow->addWidget(catalog_, 1);
  catalogRow->addWidget(catalogBrowse_);
  replayForm->addRow(catalogLabel_, catalogRow);
  columns->addWidget(replayBox_, 1);

  liveBox_ = new QGroupBox(runPage);
  auto *liveForm = new QFormLayout(liveBox_);
  serverLabel_ = new QLabel(liveBox_);
  server_ = new QLineEdit(liveBox_);
  server_->setPlaceholderText(QStringLiteral("geofon.gfz.de:18000"));
  liveForm->addRow(serverLabel_, server_);
  streamsLabel_ = new QLabel(liveBox_);
  streams_ = new QLineEdit(liveBox_);
  streams_->setPlaceholderText(QStringLiteral("KO.KULA,KO.SIMA"));
  liveForm->addRow(streamsLabel_, streams_);
  archiveLabel_ = new QLabel(liveBox_);
  auto *archiveRow = new QHBoxLayout;
  archive_ = new QLineEdit(liveBox_);
  archiveBrowse_ = new QPushButton(liveBox_);
  archiveRow->addWidget(archive_, 1);
  archiveRow->addWidget(archiveBrowse_);
  liveForm->addRow(archiveLabel_, archiveRow);
  durationLabel_ = new QLabel(liveBox_);
  duration_ = new QSpinBox(liveBox_);
  duration_->setRange(0, 7 * 24 * 3600);
  duration_->setSingleStep(60);
  liveForm->addRow(durationLabel_, duration_);
  columns->addWidget(liveBox_, 1);

  commonBox_ = new QGroupBox(runPage);
  auto *commonForm = new QFormLayout(commonBox_);
  detectorLabel_ = new QLabel(commonBox_);
  detector_ = new QComboBox(commonBox_);
  for (int i = 0; i < 4; ++i)
    detector_->addItem(QString(), i);
  commonForm->addRow(detectorLabel_, detector_);
  minStationsLabel_ = new QLabel(commonBox_);
  minStations_ = new QSpinBox(commonBox_);
  minStations_->setRange(0, 20);
  commonForm->addRow(minStationsLabel_, minStations_);
  thresholdLabel_ = new QLabel(commonBox_);
  threshold_ = new QDoubleSpinBox(commonBox_);
  defaultAtMinimum(threshold_, 0.0, 1.0, 0.05, 2);
  commonForm->addRow(thresholdLabel_, threshold_);
  triggerWindowsLabel_ = new QLabel(commonBox_);
  triggerWindows_ = new QSpinBox(commonBox_);
  triggerWindows_->setRange(0, 100);
  commonForm->addRow(triggerWindowsLabel_, triggerWindows_);
  instantLabel_ = new QLabel(commonBox_);
  instant_ = new QDoubleSpinBox(commonBox_);
  defaultAtMinimum(instant_, 0.0, 2.0, 0.05, 2);
  commonForm->addRow(instantLabel_, instant_);
  releaseLabel_ = new QLabel(commonBox_);
  release_ = new QDoubleSpinBox(commonBox_);
  defaultAtMinimum(release_, 0.0, 1.0, 0.05, 2);
  commonForm->addRow(releaseLabel_, release_);
  locateLabel_ = new QLabel(commonBox_);
  locate_ = new QComboBox(commonBox_);
  for (int i = 0; i < 3; ++i)
    locate_->addItem(QString(), i);
  commonForm->addRow(locateLabel_, locate_);
  assess_ = new QCheckBox(commonBox_);
  scores_ = new QCheckBox(commonBox_);
  verbose_ = new QCheckBox(commonBox_);
  noMagnitude_ = new QCheckBox(commonBox_);
  commonForm->addRow(assess_);
  commonForm->addRow(scores_);
  commonForm->addRow(noMagnitude_);
  commonForm->addRow(verbose_);
  outputLabel_ = new QLabel(commonBox_);
  auto *outputRow = new QHBoxLayout;
  output_ = new QLineEdit(commonBox_);
  outputBrowse_ = new QPushButton(commonBox_);
  outputRow->addWidget(output_, 1);
  outputRow->addWidget(outputBrowse_);
  commonForm->addRow(outputLabel_, outputRow);
  sitesLabel_ = new QLabel(commonBox_);
  sites_ = new QPlainTextEdit(commonBox_);
  sites_->setPlaceholderText(QStringLiteral("Istanbul,41.0082,28.9784"));
  sites_->setMaximumHeight(70);
  commonForm->addRow(sitesLabel_, sites_);
  stationsLabel_ = new QLabel(commonBox_);
  auto *stationsRow = new QHBoxLayout;
  stations_ = new QLineEdit(commonBox_);
  stationsBrowse_ = new QPushButton(commonBox_);
  stationsRow->addWidget(stations_, 1);
  stationsRow->addWidget(stationsBrowse_);
  commonForm->addRow(stationsLabel_, stationsRow);
  modelsLabel_ = new QLabel(commonBox_);
  auto *modelsRow = new QHBoxLayout;
  models_ = new QLineEdit(commonBox_);
  modelsBrowse_ = new QPushButton(commonBox_);
  modelsRow->addWidget(models_, 1);
  modelsRow->addWidget(modelsBrowse_);
  commonForm->addRow(modelsLabel_, modelsRow);
  extraLabel_ = new QLabel(commonBox_);
  extra_ = new QLineEdit(commonBox_);
  extra_->setPlaceholderText(QStringLiteral("--slack 4 --step 25"));
  commonForm->addRow(extraLabel_, extra_);
  columns->addWidget(commonBox_, 1);

  auto *buttons = new QHBoxLayout;
  run_ = new QPushButton(runPage);
  stop_ = new QPushButton(runPage);
  status_ = new QLabel(runPage);
  status_->setWordWrap(true);
  buttons->addWidget(run_);
  buttons->addWidget(stop_);
  buttons->addWidget(status_, 1);
  runLayout->addLayout(buttons);
  tabs_->addTab(runPage, QString());

  // Results tab.
  auto *resultsPage = new QWidget(tabs_);
  auto *resultsLayout = new QVBoxLayout(resultsPage);
  auto *split = new QSplitter(Qt::Vertical, resultsPage);
  events_ = new AyzekEventsModel(this);
  eventsView_ = new QTableView(split);
  eventsView_->setModel(events_);
  eventsView_->horizontalHeader()->setStretchLastSection(true);
  eventsView_->verticalHeader()->setVisible(false);
  eventsView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  eventsView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  split->addWidget(eventsView_);
  log_ = new QPlainTextEdit(split);
  log_->setReadOnly(true);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  log_->setMaximumBlockCount(50000);
  split->addWidget(log_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 2);
  resultsLayout->addWidget(split, 1);
  auto *summaryRow = new QHBoxLayout;
  summary_ = new QLabel(resultsPage);
  summary_->setWordWrap(true);
  openOutput_ = new QPushButton(resultsPage);
  summaryRow->addWidget(summary_, 1);
  summaryRow->addWidget(openOutput_);
  resultsLayout->addLayout(summaryRow);
  tabs_->addTab(resultsPage, QString());

  licence_ = new QLabel(this);
  licence_->setWordWrap(true);
  licence_->setOpenExternalLinks(true);
  licence_->setTextFormat(Qt::RichText);
  root->addWidget(licence_);

  connect(installBrowse_, &QPushButton::clicked, this,
          &EarlyWarningWidget::chooseExecutable);
  connect(installReset_, &QPushButton::clicked, this, [this] {
    ctx_.tools->setExecutable(kTool, QString());
    refreshInstall();
  });
  connect(ctx_.tools, &ToolRegistry::changed, this, [this](const QString &id) {
    if (id == kTool)
      refreshInstall();
  });
  connect(mode_, &QComboBox::currentIndexChanged, this,
          &EarlyWarningWidget::modeChanged);
  connect(addFiles_, &QPushButton::clicked, this,
          &EarlyWarningWidget::addFiles);
  connect(removeFiles_, &QPushButton::clicked, this,
          &EarlyWarningWidget::removeFiles);
  connect(catalogBrowse_, &QPushButton::clicked, this, [this] {
    browseInto(catalog_, false, tr("CSV files (*.csv);;All files (*)"));
  });
  connect(archiveBrowse_, &QPushButton::clicked, this,
          [this] { browseInto(archive_, true, {}); });
  connect(outputBrowse_, &QPushButton::clicked, this,
          [this] { browseInto(output_, true, {}); });
  connect(stationsBrowse_, &QPushButton::clicked, this, [this] {
    browseInto(stations_, false, tr("CSV files (*.csv);;All files (*)"));
  });
  connect(modelsBrowse_, &QPushButton::clicked, this,
          [this] { browseInto(models_, true, {}); });
  connect(run_, &QPushButton::clicked, this, &EarlyWarningWidget::run);
  connect(stop_, &QPushButton::clicked, this, &EarlyWarningWidget::stop);
  connect(openOutput_, &QPushButton::clicked, this,
          &EarlyWarningWidget::openOutputDir);

  loadSettings();
  retranslate();
  refreshInstall();
  modeChanged();
  updateControls();
}

void EarlyWarningWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refreshInstall();
  }
  QWidget::changeEvent(e);
}

void EarlyWarningWidget::retranslate() {
  installLabel_->setText(tr("ayzek:"));
  installBrowse_->setText(tr("Choose executable..."));
  installReset_->setText(tr("Use the shipped copy"));
  tabs_->setTabText(0, tr("Run"));
  tabs_->setTabText(1, tr("Results"));
  modeLabel_->setText(tr("Input"));
  mode_->setItemText(0, tr("Replay miniSEED files"));
  mode_->setItemText(1, tr("Live SeedLink stream"));
  replayBox_->setTitle(tr("Replay"));
  addFiles_->setText(tr("Add files..."));
  removeFiles_->setText(tr("Remove"));
  ui::setHelp(files_, tr("One miniSEED file per station, HH? channels; the "
                        "station code is read from the records and its "
                        "position from the station table."));
  speedLabel_->setText(tr("Speed"));
  ui::setHelp(speed_, tr("1 = real time, 10 = ten times faster, 0 = as fast "
                        "as the machine allows"));
  speed_->setSpecialValueText(tr("as fast as possible"));
  fromLabel_->setText(tr("Detect from (UTC)"));
  ui::setHelp(from_, tr("No triggers before this time; earlier data still "
                       "warms up the filters. Empty: from the start."));
  catalogLabel_->setText(tr("AFAD catalog"));
  ui::setHelp(catalog_, tr("An AFAD catalogue export (CSV). Each alarm is "
                          "compared with it; without one, alarms are "
                          "reported but not scored."));
  catalogBrowse_->setText(tr("Browse..."));
  liveBox_->setTitle(tr("Live"));
  serverLabel_->setText(tr("SeedLink server"));
  streamsLabel_->setText(tr("Streams"));
  ui::setHelp(streams_, tr("NET.STA or NET.STA.LOC, comma-separated; every "
                          "station needs coordinates in the station table"));
  archiveLabel_->setText(tr("Archive to"));
  ui::setHelp(archive_, tr("Every record received is appended to "
                          "DIR/STATION.mseed, which replays like any file"));
  archiveBrowse_->setText(tr("Browse..."));
  durationLabel_->setText(tr("Duration (s)"));
  duration_->setSpecialValueText(tr("until stopped"));
  commonBox_->setTitle(tr("Detection"));
  detectorLabel_->setText(tr("Detector"));
  detector_->setItemText(0, tr("6 s window detector (3-seed ensemble)"));
  detector_->setItemText(1, tr("Streaming onset transformer"));
  detector_->setItemText(2, tr("STA/LTA (reference)"));
  detector_->setItemText(3, tr("FilterPicker (reference)"));
  minStationsLabel_->setText(tr("Stations to declare"));
  minStations_->setSpecialValueText(tr("default"));
  ui::setHelp(minStations_, tr("Station detections needed to declare an "
                              "event; ayzek's default is 2 (3 for the "
                              "transformer)"));
  thresholdLabel_->setText(tr("Trigger threshold"));
  threshold_->setSpecialValueText(tr("default"));
  triggerWindowsLabel_->setText(tr("Trigger windows"));
  triggerWindows_->setSpecialValueText(tr("default"));
  instantLabel_->setText(tr("Instant threshold"));
  instant_->setSpecialValueText(tr("default"));
  releaseLabel_->setText(tr("Release"));
  release_->setSpecialValueText(tr("default"));
  locateLabel_->setText(tr("Location"));
  locate_->setItemText(0, tr("default"));
  locate_->setItemText(1, tr("geometry head of the transformer"));
  locate_->setItemText(2, tr("P and S picks"));
  assess_->setText(tr("Assess every alarm from its S-P picks (writes "
                      "assess.csv in the output folder)"));
  scores_->setText(tr("Write every window's detector probability "
                      "(scores/ in the output folder)"));
  noMagnitude_->setText(tr("No magnitude estimate"));
  verbose_->setText(tr("Verbose: every station detection, pick and Pd"));
  outputLabel_->setText(tr("Output folder"));
  ui::setHelp(output_, tr("Working directory of the run; the assessment "
                         "table and the scores go here"));
  outputBrowse_->setText(tr("Browse..."));
  sitesLabel_->setText(tr("Warning sites"));
  ui::setHelp(sites_, tr("One place per line as NAME,LAT,LON; the S-wave "
                        "warning time at each is reported"));
  stationsLabel_->setText(tr("Station table"));
  ui::setHelp(stations_, tr("code,lat,lon[,...] in place of the shipped "
                           "table (TU and KO stations), for another "
                           "network"));
  stationsBrowse_->setText(tr("Browse..."));
  modelsLabel_->setText(tr("Models folder"));
  ui::setHelp(models_, tr("Weights directory; empty uses the shipped models"));
  modelsBrowse_->setText(tr("Browse..."));
  extraLabel_->setText(tr("Extra options"));
  ui::setHelp(extra_, tr("Any other ayzek option, as on the command line"));
  run_->setText(tr("Run"));
  ui::setPrimary(run_);
  log_->setPlaceholderText(tr("The run log appears here."));
  stop_->setText(tr("Stop"));
  openOutput_->setText(tr("Open output folder"));
  licence_->setText(
      tr("ayzek is a separate program, GPL-3.0-or-later, by H. Oğuz Bolat, "
         "Sibel Çimen, Bülent Bolat and Tülay Yıldırım (Yıldız Technical "
         "University), shipped beside this application with its source "
         "(third_party/ayzek) and models. Please cite it: "
         "<a href=\"https://doi.org/10.5281/zenodo.23672495\">"
         "doi:10.5281/zenodo.23672495</a>; "
         "<a href=\"https://github.com/hogib/ayzek\">github.com/hogib/"
         "ayzek</a>."));
  if (!running_ && summary_->text().isEmpty())
    summary_->setText(tr("No run yet."));

  installBrowse_->setToolTip(ui::helpText(
      tr("Runs another copy of ayzek instead of the one shipped with the "
         "application.")));
  installReset_->setToolTip(ui::helpText(
      tr("Forgets the chosen executable and goes back to the shipped "
         "copy.")));
  ui::setHelp({modeLabel_, mode_},
              tr("Where the waveforms come from: recorded miniSEED files "
                 "played back, or a live stream from a SeedLink server "
                 "(not available in the Windows build of ayzek)."));
  addFiles_->setToolTip(ui::helpText(
      tr("Adds miniSEED files to replay, one per station.")));
  removeFiles_->setToolTip(
      ui::helpText(tr("Takes the selected files off the list.")));
  ui::setHelp(server_, tr("The SeedLink server to read from, as "
                          "HOST:PORT."));
  ui::setHelp(duration_, tr("Stops the live input after this many seconds. "
                            "0 runs until you press Stop."));
  ui::setHelp(detector_,
              tr("What decides at each station that an earthquake has "
                 "started: the 6 s window detector (ayzek's default), the "
                 "streaming onset transformer, or the classic STA/LTA and "
                 "FilterPicker methods as references."));
  ui::setHelp(threshold_,
              tr("A station triggers when the detector's probability "
                 "reaches this value in a run of consecutive windows (see "
                 "Trigger windows). ayzek's default is 0.8. Lower triggers "
                 "sooner and more often falsely."));
  ui::setHelp(triggerWindows_,
              tr("How many consecutive windows must reach the trigger "
                 "threshold. ayzek's default is 8, which adds 3.5 s to a "
                 "trigger; fewer is faster and less certain."));
  ui::setHelp(instant_,
              tr("A station also triggers on a single window at or above "
                 "this probability. ayzek's default is 0.9; a value above "
                 "1 turns this off."));
  ui::setHelp(release_,
              tr("After a trigger, the station can trigger again once two "
                 "windows have fallen below this probability. ayzek's "
                 "default is 0.3."));
  ui::setHelp(locate_,
              tr("How the event is located: from the transformer's "
                 "geometry head as soon as it triggers, or from P and S "
                 "picks. \"default\" leaves the choice to ayzek."));
  ui::setHelp(assess_,
              tr("Judges every alarm from the S-P times picked at its "
                 "stations and writes the verdicts to assess.csv in the "
                 "output folder."));
  ui::setHelp(scores_,
              tr("Writes the detector's probability for every window of "
                 "every station into scores/ in the output folder, for "
                 "looking at afterwards."));
  ui::setHelp(verbose_, tr("Prints every station detection, pick and peak "
                           "displacement (Pd) value, not only the "
                           "alarms."));
  ui::setHelp(noMagnitude_, tr("Skips the magnitude estimate from the "
                               "peak P displacement."));
  run_->setToolTip(ui::helpText(
      tr("Starts ayzek with these settings as a job. Its output appears "
         "on the Results tab.")));
  stop_->setToolTip(ui::helpText(tr("Stops ayzek.")));
  openOutput_->setToolTip(ui::helpText(
      tr("Opens the run's working directory in the file manager.")));
}

void EarlyWarningWidget::refreshInstall() {
  const ToolStatus st = ctx_.tools->status(kTool);
  modelsDirInUse_.clear();
  if (!st.found()) {
    install_->setText(
        QStringLiteral("<b>%1</b> - %2")
            .arg(sourceName(st.source),
                 tr("ayzek was not found beside the application or on the "
                    "PATH; choose its executable")));
    installReset_->setEnabled(!ctx_.tools->executable(kTool).isEmpty());
    updateControls();
    return;
  }
  const auto in = ayzek::installAt(st.program);
  QString text = QStringLiteral("<b>%1</b>: %2")
                     .arg(sourceName(st.source), st.program.toHtmlEscaped());
  if (in && in->hasModels()) {
    modelsDirInUse_ = in->modelsDir;
    text += QStringLiteral("<br>") +
            tr("models: %1").arg(in->modelsDir.toHtmlEscaped());
  } else {
    text += QStringLiteral("<br>") +
            tr("no models/ beside it; set the models folder below");
  }
  install_->setText(text);
  installReset_->setEnabled(st.source == ToolStatus::Source::Explicit);
  updateControls();
}

void EarlyWarningWidget::chooseExecutable() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("ayzek executable"), QDir::homePath());
  if (path.isEmpty())
    return;
  ctx_.tools->setExecutable(kTool, path);
  refreshInstall();
}

void EarlyWarningWidget::addFiles() {
  const QString start = ctx_.settings->value(kKey + "filesDir").toString();
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("miniSEED files, one per station"),
      start.isEmpty() ? QDir::homePath() : start,
      tr("miniSEED (*.mseed *.msd *.seed);;All files (*)"));
  if (paths.isEmpty())
    return;
  ctx_.settings->setValue(kKey + "filesDir", QFileInfo(paths.first()).path());
  for (const QString &p : paths)
    if (files_->findItems(p, Qt::MatchExactly).isEmpty())
      files_->addItem(p);
  updateControls();
}

void EarlyWarningWidget::removeFiles() {
  for (QListWidgetItem *it : files_->selectedItems())
    delete it;
  updateControls();
}

void EarlyWarningWidget::browseInto(QLineEdit *field, bool directory,
                                    const QString &filter) {
  const QString start =
      field->text().isEmpty() ? QDir::homePath() : field->text();
  const QString path =
      directory ? QFileDialog::getExistingDirectory(this, QString(), start)
                : QFileDialog::getOpenFileName(this, QString(), start, filter);
  if (!path.isEmpty())
    field->setText(path);
}

ayzek::Options EarlyWarningWidget::options() const {
  ayzek::Options o;
  o.mode = ayzek::Mode(mode_->currentData().toInt());
  for (int i = 0; i < files_->count(); ++i)
    o.files << files_->item(i)->text();
  o.speed = speed_->value();
  o.from = from_->text().trimmed();
  o.catalog = catalog_->text().trimmed();
  o.server = server_->text().trimmed();
  o.streams = streams_->text().trimmed();
  o.archiveDir = archive_->text().trimmed();
  o.durationS = duration_->value();
  o.modelsDir = models_->text().trimmed();
  if (o.modelsDir.isEmpty())
    o.modelsDir = modelsDirInUse_;
  o.stations = stations_->text().trimmed();
  o.detector = ayzek::Detector(detector_->currentData().toInt());
  o.minStations = minStations_->value();
  o.threshold = orDefault(threshold_);
  o.triggerWindows = triggerWindows_->value();
  o.instantThreshold = orDefault(instant_);
  o.release = orDefault(release_);
  o.locate = ayzek::Locate(locate_->currentData().toInt());
  const QString out = output_->text().trimmed();
  o.assess = assess_->isChecked();
  if (o.assess && !out.isEmpty())
    o.assessCsv = out + QStringLiteral("/assess.csv");
  if (scores_->isChecked())
    o.scoresDir =
        (out.isEmpty() ? QStringLiteral(".") : out) + QStringLiteral("/scores");
  o.noMagnitude = noMagnitude_->isChecked();
  o.verbose = verbose_->isChecked();
  o.sites = ayzek::parseSites(sites_->toPlainText());
  const QString extra = extra_->text().trimmed();
  if (!extra.isEmpty())
    o.extra = extra.split(QRegularExpression(QStringLiteral("\\s+")),
                          Qt::SkipEmptyParts);
  return o;
}

void EarlyWarningWidget::loadSettings() {
  QSettings *s = ctx_.settings;
  mode_->setCurrentIndex(s->value(kKey + "mode", 0).toInt());
  files_->addItems(s->value(kKey + "files").toStringList());
  speed_->setValue(s->value(kKey + "speed", 1.0).toDouble());
  from_->setText(s->value(kKey + "from").toString());
  catalog_->setText(s->value(kKey + "catalog").toString());
  server_->setText(s->value(kKey + "server").toString());
  streams_->setText(s->value(kKey + "streams").toString());
  archive_->setText(s->value(kKey + "archive").toString());
  duration_->setValue(s->value(kKey + "duration", 0).toInt());
  models_->setText(s->value(kKey + "models").toString());
  stations_->setText(s->value(kKey + "stations").toString());
  detector_->setCurrentIndex(s->value(kKey + "detector", 0).toInt());
  minStations_->setValue(s->value(kKey + "minStations", 0).toInt());
  threshold_->setValue(s->value(kKey + "threshold", 0.0).toDouble());
  triggerWindows_->setValue(s->value(kKey + "triggerWindows", 0).toInt());
  instant_->setValue(s->value(kKey + "instant", 0.0).toDouble());
  release_->setValue(s->value(kKey + "release", 0.0).toDouble());
  locate_->setCurrentIndex(s->value(kKey + "locate", 0).toInt());
  assess_->setChecked(s->value(kKey + "assess", false).toBool());
  scores_->setChecked(s->value(kKey + "scores", false).toBool());
  noMagnitude_->setChecked(s->value(kKey + "noMagnitude", false).toBool());
  verbose_->setChecked(s->value(kKey + "verbose", false).toBool());
  output_->setText(s->value(kKey + "output").toString());
  sites_->setPlainText(s->value(kKey + "sites").toString());
  extra_->setText(s->value(kKey + "extra").toString());
}

void EarlyWarningWidget::saveSettings() const {
  QSettings *s = ctx_.settings;
  QStringList files;
  for (int i = 0; i < files_->count(); ++i)
    files << files_->item(i)->text();
  s->setValue(kKey + "mode", mode_->currentIndex());
  s->setValue(kKey + "files", files);
  s->setValue(kKey + "speed", speed_->value());
  s->setValue(kKey + "from", from_->text());
  s->setValue(kKey + "catalog", catalog_->text());
  s->setValue(kKey + "server", server_->text());
  s->setValue(kKey + "streams", streams_->text());
  s->setValue(kKey + "archive", archive_->text());
  s->setValue(kKey + "duration", duration_->value());
  s->setValue(kKey + "models", models_->text());
  s->setValue(kKey + "stations", stations_->text());
  s->setValue(kKey + "detector", detector_->currentIndex());
  s->setValue(kKey + "minStations", minStations_->value());
  s->setValue(kKey + "threshold", threshold_->value());
  s->setValue(kKey + "triggerWindows", triggerWindows_->value());
  s->setValue(kKey + "instant", instant_->value());
  s->setValue(kKey + "release", release_->value());
  s->setValue(kKey + "locate", locate_->currentIndex());
  s->setValue(kKey + "assess", assess_->isChecked());
  s->setValue(kKey + "scores", scores_->isChecked());
  s->setValue(kKey + "noMagnitude", noMagnitude_->isChecked());
  s->setValue(kKey + "verbose", verbose_->isChecked());
  s->setValue(kKey + "output", output_->text());
  s->setValue(kKey + "sites", sites_->toPlainText());
  s->setValue(kKey + "extra", extra_->text());
}

void EarlyWarningWidget::modeChanged() {
  const bool live = mode_->currentData().toInt() == int(ayzek::Mode::Live);
  replayBox_->setVisible(!live);
  liveBox_->setVisible(live);
  updateControls();
}

void EarlyWarningWidget::run() {
  if (running_)
    return;
  const ToolStatus st = ctx_.tools->status(kTool);
  if (!st.found()) {
    say(tr("ayzek was not found; choose its executable above."), true);
    return;
  }
  ayzek::Options o = options();
  if (!o.from.isEmpty() &&
      !QDateTime::fromString(o.from, Qt::ISODate).isValid()) {
    say(tr("The start time must be UTC like 2025-11-10T18:20:00."), true);
    return;
  }
  if (const QString err = ayzek::validate(o); !err.isEmpty()) {
    say(err, true);
    return;
  }
  QString workDir = output_->text().trimmed();
  if (!workDir.isEmpty() && !QDir().mkpath(workDir)) {
    say(tr("Cannot create the output folder %1").arg(workDir), true);
    return;
  }
  if (workDir.isEmpty())
    workDir = QDir::homePath();
  if (!o.scoresDir.isEmpty())
    QDir(workDir).mkpath(o.scoresDir);
  saveSettings();
  const QString name =
      o.mode == ayzek::Mode::Live
          ? tr("ayzek live %1").arg(o.streams)
          : tr("ayzek replay, %n file(s)", nullptr, int(o.files.size()));
  JobSpec spec =
      ctx_.tools->makeSpec(kTool, ayzek::arguments(o), workDir, name);
  events_->clear();
  log_->clear();
  summary_->setText(tr("Running..."));
  running_ = ctx_.jobs->addProcess(spec);
  connect(running_, &Job::lineReceived, this, &EarlyWarningWidget::onLine);
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running)
      finished();
  });
  tabs_->setCurrentIndex(1);
  say(tr("Started: %1 %2").arg(spec.program, spec.args.join(' ')));
  updateControls();
}

void EarlyWarningWidget::stop() {
  if (running_)
    running_->cancel();
}

void EarlyWarningWidget::onLine(const QString &line) {
  log_->appendPlainText(ayzek::stripAnsi(line));
  if (const auto e = ayzek::parseEventLine(line)) {
    events_->append(*e);
    eventsView_->scrollToBottom();
  }
}

void EarlyWarningWidget::finished() {
  Job *job = running_;
  running_ = nullptr;
  const ayzek::Summary s = ayzek::parseSummary(job->log());
  QStringList parts;
  if (s.alarms >= 0)
    parts << tr("%n alarm(s)", nullptr, s.alarms);
  if (s.correct >= 0)
    parts << tr("%1 matching a catalogue event, %2 without one")
                 .arg(s.correct)
                 .arg(s.falseAlarms);
  if (s.events >= 0)
    parts << tr("%1 catalogue events: %2 detected (%3 before S), %4 missed")
                 .arg(s.events)
                 .arg(s.detected)
                 .arg(s.detectedBeforeS)
                 .arg(s.missed);
  QString text = stateName(job->state());
  if (job->state() == Job::State::Failed)
    text += tr(" (exit %1; see the log)").arg(job->exitCode());
  if (!parts.isEmpty())
    text += QStringLiteral(": ") + parts.join(QStringLiteral("; "));
  summary_->setText(text);
  say(text, job->state() == Job::State::Failed);
  updateControls();
}

void EarlyWarningWidget::openOutputDir() {
  const QString dir = output_->text().trimmed();
  QDesktopServices::openUrl(
      QUrl::fromLocalFile(dir.isEmpty() ? QDir::homePath() : dir));
}

void EarlyWarningWidget::say(const QString &message, bool warning) {
  status_->setText(message);
  status_->setStyleSheet(warning ? QStringLiteral("color: #a00;") : QString());
}

void EarlyWarningWidget::updateControls() {
  const bool idle = running_ == nullptr;
  const bool live = mode_->currentData().toInt() == int(ayzek::Mode::Live);
  run_->setEnabled(idle && (live || files_->count() > 0) &&
                   ctx_.tools->status(kTool).found());
  stop_->setEnabled(!idle);
  removeFiles_->setEnabled(idle && files_->count() > 0);
  mode_->setEnabled(idle);
}

} // namespace skg
