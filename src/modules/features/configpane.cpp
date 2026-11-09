#include "modules/features/configpane.h"

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
  for (auto *w : {station_, catalog_, stationCoords_})
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
}

} // namespace skg
