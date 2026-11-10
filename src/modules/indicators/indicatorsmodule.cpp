#include "modules/indicators/indicatorsmodule.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <exception>

#include "cbi/catalog.h"
#include "cbi/decluster.h"
#include "cbi/magnitudes.h"
#include "cbi/zones.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/indicators/resultspane.h"

namespace skg {

namespace {
const QString kPrefix = QStringLiteral("indicators/");
QString key(const char *name) { return kPrefix + QLatin1String(name); }

QChar unitAt(const QComboBox *c) {
  return QLatin1Char("DWMY"[std::clamp(c->currentIndex(), 0, 3)]);
}
} // namespace

QWidget *IndicatorsModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new IndicatorsWidget(ctx, parent);
}

IndicatorsWidget::IndicatorsWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::make_shared<IndicatorsState>()) {
  auto edit = [this](QPushButton **browse) {
    auto *e = new QLineEdit(this);
    *browse = new QPushButton(QStringLiteral("…"), this);
    (*browse)->setFixedWidth(32);
    return e;
  };
  auto line = [this](std::initializer_list<QWidget *> ws) {
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    bool first = true;
    for (QWidget *x : ws) {
      l->addWidget(x, first ? 1 : 0);
      first = false;
    }
    return w;
  };
  auto dspin = [this](double lo, double hi, double step, int decimals) {
    auto *s = new QDoubleSpinBox(this);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(decimals);
    return s;
  };
  auto spin = [this](int lo, int hi) {
    auto *s = new QSpinBox(this);
    s->setRange(lo, hi);
    return s;
  };
  auto unitBox = [this] {
    auto *c = new QComboBox(this);
    c->addItems({QString(), QString(), QString(), QString()});
    return c;
  };

  // Catalog and selection
  catalog_ = edit(&catalogBrowse_);
  describe_ = new QPushButton(this);
  zone_ = new QComboBox(this);
  zone_->addItems({QString(), QString(), QString(), QString()});
  zoneFile_ = edit(&zoneBrowse_);
  strands_ = new QLineEdit(this);
  buffer_ = dspin(0, 1000, 5, 0);
  lat_ = dspin(-90, 90, 0.1, 3);
  lon_ = dspin(-180, 180, 0.1, 3);
  radius_ = dspin(1, 5000, 10, 0);
  useStart_ = new QCheckBox(this);
  start_ = new QDateEdit(this);
  start_->setCalendarPopup(true);
  start_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  homogenize_ = new QCheckBox(this);
  conversion_ = edit(&conversionBrowse_);
  fit_ = new QPushButton(this);
  decluster_ = new QCheckBox(this);
  // Indicators
  mc_ = dspin(0, 9, 0.1, 2);
  estimateMc_ = new QPushButton(this);
  magBin_ = dspin(0.001, 1, 0.01, 3);
  window_ = spin(1, 120);
  windowUnit_ = unitBox();
  step_ = spin(1, 120);
  stepUnit_ = unitBox();
  sigma_ = dspin(0.05, 5, 0.05, 2);
  ma_ = new QLineEdit(this);
  maCombined_ = new QCheckBox(this);
  bollinger_ = new QCheckBox(this);
  bbWindow_ = spin(2, 500);
  bbK_ = dspin(0.1, 10, 0.5, 1);
  labelMag_ = dspin(0, 9, 0.1, 1);
  labelDays_ = spin(1, 3650);
  outRoot_ = edit(&outBrowse_);
  plots_ = new QCheckBox(this);
  dpi_ = spin(30, 600);
  run_ = new QPushButton(this);
  stop_ = new QPushButton(this);
  status_ = new QLabel(this);
  status_->setWordWrap(true);
  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  catalogLabel_ = new QLabel(this);
  zoneLabel_ = new QLabel(this);
  zoneFileLabel_ = new QLabel(this);
  strandsLabel_ = new QLabel(this);
  bufferLabel_ = new QLabel(this);
  circleLabel_ = new QLabel(this);
  startLabel_ = new QLabel(this);
  conversionLabel_ = new QLabel(this);
  mcLabel_ = new QLabel(this);
  magBinLabel_ = new QLabel(this);
  windowLabel_ = new QLabel(this);
  stepLabel_ = new QLabel(this);
  sigmaLabel_ = new QLabel(this);
  maLabel_ = new QLabel(this);
  bbLabel_ = new QLabel(this);
  labelLabel_ = new QLabel(this);
  outLabel_ = new QLabel(this);
  dpiLabel_ = new QLabel(this);

  auto *catBox = new QGroupBox(this);
  catBox->setObjectName(QStringLiteral("catalogBox"));
  auto *cf = new QFormLayout(catBox);
  cf->addRow(catalogLabel_, line({catalog_, catalogBrowse_, describe_}));
  cf->addRow(zoneLabel_, zone_);
  cf->addRow(zoneFileLabel_, line({zoneFile_, zoneBrowse_}));
  cf->addRow(strandsLabel_, strands_);
  cf->addRow(bufferLabel_, buffer_);
  circleRow_ = line({lat_, lon_, radius_});
  cf->addRow(circleLabel_, circleRow_);
  cf->addRow(startLabel_, line({start_, useStart_}));
  cf->addRow(homogenize_, line({conversion_, conversionBrowse_, fit_}));
  cf->addRow(QString(), decluster_);

  auto *indBox = new QGroupBox(this);
  auto *inf = new QFormLayout(indBox);
  inf->addRow(mcLabel_, line({mc_, estimateMc_}));
  inf->addRow(magBinLabel_, magBin_);
  windowRow_ = line({window_, windowUnit_});
  inf->addRow(windowLabel_, windowRow_);
  stepRow_ = line({step_, stepUnit_});
  inf->addRow(stepLabel_, stepRow_);
  inf->addRow(sigmaLabel_, sigma_);
  inf->addRow(maLabel_, line({ma_, maCombined_}));
  bbRow_ = line({bbWindow_, bbK_});
  inf->addRow(bollinger_, bbRow_);
  labelRow_ = line({labelMag_, labelDays_});
  inf->addRow(labelLabel_, labelRow_);
  inf->addRow(outLabel_, line({outRoot_, outBrowse_}));
  inf->addRow(dpiLabel_, line({dpi_, plots_}));

  auto *settings = new QWidget(this);
  auto *sl = new QVBoxLayout(settings);
  sl->addWidget(catBox);
  sl->addWidget(indBox);
  auto *buttons = new QHBoxLayout;
  buttons->addStretch(1);
  buttons->addWidget(run_);
  buttons->addWidget(stop_);
  sl->addLayout(buttons);
  sl->addWidget(status_);
  sl->addStretch(1);
  auto *scroll = new QScrollArea(this);
  scroll->setWidget(settings);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  auto *runTab = new QSplitter(Qt::Horizontal, this);
  runTab->addWidget(scroll);
  runTab->addWidget(log_);
  runTab->setStretchFactor(0, 3);
  runTab->setStretchFactor(1, 2);
  results_ = new IndicatorsResultsPane(state_, this);
  tabs_ = new QTabWidget(this);
  tabs_->addTab(runTab, QString());
  tabs_->addTab(results_, QString());
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(tabs_);

  connect(catalogBrowse_, &QPushButton::clicked, this, [this] {
    browse(catalog_, tr("Catalogs (*.csv *.csv.gz);;All files (*)"));
  });
  connect(zoneBrowse_, &QPushButton::clicked, this, [this] {
    browse(zoneFile_, tr("GeoJSON (*.geojson *.json);;All files (*)"));
  });
  connect(conversionBrowse_, &QPushButton::clicked, this, [this] {
    browse(conversion_, tr("Magnitude conversion (*.json);;All files (*)"));
  });
  connect(outBrowse_, &QPushButton::clicked, this,
          [this] { browse(outRoot_, QString(), true); });
  connect(describe_, &QPushButton::clicked, this,
          &IndicatorsWidget::describeCatalog);
  connect(estimateMc_, &QPushButton::clicked, this,
          &IndicatorsWidget::estimateMc);
  connect(fit_, &QPushButton::clicked, this, &IndicatorsWidget::fitConversion);
  connect(run_, &QPushButton::clicked, this, &IndicatorsWidget::startRun);
  connect(stop_, &QPushButton::clicked, this, &IndicatorsWidget::stop);
  connect(zone_, &QComboBox::currentIndexChanged, this,
          &IndicatorsWidget::updateControls);
  connect(useStart_, &QCheckBox::toggled, this,
          &IndicatorsWidget::updateControls);
  connect(homogenize_, &QCheckBox::toggled, this,
          &IndicatorsWidget::updateControls);
  connect(bollinger_, &QCheckBox::toggled, this,
          &IndicatorsWidget::updateControls);
  connect(plots_, &QCheckBox::toggled, this, &IndicatorsWidget::updateControls);

  loadSettings();
  retranslate();
  updateControls();
}

void IndicatorsWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void IndicatorsWidget::retranslate() {
  catalogLabel_->setText(tr("Catalog"));
  catalog_->setPlaceholderText(
      tr("AFAD export CSV: Date, Latitude, Longitude, Magnitude…"));
  describe_->setText(tr("Describe"));
  describe_->setToolTip(
      ui::helpText(tr("Loads the catalog, applies the selection and "
                      "reports the events kept.")));
  zoneLabel_->setText(tr("Events"));
  const QStringList zones{
      tr("Whole catalog"), tr("Near the East Anatolian Fault Zone"),
      tr("Near a fault trace (GeoJSON file)"), tr("Within a circle")};
  for (int i = 0; i < zones.size(); ++i)
    zone_->setItemText(i, zones[i]);
  zoneFileLabel_->setText(tr("Fault GeoJSON"));
  zoneFile_->setPlaceholderText(
      tr("FeatureCollection of LineString features; property \"strand\" "
         "optional"));
  strandsLabel_->setText(tr("Strands"));
  strands_->setPlaceholderText(
      tr("comma-separated, empty = all (EAFZ: main, northern)"));
  bufferLabel_->setText(tr("Buffer (km)"));
  circleLabel_->setText(tr("Centre lat, lon, radius (km)"));
  startLabel_->setText(tr("From date"));
  useStart_->setText(tr("drop earlier events"));
  homogenize_->setText(tr("Homogenise magnitudes"));
  ui::setHelp(homogenize_,
              tr("Converts AFAD magnitudes before 2013-11-20 onto the "
                 "2014-2019 ML/Mw practice by rate matching. Empty file = "
                 "the conversion shipped with the app. Without it, a "
                 "catalog that crosses that date mixes two magnitude "
                 "scales and every indicator shows an artificial jump."));
  ui::setHelp(conversion_,
              tr("A magnitude conversion saved with Fit…, as JSON. Empty "
                 "uses the conversion shipped with the application."));
  conversion_->setPlaceholderText(tr("conversion JSON (empty = shipped)"));
  fit_->setText(tr("Fit…"));
  fit_->setToolTip(
      ui::helpText(tr("Fits a conversion on a national AFAD catalog "
                      "(declustered) and saves it as JSON.")));
  decluster_->setText(tr("Decluster (Gardner-Knopoff mainshocks only)"));
  mcLabel_->setText(tr("Completeness Mc"));
  estimateMc_->setText(tr("Estimate"));
  estimateMc_->setToolTip(ui::helpText(
      tr("Sets Mc to the maximum-curvature estimate plus 0.2, computed on "
         "the selected events.")));
  magBinLabel_->setText(tr("Magnitude bin"));
  windowLabel_->setText(tr("Window"));
  stepLabel_->setText(tr("Step"));
  const QStringList units{tr("days"), tr("weeks"), tr("months"), tr("years")};
  for (int i = 0; i < units.size(); ++i) {
    windowUnit_->setItemText(i, units[i]);
    stepUnit_->setItemText(i, units[i]);
  }
  sigmaLabel_->setText(tr("Half-Gauss σ"));
  maLabel_->setText(tr("Moving averages"));
  ma_->setPlaceholderText(tr("window counts, e.g. 7,15,23"));
  maCombined_->setText(tr("on one figure"));
  bollinger_->setText(tr("Bollinger bands"));
  bbWindow_->setPrefix(tr("window "));
  bbK_->setPrefix(tr("k "));
  labelLabel_->setText(tr("Risk label"));
  labelMag_->setPrefix(tr("M ≥ "));
  labelDays_->setSuffix(tr(" days before"));
  outLabel_->setText(tr("Output root"));
  dpiLabel_->setText(tr("Figures"));
  dpi_->setSuffix(tr(" dpi"));
  plots_->setText(tr("write PNG figures"));
  run_->setText(tr("Run"));
  ui::setPrimary(run_);
  log_->setPlaceholderText(tr("The run log appears here."));
  stop_->setText(tr("Stop"));
  tabs_->setTabText(0, tr("Settings and run"));
  tabs_->setTabText(1, tr("Results"));
  if (auto *box = findChild<QGroupBox *>(QStringLiteral("catalogBox")))
    box->setTitle(tr("Catalog and selection"));
  for (QGroupBox *box : findChildren<QGroupBox *>())
    if (box->objectName().isEmpty())
      box->setTitle(tr("Indicators"));

  ui::setHelp(catalog_,
              tr("The earthquake catalog: an AFAD export (CSV or .gz) with "
                 "Date as dd/mm/YYYY HH:MM:SS, Latitude, Longitude and "
                 "Magnitude."));
  ui::setHelp(zone_,
              tr("Which events of the catalog are used: all of them, those "
                 "near the East Anatolian Fault Zone (traces shipped with "
                 "the application), those near the fault traces of your "
                 "own GeoJSON file, or those within a circle."));
  ui::setHelp(zoneFile_,
              tr("Your own fault traces: a GeoJSON FeatureCollection of "
                 "LineString features. A \"strand\" property on a feature "
                 "lets you pick strands below."));
  ui::setHelp(strands_,
              tr("Uses only the named strands of the fault traces, "
                 "separated by commas. Empty uses all. The shipped East "
                 "Anatolian traces have main and northern."));
  ui::setHelp(buffer_, tr("Events within this distance of the fault traces "
                          "are used, in km. Default 30."));
  ui::setHelp({lat_, lon_, radius_},
              tr("For the circle: latitude and longitude of its centre in "
                 "degrees, and its radius in km. Events inside it are "
                 "used."));
  ui::setHelp({start_, useStart_},
              tr("Drops the events before this date, for instance from "
                 "before the network was densified."));
  ui::setHelp(decluster_,
              tr("Keeps only the Gardner-Knopoff mainshocks and single "
                 "events, i.e. removes aftershock and foreshock sequences. "
                 "Most indicators then rest on far fewer events."));
  ui::setHelp(mc_,
              tr("The completeness magnitude: the magnitude above which "
                 "the catalog is taken to hold every event. Events below "
                 "it do not enter the b-value, the a-value and η. The "
                 "original analysis used 1.3 for the EAFZ; with the 2014+ "
                 "ML catalog use about 2.0."));
  ui::setHelp(magBin_, tr("The rounding of the catalog's magnitudes (0.1 "
                          "for AFAD). The b-value uses Mc minus half a "
                          "bin."));
  ui::setHelp({window_, windowUnit_},
              tr("Length of the sliding time window each indicator value "
                 "is computed on, as a calendar duration. Longer windows "
                 "hold more events and change more slowly. Default 12 "
                 "months."));
  ui::setHelp({step_, stepUnit_},
              tr("How far the window advances between two values. A step "
                 "shorter than the window makes the windows overlap. "
                 "Default 1 month."));
  ui::setHelp(sigma_,
              tr("Width of the weighting for the second version of the "
                 "mean magnitude and the energy rate, in which the weights "
                 "rise towards the newest event of the window. "
                 "Default 0.4."));
  ui::setHelp(ma_, tr("Trailing averages of the indicators over these "
                      "numbers of windows, separated by commas, e.g. "
                      "7,15,23. One figure per number."));
  ui::setHelp(maCombined_, tr("Draws all the moving averages of an "
                              "indicator on one figure instead of one "
                              "figure each."));
  ui::setHelp(bollinger_,
              tr("Also draws each indicator with a trailing mean and bands "
                 "at ± k standard deviations around it."));
  ui::setHelp(bbWindow_, tr("How many windows the trailing mean and "
                            "standard deviation of the Bollinger bands "
                            "cover. Default 20."));
  ui::setHelp(bbK_, tr("Width of the Bollinger bands in standard "
                       "deviations on each side of the mean. Default 2."));
  ui::setHelp({labelMag_, labelDays_},
              tr("The Risk_Durumu column marks the windows that end within "
                 "this many days before an earthquake of at least this "
                 "magnitude."));
  ui::setHelp(outRoot_,
              tr("The folder under which each run makes its own folder, "
                 "named after the catalog, the selection, the window and "
                 "the step."));
  ui::setHelp(dpi_, tr("Resolution of the PNG figures in dots per inch. "
                       "Higher gives larger, sharper pictures."));
  ui::setHelp(plots_, tr("Writes a PNG figure per indicator. Unticked, "
                         "only the window table is written."));
  run_->setToolTip(ui::helpText(
      tr("Computes the indicators on the selected events as a job and "
         "shows the result on the Results tab.")));
  stop_->setToolTip(ui::helpText(tr("Stops the run.")));
}

