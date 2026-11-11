#include "modules/features/configpane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include "modules/features/bandsmodel.h"
#include <QDir>
#include <QFileInfo>
#include <QSignalBlocker>
#include <QTimeZone>
#include "fx/csv.h"
#include <algorithm>

namespace skg {

ConfigPane::ConfigPane(QWidget *parent) : QWidget(parent) {
  auto addRow = [this](QFormLayout *form, const char *source, QWidget *field) {
    auto *label = new QLabel(this);
    form->addRow(label, field);
    rows_.append({label, source});
  };
  auto pair = [this](QWidget *a, QWidget *b) {
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(a, 1);
    l->addWidget(b);
    return w;
  };

  // --- general ------------------------------------------------------------
  general_ = new QGroupBox(this);
  auto *g = new QFormLayout(general_);
  station_ = new QLineEdit(this);
  from_ = new QLineEdit(this);
  to_ = new QLineEdit(this);
  channels_ = new QLineEdit(this);
  from_->setObjectName(QStringLiteral("periodFrom"));
  to_->setObjectName(QStringLiteral("periodTo"));
  channels_->setObjectName(QStringLiteral("channels"));
  fs_ = dspin(0.01, 10000, 3, 1);
  winSec_ = ispin(1, 864000);
  stepSec_ = ispin(1, 864000);
  staSec_ = dspin(0.01, 100000, 2, 0.5);
  ltaSec_ = ispin(1, 864000);
  freqmin_ = dspin(0.0, 10000, 3, 0.1);
  freqmax_ = dspin(0.0, 10000, 3, 0.1);
  splitHz_ = dspin(0.0, 10000, 3, 0.1);
  chaosOnly_ = new QCheckBox(this);
  addRow(g, QT_TR_NOOP("Station"), station_);
  addRow(g, QT_TR_NOOP("Channels"), channels_);
  addRow(g, QT_TR_NOOP("Read from (UTC)"), from_);
  addRow(g, QT_TR_NOOP("Read up to (UTC)"), to_);
  addRow(g, QT_TR_NOOP("Sampling rate after decimation (Hz)"), fs_);
  addRow(g, QT_TR_NOOP("Window (s)"), winSec_);
  addRow(g, QT_TR_NOOP("Step (s)"), stepSec_);
  addRow(g, QT_TR_NOOP("STA (s)"), staSec_);
  addRow(g, QT_TR_NOOP("LTA (s)"), ltaSec_);
  addRow(g, QT_TR_NOOP("Bandpass low (Hz)"), freqmin_);
  addRow(g, QT_TR_NOOP("Bandpass high (Hz)"), freqmax_);
  addRow(g, QT_TR_NOOP("Spectral split (Hz)"), splitHz_);
  g->addRow(QString(), chaosOnly_);

  // --- chaos --------------------------------------------------------------
  chaos_ = new QGroupBox(this);
  auto *c = new QFormLayout(chaos_);
  tau_ = ispin(1, 10000);
  dim_ = ispin(1, 50);
  evolve_ = ispin(1, 10000);
  minSamples_ = ispin(10, 10000000);
  meanPeriod_ = dspin(0.001, 100000, 3, 0.1);
  riseLo_ = dspin(0.0, 1.0, 2, 0.05);
  riseHi_ = dspin(0.0, 1.0, 2, 0.05);
  horizon_ = dspin(0.1, 10000, 1, 1);
  sampentM_ = ispin(1, 20);
  sampentR_ = dspin(0.001, 10, 3, 0.05);
  sampentTol_ = new QLabel(this);
  sampentTol_->setWordWrap(true);
  addRow(c, QT_TR_NOOP("Embedding delay tau (samples)"), tau_);
  addRow(c, QT_TR_NOOP("Embedding dimension"), dim_);
  addRow(c, QT_TR_NOOP("Wolf evolve steps"), evolve_);
  addRow(c, QT_TR_NOOP("Minimum samples"), minSamples_);
  addRow(c, QT_TR_NOOP("Mean period (s)"), meanPeriod_);
  addRow(c, QT_TR_NOOP("Rosenstein rise from"), riseLo_);
  addRow(c, QT_TR_NOOP("Rosenstein rise to"), riseHi_);
  addRow(c, QT_TR_NOOP("Rosenstein horizon (mean periods)"), horizon_);
  addRow(c, QT_TR_NOOP("SampEn m"), sampentM_);
  addRow(c, QT_TR_NOOP("SampEn r"), sampentR_);
  addRow(c, QT_TR_NOOP("SampEn tolerance"), sampentTol_);

  // --- calibration --------------------------------------------------------
  calibration_ = new QGroupBox(this);
  auto *k = new QFormLayout(calibration_);
  catalog_ = new QLineEdit(this);
  catalogBrowse_ = new QPushButton(QStringLiteral("…"), this);
  catalogBrowse_->setFixedWidth(32);
  stationCoords_ = new QLineEdit(this);
  stationCoordsBrowse_ = new QPushButton(QStringLiteral("…"), this);
  stationCoordsBrowse_->setFixedWidth(32);
  useCoords_ = new QCheckBox(this);
  lat_ = dspin(-90, 90, 5, 0.01);
  lon_ = dspin(-180, 180, 5, 0.01);
  minMag_ = dspin(-2, 10, 1, 0.1);
  maxDist_ = dspin(1, 20000, 0, 10);
  preSec_ = dspin(0, 86400, 0, 10);
  minPost_ = dspin(0, 86400, 0, 10);
  coda_ = dspin(0, 100, 1, 0.5);
  maxWindows_ = ispin(10, 100000);
  loud_ = dspin(1, 100, 1, 0.5);
  seed_ = ispin(0, 2147483647);
  maxTau_ = ispin(1, 10000);
  maxDim_ = ispin(1, 50);
  addRow(k, QT_TR_NOOP("Earthquake catalog (CSV, .gz)"),
         pair(catalog_, catalogBrowse_));
  addRow(k, QT_TR_NOOP("Station list (CSV)"),
         pair(stationCoords_, stationCoordsBrowse_));
  k->addRow(QString(), useCoords_);
  addRow(k, QT_TR_NOOP("Latitude"), lat_);
  addRow(k, QT_TR_NOOP("Longitude"), lon_);
  addRow(k, QT_TR_NOOP("Minimum magnitude"), minMag_);
  addRow(k, QT_TR_NOOP("Maximum distance (km)"), maxDist_);
  addRow(k, QT_TR_NOOP("Seconds before an event"), preSec_);
  addRow(k, QT_TR_NOOP("Minimum seconds after"), minPost_);
  addRow(k, QT_TR_NOOP("Coda factor"), coda_);
  addRow(k, QT_TR_NOOP("Quiet windows sampled"), maxWindows_);
  addRow(k, QT_TR_NOOP("Loudness factor"), loud_);
  addRow(k, QT_TR_NOOP("Seed"), seed_);
  addRow(k, QT_TR_NOOP("Maximum tau"), maxTau_);
  addRow(k, QT_TR_NOOP("Maximum dimension"), maxDim_);

  // --- bands --------------------------------------------------------------
  bandsBox_ = new QGroupBox(this);
  bands_ = new BandsModel(this);
  bandsView_ = new QTableView(this);
  bandsView_->setModel(bands_);
  bandsView_->verticalHeader()->hide();
  bandsView_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  bandsView_->horizontalHeader()->setSectionResizeMode(BandsModel::Name,
                                                       QHeaderView::Stretch);
  bandsView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  bandsView_->setMinimumHeight(120);
  addBand_ = new QPushButton(this);
  removeBand_ = new QPushButton(this);
  auto *bandButtons = new QHBoxLayout;
  bandButtons->addWidget(addBand_);
  bandButtons->addWidget(removeBand_);
  bandButtons->addStretch(1);
  auto *bl = new QVBoxLayout(bandsBox_);
  bl->addWidget(bandsView_);
  bl->addLayout(bandButtons);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(general_);
  layout->addWidget(chaos_);
  layout->addWidget(calibration_);
  layout->addWidget(bandsBox_);
  layout->addStretch(1);

  // --- behaviour ----------------------------------------------------------
  connect(useCoords_, &QCheckBox::toggled, this, [this](bool on) {
    lat_->setEnabled(on);
    lon_->setEnabled(on);
  });
  connect(catalogBrowse_, &QPushButton::clicked, this, [this] {
    browseFile(catalog_, tr("Catalogs (*.csv *.csv.gz);;All files (*)"));
  });
  connect(stationCoordsBrowse_, &QPushButton::clicked, this, [this] {
    browseFile(stationCoords_, tr("Station lists (*.csv);;All files (*)"));
  });
  connect(addBand_, &QPushButton::clicked, this, [this] {
    fx::Band b;
    b.freqmin = freqmin_->value();
    b.freqmax = freqmax_->value();
    b.fs = fs_->value();
    b.winSec = winSec_->value();
    b.stepSec = stepSec_->value();
    b.staSec = staSec_->value();
    b.ltaSec = ltaSec_->value();
    bands_->addBand(b);
    emit edited();
  });
  connect(removeBand_, &QPushButton::clicked, this, [this] {
    QList<int> rows;
    for (const QModelIndex &i : bandsView_->selectionModel()->selectedRows())
      rows << i.row();
    bands_->removeBands(rows);
    emit edited();
  });
  connect(bands_, &QAbstractItemModel::dataChanged, this,
          [this] { emit edited(); });
  for (auto *w : {fs_, staSec_, freqmin_, freqmax_, splitHz_, meanPeriod_,
                  riseLo_, riseHi_, horizon_, sampentR_, lat_, lon_, minMag_,
                  maxDist_, preSec_, minPost_, coda_, loud_})
    connect(w, &QDoubleSpinBox::valueChanged, this, [this] { emit edited(); });
  for (auto *w : {winSec_, stepSec_, ltaSec_, tau_, dim_, evolve_, minSamples_,
                  sampentM_, maxWindows_, seed_, maxTau_, maxDim_})
    connect(w, &QSpinBox::valueChanged, this, [this] { emit edited(); });
  for (auto *w : {station_, catalog_, stationCoords_, from_, to_, channels_})
    connect(w, &QLineEdit::textEdited, this, [this] { emit edited(); });
  for (auto *w : {chaosOnly_, useCoords_})
    connect(w, &QCheckBox::toggled, this, [this] { emit edited(); });

  retranslate();
  setConfig(fx::Config());
}

QDoubleSpinBox *ConfigPane::dspin(double lo, double hi, int decimals,
                                  double step) {
  auto *s = new QDoubleSpinBox(this);
  s->setRange(lo, hi);
  s->setDecimals(decimals);
  s->setSingleStep(step);
  return s;
}

QSpinBox *ConfigPane::ispin(int lo, int hi) {
  auto *s = new QSpinBox(this);
  s->setRange(lo, hi);
  return s;
}

void ConfigPane::browseFile(QLineEdit *target, const QString &filter) {
  const QString start = target->text().isEmpty()
                            ? QDir::homePath()
                            : QFileInfo(target->text()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Choose a file"), start, filter);
  if (!path.isEmpty()) {
    target->setText(path);
    emit edited();
  }
}

void ConfigPane::setConfig(const fx::Config &cfg) {
  base_ = cfg;
  const QSignalBlocker b(this);
  station_->setText(cfg.station);
  const auto isoOf = [](const std::optional<double> &epoch) {
    return epoch ? QDateTime::fromMSecsSinceEpoch(qint64(*epoch * 1000.0),
                                                  QTimeZone::UTC)
                       .toString(Qt::ISODate)
                 : QString();
  };
  from_->setText(isoOf(cfg.startEpoch));
  to_->setText(isoOf(cfg.endEpoch));
  channels_->setText(cfg.channels);
  fs_->setValue(cfg.fs);
  winSec_->setValue(cfg.winSec);
  stepSec_->setValue(cfg.stepSec);
  staSec_->setValue(cfg.staSec);
  ltaSec_->setValue(cfg.ltaSec);
  freqmin_->setValue(cfg.freqmin);
  freqmax_->setValue(cfg.freqmax);
  splitHz_->setValue(cfg.spectralSplitHz);
  chaosOnly_->setChecked(cfg.chaosOnly);
  tau_->setValue(cfg.chaosTau);
  dim_->setValue(cfg.chaosDim);
  evolve_->setValue(cfg.chaosEvolve);
  minSamples_->setValue(cfg.chaosMinSamples);
  meanPeriod_->setValue(cfg.chaosMeanPeriod);
  riseLo_->setValue(cfg.chaosRosRise[0]);
  riseHi_->setValue(cfg.chaosRosRise[1]);
  horizon_->setValue(cfg.chaosRosHorizon);
  sampentM_->setValue(cfg.chaosSampentM);
  sampentR_->setValue(cfg.chaosSampentR);
  if (cfg.chaosSampentTol)
    sampentTol_->setText(tr("E %1, N %2, Z %3 (absolute, from a calibration)")
                             .arg((*cfg.chaosSampentTol)[0], 0, 'g', 4)
                             .arg((*cfg.chaosSampentTol)[1], 0, 'g', 4)
                             .arg((*cfg.chaosSampentTol)[2], 0, 'g', 4));
  else
    sampentTol_->setText(tr("r times the quiet std of each component, set by "
                            "the calibration of the run"));
  const fx::CalibrationConfig &k = cfg.calibration;
  catalog_->setText(k.catalog);
  stationCoords_->setText(k.stationCoords);
  const bool coords = k.stationLat && k.stationLon;
  useCoords_->setChecked(coords);
  lat_->setEnabled(coords);
  lon_->setEnabled(coords);
  lat_->setValue(k.stationLat.value_or(0.0));
  lon_->setValue(k.stationLon.value_or(0.0));
  minMag_->setValue(k.minMag);
  maxDist_->setValue(k.maxDistKm);
  preSec_->setValue(k.preSec);
  minPost_->setValue(k.minPostSec);
  coda_->setValue(k.codaFactor);
  maxWindows_->setValue(k.maxWindows);
  loud_->setValue(k.loudFactor);
  seed_->setValue(int(std::min<quint64>(k.seed, 2147483647)));
  maxTau_->setValue(k.maxTau);
  maxDim_->setValue(k.maxDim);
  bands_->setBands(cfg.bands);
}

fx::Config ConfigPane::config() const {
  fx::Config cfg = base_;
  cfg.station = station_->text().trimmed().toUpper();
  // Empty or unreadable: no limit on that side (validate() says when the
  // two are the wrong way round).
  cfg.startEpoch = fx::parseIsoUtc(from_->text());
  cfg.endEpoch = fx::parseIsoUtc(to_->text());
  cfg.channels = channels_->text().trimmed().toUpper();
  cfg.fs = fs_->value();
  cfg.winSec = winSec_->value();
  cfg.stepSec = stepSec_->value();
  cfg.staSec = staSec_->value();
  cfg.ltaSec = ltaSec_->value();
  cfg.freqmin = freqmin_->value();
  cfg.freqmax = freqmax_->value();
  cfg.spectralSplitHz = splitHz_->value();
  cfg.chaosOnly = chaosOnly_->isChecked();
  cfg.chaosTau = tau_->value();
  cfg.chaosDim = dim_->value();
  cfg.chaosEvolve = evolve_->value();
  cfg.chaosMinSamples = minSamples_->value();
  cfg.chaosMeanPeriod = meanPeriod_->value();
  cfg.chaosRosRise = {riseLo_->value(), riseHi_->value()};
  cfg.chaosRosHorizon = horizon_->value();
  cfg.chaosSampentM = sampentM_->value();
  cfg.chaosSampentR = sampentR_->value();
  fx::CalibrationConfig &k = cfg.calibration;
  k.catalog = catalog_->text().trimmed();
  k.stationCoords = stationCoords_->text().trimmed();
  if (useCoords_->isChecked()) {
    k.stationLat = lat_->value();
    k.stationLon = lon_->value();
  } else {
    k.stationLat.reset();
    k.stationLon.reset();
  }
  k.minMag = minMag_->value();
  k.maxDistKm = maxDist_->value();
  k.preSec = preSec_->value();
  k.minPostSec = minPost_->value();
  k.codaFactor = coda_->value();
  k.maxWindows = maxWindows_->value();
  k.loudFactor = loud_->value();
  k.seed = quint64(seed_->value());
  k.maxTau = maxTau_->value();
  k.maxDim = maxDim_->value();
  cfg.bands = bands_->bands();
  return cfg;
}

void ConfigPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void ConfigPane::retranslate() {
  general_->setTitle(tr("Run"));
  chaos_->setTitle(tr("Chaos features"));
  calibration_->setTitle(tr("Calibration on quiet periods"));
  bandsBox_->setTitle(tr("Bands (each is a run of its own; empty = one run "
                         "with the settings above)"));
  for (const Row &r : rows_)
    r.label->setText(tr(r.source));
  chaosOnly_->setText(tr("chaos features only"));
  useCoords_->setText(tr("set the station position directly"));
  addBand_->setText(tr("Add band"));
  stationCoords_->setPlaceholderText(
      tr("empty: the built-in AFAD station list"));
  catalog_->setPlaceholderText(
      tr("needed for a full run unless a calibration is reused"));
  removeBand_->setText(tr("Remove"));
  fs_->setSuffix(tr(" Hz"));
  freqmin_->setSuffix(tr(" Hz"));
  freqmax_->setSuffix(tr(" Hz"));
  splitHz_->setSuffix(tr(" Hz"));
  for (auto *s : {winSec_, stepSec_, ltaSec_})
    s->setSuffix(tr(" s"));
  for (auto *s : {staSec_, meanPeriod_, preSec_, minPost_})
    s->setSuffix(tr(" s"));
  maxDist_->setSuffix(tr(" km"));

  ui::setHelp(station_, tr("The station code. It only labels the outputs "
                           "and looks the station up in the station list "
                           "for the calibration."));
  ui::setHelp(channels_,
              tr("Which sensor of the station is read when the files hold "
                 "several: the first two letters of the channel code, such "
                 "as HH (broadband) or HN (accelerometer), with a location "
                 "code in front when needed (00.HH). Empty: the only sensor "
                 "there is; with several, the one with the most data, and "
                 "the log says which was taken. Two are never mixed."));
  channels_->setPlaceholderText(tr("automatic"));
  ui::setHelp(from_,
              tr("Only data recorded at or after this time is read, as a "
                 "date or a date and time in UTC (2020-01-24 or "
                 "2020-01-24T17:55:00). Empty: from the start of the "
                 "files. The calibration reads the same period."));
  ui::setHelp(to_,
              tr("Only data recorded before this time is read (UTC, as "
                 "above). Empty: to the end of the files. Set it to an "
                 "earthquake's time to keep everything after it out of the "
                 "features and of the calibration."));
  from_->setPlaceholderText(tr("the start of the files"));
  to_->setPlaceholderText(tr("the end of the files"));
  ui::setHelp(fs_,
              tr("The sampling rate the data is reduced to before the "
                 "features are computed. The recorded rate must be a whole "
                 "multiple of it. Default 5 Hz."));
  ui::setHelp(winSec_, tr("Length of one analysis window in seconds. Each "
                          "window gives one row of features. Default "
                          "200 s."));
  ui::setHelp(stepSec_,
              tr("How far the window moves between two rows, in seconds. "
                 "A step shorter than the window makes the windows "
                 "overlap. Default 50 s."));
  ui::setHelp(staSec_,
              tr("Length of the short-term average in the STA/LTA feature "
                 "(the ratio of recent to background signal level), in "
                 "seconds. Default 0.5 s."));
  ui::setHelp(ltaSec_, tr("Length of the long-term (background) average in "
                          "the STA/LTA feature, in seconds. Default 60 s."));
  ui::setHelp(freqmin_, tr("Lower corner of the bandpass filter applied "
                           "before the features: slower motion is removed. "
                           "Default 0.1 Hz."));
  ui::setHelp(freqmax_,
              tr("Upper corner of the bandpass filter: faster motion is "
                 "removed. It must stay below half the sampling rate. "
                 "Default 2 Hz."));
  ui::setHelp(splitHz_,
              tr("The frequency that separates \"low\" from \"high\" in the "
                 "low- and high-frequency energy features. Default 1 Hz."));
  ui::setHelp(chaosOnly_,
              tr("Computes only the chaotic features (Lyapunov exponents, "
                 "sample entropy and the like) and writes them to a "
                 "separate file, <name>_chaos_features."));
  ui::setHelp(tau_,
              tr("The embedding delay: how many samples apart the values "
                 "are that are combined into one point of the "
                 "reconstructed state space. In a full run it is "
                 "calibrated from the data and this is only the starting "
                 "value."));
  ui::setHelp(dim_,
              tr("The embedding dimension: how many delayed values make up "
                 "one point of the reconstructed state space. Calibrated "
                 "in a full run; this is the starting value."));
  ui::setHelp(evolve_,
              tr("For the Wolf Lyapunov exponent: how many samples two "
                 "neighbouring trajectories are followed before their "
                 "separation is measured and a new neighbour is chosen. "
                 "Default 5."));
  ui::setHelp(minSamples_,
              tr("A window with fewer samples than this gets no Lyapunov "
                 "exponents (the values are left empty). Default 500."));
  ui::setHelp(meanPeriod_,
              tr("The typical period of the signal in seconds. It keeps "
                 "neighbours in the Rosenstein method apart in time and is "
                 "its time unit. Calibrated in a full run; this is the "
                 "starting value."));
  ui::setHelp({riseLo_, riseHi_},
              tr("The Rosenstein exponent is the slope of the divergence "
                 "curve between these two fractions of its total rise. "
                 "Defaults 0.1 and 0.9: the middle of the rise, without "
                 "its start and its flattening end."));
  ui::setHelp(horizon_,
              tr("How long neighbouring trajectories are followed for the "
                 "Rosenstein divergence curve, in mean periods. "
                 "Default 20."));
  ui::setHelp(sampentM_,
              tr("Sample entropy compares runs of this many consecutive "
                 "samples (the pattern length m). Default 2."));
  ui::setHelp(sampentR_,
              tr("Sample entropy counts two patterns as alike when they "
                 "differ by less than r times the signal's standard "
                 "deviation. In a full run the tolerance is set from the "
                 "quiet periods. Default 0.2."));
  ui::setHelp({catalog_, catalogBrowse_},
              tr("The AFAD earthquake catalog (CSV or .csv.gz). The "
                 "calibration uses it to find the periods without "
                 "earthquakes. Needed for a full run unless a calibration "
                 "is reused."));
  ui::setHelp({stationCoords_, stationCoordsBrowse_},
              tr("A CSV with station codes, latitudes and longitudes, to "
                 "find the station's position. Empty uses the AFAD list "
                 "built into the application."));
  ui::setHelp(useCoords_, tr("Type the station's latitude and longitude "
                             "below instead of looking the code up in the "
                             "station list."));
  ui::setHelp(lat_, tr("Latitude of the station in degrees, north "
                       "positive."));
  ui::setHelp(lon_, tr("Longitude of the station in degrees, east "
                       "positive."));
  ui::setHelp(minMag_,
              tr("Only earthquakes of at least this magnitude make a period "
                 "\"not quiet\" for the calibration."));
  ui::setHelp(maxDist_, tr("Only earthquakes within this distance of the "
                           "station make a period \"not quiet\", in km."));
  ui::setHelp(preSec_, tr("The time excluded as not quiet starts this many "
                          "seconds before an earthquake's origin time."));
  ui::setHelp(minPost_,
              tr("The time after an earthquake that is excluded at least, "
                 "in seconds. The larger of this and the coda duration "
                 "counts."));
  ui::setHelp(coda_,
              tr("Multiplies the estimated coda duration (how long the "
                 "shaking takes to die down, after Lee) to get the time "
                 "excluded after an earthquake. Larger excludes more."));
  ui::setHelp(maxWindows_,
              tr("How many quiet, gap-free windows are drawn at random for "
                 "the calibration. More is slower and steadier."));
  ui::setHelp(loud_,
              tr("Of the sampled quiet windows, those louder than this "
                 "factor times the median level are dropped, per "
                 "component, as probably not quiet after all."));
  ui::setHelp(seed_, tr("Starting value of the random draw of quiet "
                        "windows. The same seed repeats the same "
                        "calibration."));
  ui::setHelp(maxTau_,
              tr("The largest embedding delay the calibration tries. Tau "
                 "is the first minimum of the average mutual information "
                 "up to this value."));
  ui::setHelp(maxDim_,
              tr("The largest embedding dimension the calibration tries "
                 "with the false-nearest-neighbours test."));
  bandsView_->setToolTip(ui::helpText(
      tr("Optional frequency bands. Each row is a complete run of its own "
         "(band corners, rate, window, step, STA, LTA, split), written to "
         "a sub-folder named after the band. Edit a cell in place.")));
  addBand_->setToolTip(ui::helpText(
      tr("Adds a band row filled with the settings shown above.")));
  removeBand_->setToolTip(
      ui::helpText(tr("Removes the selected band row.")));
}

} // namespace skg