void IndicatorsWidget::loadSettings() {
  QSettings *s = ctx_.settings;
  catalog_->setText(s->value(key("catalog")).toString());
  zone_->setCurrentIndex(s->value(key("zone"), 1).toInt());
  zoneFile_->setText(s->value(key("zoneFile")).toString());
  strands_->setText(s->value(key("strands")).toString());
  buffer_->setValue(s->value(key("bufferKm"), 30.0).toDouble());
  lat_->setValue(s->value(key("lat"), 38.0).toDouble());
  lon_->setValue(s->value(key("lon"), 38.0).toDouble());
  radius_->setValue(s->value(key("radiusKm"), 100.0).toDouble());
  useStart_->setChecked(s->value(key("useStart"), false).toBool());
  start_->setDate(s->value(key("start"), QDate(2014, 1, 1)).toDate());
  homogenize_->setChecked(s->value(key("homogenize"), false).toBool());
  conversion_->setText(s->value(key("conversion")).toString());
  decluster_->setChecked(s->value(key("decluster"), false).toBool());
  mc_->setValue(s->value(key("mc"), 1.3).toDouble());
  magBin_->setValue(s->value(key("magBin"), 0.1).toDouble());
  window_->setValue(s->value(key("window"), 12).toInt());
  windowUnit_->setCurrentIndex(s->value(key("windowUnit"), 2).toInt());
  step_->setValue(s->value(key("step"), 1).toInt());
  stepUnit_->setCurrentIndex(s->value(key("stepUnit"), 2).toInt());
  sigma_->setValue(s->value(key("sigma"), 0.4).toDouble());
  ma_->setText(s->value(key("ma"), QStringLiteral("7,15,23")).toString());
  maCombined_->setChecked(s->value(key("maCombined"), false).toBool());
  bollinger_->setChecked(s->value(key("bollinger"), false).toBool());
  bbWindow_->setValue(s->value(key("bbWindow"), 20).toInt());
  bbK_->setValue(s->value(key("bbK"), 2.0).toDouble());
  labelMag_->setValue(s->value(key("labelMag"), 6.0).toDouble());
  labelDays_->setValue(s->value(key("labelDays"), 90).toInt());
  outRoot_->setText(
      s->value(key("outRoot"), QDir::homePath() + QStringLiteral("/indicators"))
          .toString());
  plots_->setChecked(s->value(key("plots"), true).toBool());
  dpi_->setValue(s->value(key("dpi"), 100).toInt());
}

void IndicatorsWidget::saveSettings() {
  QSettings *s = ctx_.settings;
  s->setValue(key("catalog"), catalog_->text());
  s->setValue(key("zone"), zone_->currentIndex());
  s->setValue(key("zoneFile"), zoneFile_->text());
  s->setValue(key("strands"), strands_->text());
  s->setValue(key("bufferKm"), buffer_->value());
  s->setValue(key("lat"), lat_->value());
  s->setValue(key("lon"), lon_->value());
  s->setValue(key("radiusKm"), radius_->value());
  s->setValue(key("useStart"), useStart_->isChecked());
  s->setValue(key("start"), start_->date());
  s->setValue(key("homogenize"), homogenize_->isChecked());
  s->setValue(key("conversion"), conversion_->text());
  s->setValue(key("decluster"), decluster_->isChecked());
  s->setValue(key("mc"), mc_->value());
  s->setValue(key("magBin"), magBin_->value());
  s->setValue(key("window"), window_->value());
  s->setValue(key("windowUnit"), windowUnit_->currentIndex());
  s->setValue(key("step"), step_->value());
  s->setValue(key("stepUnit"), stepUnit_->currentIndex());
  s->setValue(key("sigma"), sigma_->value());
  s->setValue(key("ma"), ma_->text());
  s->setValue(key("maCombined"), maCombined_->isChecked());
  s->setValue(key("bollinger"), bollinger_->isChecked());
  s->setValue(key("bbWindow"), bbWindow_->value());
  s->setValue(key("bbK"), bbK_->value());
  s->setValue(key("labelMag"), labelMag_->value());
  s->setValue(key("labelDays"), labelDays_->value());
  s->setValue(key("outRoot"), outRoot_->text());
  s->setValue(key("plots"), plots_->isChecked());
  s->setValue(key("dpi"), dpi_->value());
}

bool IndicatorsWidget::options(cbi::RunOptions *o, QString *err) const {
  o->catalogPath = catalog_->text().trimmed();
  if (o->catalogPath.isEmpty() || !QFileInfo::exists(o->catalogPath)) {
    *err = tr("Choose a catalog file first.");
    return false;
  }
  using Zone = cbi::CatalogSelection::Zone;
  const Zone zones[4] = {Zone::None, Zone::Eafz, Zone::GeoJson, Zone::Circle};
  o->selection.zone = zones[std::clamp(zone_->currentIndex(), 0, 3)];
  o->selection.zoneFile = zoneFile_->text().trimmed();
  if (o->selection.zone == Zone::GeoJson && o->selection.zoneFile.isEmpty()) {
    *err = tr("Choose the fault GeoJSON file.");
    return false;
  }
  o->selection.strands.clear();
  for (const QString &s : strands_->text().split(QLatin1Char(',')))
    if (!s.trimmed().isEmpty())
      o->selection.strands << s.trimmed();
  o->selection.bufferKm = buffer_->value();
  o->selection.centerLat = lat_->value();
  o->selection.centerLon = lon_->value();
  o->selection.radiusKm = radius_->value();
  if (useStart_->isChecked())
    o->selection.start =
        QDateTime(start_->date(), QTime(0, 0), QTimeZone::utc());
  else
    o->selection.start.reset();
  o->selection.homogenize = homogenize_->isChecked();
  o->selection.conversionFile = conversion_->text().trimmed();
  o->selection.decluster = decluster_->isChecked();
  o->config.mc = mc_->value();
  o->config.magBin = magBin_->value();
  o->config.window = cbi::Duration{window_->value(), unitAt(windowUnit_)};
  o->config.step = cbi::Duration{step_->value(), unitAt(stepUnit_)};
  o->config.halfGaussSigma = sigma_->value();
  o->maWindows.clear();
  for (const QString &s : ma_->text().split(QLatin1Char(','))) {
    if (s.trimmed().isEmpty())
      continue;
    bool ok = false;
    const int w = s.trimmed().toInt(&ok);
    if (!ok || w <= 0) {
      *err = tr("Moving averages: comma-separated positive counts, e.g. "
                "7,15,23.");
      return false;
    }
    o->maWindows.push_back(w);
  }
  o->maCombined = maCombined_->isChecked();
  o->bollinger = bollinger_->isChecked();
  o->bbWindow = bbWindow_->value();
  o->bbK = bbK_->value();
  o->labelMag = labelMag_->value();
  o->labelDays = labelDays_->value();
  o->outRoot = outRoot_->text().trimmed();
  if (o->outRoot.isEmpty()) {
    *err = tr("Choose the output root folder.");
    return false;
  }
  o->plots = plots_->isChecked();
  o->dpi = dpi_->value();
  o->lang =
      ctx_.settings->value(QStringLiteral("ui/language"), QStringLiteral("tr"))
          .toString();
  return true;
}

void IndicatorsWidget::browse(QLineEdit *edit, const QString &filter,
                              bool dir) {
  const QString startAt = edit->text().isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(edit->text()).path();
  const QString path =
      dir ? QFileDialog::getExistingDirectory(this, QString(), startAt)
          : QFileDialog::getOpenFileName(this, QString(), startAt, filter);
  if (!path.isEmpty()) {
    edit->setText(path);
    saveSettings();
  }
}

void IndicatorsWidget::describeCatalog() {
  cbi::RunOptions o;
  QString err;
  if (!options(&o, &err)) {
    say(err, true);
    return;
  }
  saveSettings();
  try {
    QStringList log;
    const cbi::Catalog all = cbi::loadCatalog(o.catalogPath);
    const cbi::Catalog sel = o.selection.apply(all, &log);
    log_->appendPlainText(tr("Catalog: %n event(s)", nullptr, int(all.size())));
    for (const QString &l : log + cbi::describeCatalog(sel, o.lang))
      log_->appendPlainText(l);
    say(tr("%n event(s) selected.", nullptr, int(sel.size())));
  } catch (const std::exception &e) {
    say(QString::fromUtf8(e.what()), true);
  }
}

void IndicatorsWidget::estimateMc() {
  cbi::RunOptions o;
  QString err;
  if (!options(&o, &err)) {
    say(err, true);
    return;
  }
  try {
    const cbi::Catalog sel = o.selection.apply(cbi::loadCatalog(o.catalogPath));
    if (sel.empty()) {
      say(tr("No event matches the selection."), true);
      return;
    }
    const double mc = cbi::maxCurvatureMc(sel.magnitudes(), o.config.magBin);
    mc_->setValue(mc);
    saveSettings();
    say(tr("Mc by maximum curvature + 0.2: %1 (%n event(s))", nullptr,
           int(sel.size()))
            .arg(QString::number(mc, 'f', 2)));
  } catch (const std::exception &e) {
    say(QString::fromUtf8(e.what()), true);
  }
}

void IndicatorsWidget::fitConversion() {
  if (running_)
    return;
  const QString national = QFileDialog::getOpenFileName(
      this, tr("National AFAD catalog to calibrate on"),
      QFileInfo(catalog_->text()).path(),
      tr("Catalogs (*.csv *.csv.gz);;All files (*)"));
  if (national.isEmpty())
    return;
  const QString out = QFileDialog::getSaveFileName(
      this, tr("Save the conversion as"),
      QFileInfo(national).path() + QStringLiteral("/afad_magnitude_map.json"),
      tr("JSON (*.json)"));
  if (out.isEmpty())
    return;
  log_->clear();
  auto *job =
      new TaskJob(tr("Magnitude calibration"), QStringLiteral("cbi"),
                  tr("Rate matching on %1").arg(QFileInfo(national).fileName()),
                  [national, out](TaskContext &ctx) -> int {
                    try {
                      QStringList log;
                      const cbi::MagnitudeConversion conv =
                          cbi::calibrateAfadConversion(
                              cbi::loadCatalog(national), 20, &log);
                      for (const QString &l : log)
                        ctx.log(l);
                      if (!conv.save(out)) {
                        ctx.log(QStringLiteral("cannot write ") + out);
                        return 1;
                      }
                      ctx.log(QStringLiteral("[OK] ") + out);
                      return 0;
                    } catch (const std::exception &e) {
                      ctx.log(QString::fromUtf8(e.what()));
                      return 1;
                    }
                  });
  running_ = ctx_.jobs->add(job);
  connect(running_, &Job::lineReceived, this,
          [this](const QString &l) { log_->appendPlainText(l); });
  connect(running_, &Job::stateChanged, this, [this, out] {
    if (running_ && running_->state() != Job::State::Running) {
      if (running_->state() == Job::State::Done) {
        conversion_->setText(out);
        homogenize_->setChecked(true);
        saveSettings();
      }
      running_ = nullptr;
      updateControls();
    }
  });
  updateControls();
}

void IndicatorsWidget::startRun() {
  if (running_)
    return;
  cbi::RunOptions o;
  QString err;
  if (!options(&o, &err)) {
    say(err, true);
    return;
  }
  saveSettings();
  log_->clear();
  std::shared_ptr<IndicatorsState> state = state_;
  auto *job = new TaskJob(
      tr("Indicators %1").arg(QFileInfo(o.catalogPath).completeBaseName()),
      QStringLiteral("cbi"),
      tr("%1, window %2, step %3")
          .arg(o.selection.tag(), o.config.window.text(), o.config.step.text()),
      [o, state](TaskContext &ctx) -> int {
        cbi::RunHooks hooks;
        hooks.log = [&](const QString &m) { ctx.log(m); };
        hooks.shouldStop = [&] { return ctx.cancelled(); };
        hooks.progress = [&](int done, int total, const QString &what) {
          if (total > 0)
            ctx.progress(double(done) / total, what);
        };
        try {
          state->result = cbi::runIndicators(o, hooks);
          state->hasResult = true;
          return state->result.stopped ? 1 : 0;
        } catch (const std::exception &e) {
          ctx.log(QStringLiteral("[ERROR] ") + QString::fromUtf8(e.what()));
          return 1;
        }
      });
  running_ = ctx_.jobs->add(job);
  connect(running_, &Job::lineReceived, this,
          [this](const QString &l) { log_->appendPlainText(l); });
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running) {
      const bool ok = running_->state() == Job::State::Done;
      running_ = nullptr;
      if (state_->hasResult) {
        results_->refresh();
        if (ok)
          tabs_->setCurrentIndex(1);
      }
      say(ok ? tr("Done: %1").arg(state_->result.outDir)
             : tr("The run did not finish; see the log."),
          !ok);
      updateControls();
    }
  });
  say(tr("Running…"));
  updateControls();
}

void IndicatorsWidget::stop() {
  if (running_)
    running_->cancel();
}

void IndicatorsWidget::updateControls() {
  const bool idle = running_ == nullptr;
  run_->setEnabled(idle);
  fit_->setEnabled(idle);
  stop_->setEnabled(!idle);
  const int z = zone_->currentIndex();
  zoneFile_->setEnabled(z == 2);
  zoneBrowse_->setEnabled(z == 2);
  strands_->setEnabled(z == 1 || z == 2);
  buffer_->setEnabled(z == 1 || z == 2);
  circleRow_->setEnabled(z == 3);
  start_->setEnabled(useStart_->isChecked());
  conversion_->setEnabled(homogenize_->isChecked());
  conversionBrowse_->setEnabled(homogenize_->isChecked());
  bbRow_->setEnabled(bollinger_->isChecked());
  dpi_->setEnabled(plots_->isChecked());
}

void IndicatorsWidget::say(const QString &message, bool warning) {
  status_->setText(message);
  status_->setStyleSheet(warning ? QStringLiteral("color: #b00020;")
                                 : QString());
}

} // namespace skg
