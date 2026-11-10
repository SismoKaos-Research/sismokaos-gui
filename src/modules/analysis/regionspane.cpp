#include "modules/analysis/regionspane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <initializer_list>

#include "analysis/regionmap.h"
#include "analysis/shapefile.h"
#include "cbi/zones.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/analysis/figureview.h"

namespace skg {

using namespace analysis;

namespace {
QString key(const char *name) {
  return QStringLiteral("analysis/regions/") + QLatin1String(name);
}

// The GEM traces shipped with the indicators library, as map lines.
std::shared_ptr<const std::vector<FaultLine>> shippedFaults() {
  static const auto lines = [] {
    auto out = std::make_shared<std::vector<FaultLine>>();
    for (const cbi::FaultZone::Segment &s : cbi::eafz().segments)
      out->push_back(
          {{QPointF(s.lon0, s.lat0), QPointF(s.lon1, s.lat1)}, QString()});
    return std::shared_ptr<const std::vector<FaultLine>>(out);
  }();
  return lines;
}
} // namespace

QLabel *RegionsPane::label(const char *source) {
  auto *l = new QLabel(this);
  labels_.emplace_back(l, source);
  return l;
}

RegionsPane::RegionsPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
                         QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::move(state)) {
  auto row = [this](std::initializer_list<QWidget *> widgets) {
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    for (QWidget *x : widgets)
      l->addWidget(x, qobject_cast<QLineEdit *>(x) ? 1 : 0);
    return w;
  };
  auto browseButton = [this] {
    auto *b = new QPushButton(QStringLiteral("…"), this);
    b->setFixedWidth(32);
    return b;
  };

  // --- method ---------------------------------------------------------------
  methods_ = new QComboBox(this);
  methods_->addItem(QString());
  methods_->addItem(QString());
  methods_->addItem(QString());
  regions_ = new QSpinBox(this);
  regions_->setRange(1, 20);
  regions_->setValue(5);
  gridRows_ = new QSpinBox(this);
  gridRows_->setRange(2, 40);
  gridRows_->setValue(10);
  gridCols_ = new QSpinBox(this);
  gridCols_->setRange(2, 40);
  gridCols_->setValue(10);
  iterations_ = new QSpinBox(this);
  iterations_->setRange(100, 1000000);
  iterations_->setSingleStep(1000);
  iterations_->setValue(5000);
  quantile_ = new QDoubleSpinBox(this);
  quantile_->setRange(0.01, 1.0);
  quantile_->setSingleStep(0.05);
  quantile_->setDecimals(2);
  quantile_->setValue(0.15);
  seed_ = new QSpinBox(this);
  seed_->setRange(0, 2147483647);
  seed_->setValue(42);
  methodBox_ = new QGroupBox(this);
  auto *methodForm = new QFormLayout(methodBox_);
  methodForm->addRow(label(QT_TR_NOOP("Methods")), methods_);
  methodForm->addRow(label(QT_TR_NOOP("Regions (SOM)")), regions_);
  methodForm->addRow(
      label(QT_TR_NOOP("Map before Mean Shift")),
      row({gridRows_, new QLabel(QStringLiteral("×"), this), gridCols_}));
  methodForm->addRow(label(QT_TR_NOOP("Mean Shift quantile")), quantile_);
  methodForm->addRow(label(QT_TR_NOOP("Training iterations")), iterations_);
  methodForm->addRow(label(QT_TR_NOOP("Random seed")), seed_);

  // --- maps -----------------------------------------------------------------
  area_ = new QLineEdit(this);
  marker_ = new QCheckBox(this);
  markerLat_ = new QDoubleSpinBox(this);
  markerLat_->setRange(-90, 90);
  markerLat_->setDecimals(4);
  markerLon_ = new QDoubleSpinBox(this);
  markerLon_->setRange(-180, 180);
  markerLon_->setDecimals(4);
  markerLabel_ = new QLineEdit(this);
  provincesEdit_ = new QLineEdit(this);
  provincesBrowse_ = browseButton();
  citiesEdit_ = new QLineEdit(this);
  citiesBrowse_ = browseButton();
  showCountries_ = new QCheckBox(this);
  showCountries_->setChecked(true);
  showFaults_ = new QCheckBox(this);
  showFaults_->setChecked(true);
  showCities_ = new QCheckBox(this);
  showCities_->setChecked(true);
  maps_ = new QCheckBox(this);
  maps_->setChecked(true);
  dpi_ = new QSpinBox(this);
  dpi_->setRange(50, 600);
  dpi_->setSingleStep(50);
  dpi_->setValue(200);
  svg_ = new QCheckBox(this);
  mapBox_ = new QGroupBox(this);
  auto *mapForm = new QFormLayout(mapBox_);
  mapForm->addRow(label(QT_TR_NOOP("Area name")), area_);
  mapForm->addRow(marker_, row({markerLat_, markerLon_, markerLabel_}));
  mapForm->addRow(label(QT_TR_NOOP("Provinces")),
                  row({provincesEdit_, provincesBrowse_}));
  mapForm->addRow(label(QT_TR_NOOP("Cities")),
                  row({citiesEdit_, citiesBrowse_}));
  mapForm->addRow(label(QT_TR_NOOP("Layers")),
                  row({showCountries_, showFaults_, showCities_}));
  mapForm->addRow(maps_, row({dpi_, svg_}));

  outDir_ = new QLineEdit(this);
  outBrowse_ = browseButton();
  start_ = new QPushButton(this);
  stop_ = new QPushButton(this);
  open_ = new QPushButton(this);
  catalog_ = new QLabel(this);
  catalog_->setWordWrap(true);
  status_ = new QLabel(this);
  status_->setWordWrap(true);
  auto *outForm = new QFormLayout;
  outForm->addRow(label(QT_TR_NOOP("Output folder")),
                  row({outDir_, outBrowse_}));
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(start_);
  buttons->addWidget(stop_);
  buttons->addStretch(1);
  buttons->addWidget(open_);

  auto *settings = new QWidget(this);
  auto *settingsLayout = new QVBoxLayout(settings);
  settingsLayout->addWidget(catalog_);
  settingsLayout->addWidget(methodBox_);
  settingsLayout->addWidget(mapBox_);
  settingsLayout->addLayout(outForm);
  settingsLayout->addLayout(buttons);
  settingsLayout->addWidget(status_);
  settingsLayout->addStretch(1);
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setMinimumWidth(440);
  scroll->setWidget(settings);

  // --- results --------------------------------------------------------------
  table_ = new QTableWidget(this);
  table_->setColumnCount(7);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->verticalHeader()->setVisible(false);
  table_->horizontalHeader()->setStretchLastSection(true);
  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(2000);
  mapChoice_ = new QComboBox(this);
  view_ = new FigureView(this);
  auto *mapPane = new QWidget(this);
  auto *mapLayout = new QVBoxLayout(mapPane);
  mapLayout->setContentsMargins(0, 0, 0, 0);
  mapLayout->addWidget(mapChoice_);
  mapLayout->addWidget(view_, 1);
  auto *top = new QSplitter(Qt::Horizontal, this);
  top->addWidget(table_);
  top->addWidget(log_);
  top->setStretchFactor(0, 3);
  top->setStretchFactor(1, 2);
  auto *right = new QSplitter(Qt::Vertical, this);
  right->addWidget(top);
  right->addWidget(mapPane);
  right->setStretchFactor(0, 1);
  right->setStretchFactor(1, 4);
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(scroll);
  split->addWidget(right);
  split->setStretchFactor(0, 0);
  split->setStretchFactor(1, 1);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  // --- behaviour ------------------------------------------------------------
  connect(methods_, &QComboBox::currentIndexChanged, this,
          &RegionsPane::updateControls);
  connect(marker_, &QCheckBox::toggled, this, &RegionsPane::updateControls);
  connect(maps_, &QCheckBox::toggled, this, &RegionsPane::updateControls);
  connect(provincesBrowse_, &QPushButton::clicked, this, [this] {
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose the province boundaries"), QDir::homePath(),
        tr("Shapefiles (*.shp);;All files (*)"));
    if (!p.isEmpty())
      provincesEdit_->setText(p);
  });
  connect(citiesBrowse_, &QPushButton::clicked, this, [this] {
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose a city list"), QDir::homePath(),
        tr("City lists (*.csv);;All files (*)"));
    if (!p.isEmpty())
      citiesEdit_->setText(p);
  });
  connect(outBrowse_, &QPushButton::clicked, this, [this] {
    const QString p = QFileDialog::getExistingDirectory(
        this, tr("Choose the output folder"),
        outDir_->text().isEmpty() ? QDir::homePath() : outDir_->text());
    if (!p.isEmpty())
      outDir_->setText(p);
  });
  connect(start_, &QPushButton::clicked, this, &RegionsPane::run);
  connect(stop_, &QPushButton::clicked, this, [this] {
    if (running_)
      running_->cancel();
  });
  connect(open_, &QPushButton::clicked, this, [this] {
    const QString dir =
        run_ && run_->done ? run_->result.outDir : outDir_->text().trimmed();
    if (!dir.isEmpty() && QFileInfo(dir).isDir())
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  connect(mapChoice_, &QComboBox::currentIndexChanged, this,
          &RegionsPane::showMap);

  // --- saved settings -------------------------------------------------------
  QSettings &s = *ctx_.settings;
  methods_->setCurrentIndex(
      std::clamp(s.value(key("methods"), 0).toInt(), 0, 2));
  regions_->setValue(s.value(key("regions"), 5).toInt());
  gridRows_->setValue(s.value(key("gridRows"), 10).toInt());
  gridCols_->setValue(s.value(key("gridCols"), 10).toInt());
  iterations_->setValue(s.value(key("iterations"), 5000).toInt());
  quantile_->setValue(s.value(key("quantile"), 0.15).toDouble());
  seed_->setValue(s.value(key("seed"), 42).toInt());
  area_->setText(s.value(key("area")).toString());
  marker_->setChecked(s.value(key("marker"), false).toBool());
  markerLat_->setValue(s.value(key("markerLat"), 0.0).toDouble());
  markerLon_->setValue(s.value(key("markerLon"), 0.0).toDouble());
  markerLabel_->setText(s.value(key("markerLabel")).toString());
  provincesEdit_->setText(s.value(key("provinces")).toString());
  citiesEdit_->setText(s.value(key("cities")).toString());
  showCountries_->setChecked(s.value(key("showCountries"), true).toBool());
  showFaults_->setChecked(s.value(key("showFaults"), true).toBool());
  showCities_->setChecked(s.value(key("showCities"), true).toBool());
  maps_->setChecked(s.value(key("maps"), true).toBool());
  dpi_->setValue(s.value(key("dpi"), 200).toInt());
  svg_->setChecked(s.value(key("svg"), false).toBool());
  outDir_->setText(
      s.value(key("outDir"), QDir::home().filePath(QStringLiteral("regions")))
          .toString());
  retranslate();
  updateControls();
}

void RegionsPane::save() {
  QSettings &s = *ctx_.settings;
  s.setValue(key("methods"), methods_->currentIndex());
  s.setValue(key("regions"), regions_->value());
  s.setValue(key("gridRows"), gridRows_->value());
  s.setValue(key("gridCols"), gridCols_->value());
  s.setValue(key("iterations"), iterations_->value());
  s.setValue(key("quantile"), quantile_->value());
  s.setValue(key("seed"), seed_->value());
  s.setValue(key("area"), area_->text());
  s.setValue(key("marker"), marker_->isChecked());
  s.setValue(key("markerLat"), markerLat_->value());
  s.setValue(key("markerLon"), markerLon_->value());
  s.setValue(key("markerLabel"), markerLabel_->text());
  s.setValue(key("provinces"), provincesEdit_->text());
  s.setValue(key("cities"), citiesEdit_->text());
  s.setValue(key("showCountries"), showCountries_->isChecked());
  s.setValue(key("showFaults"), showFaults_->isChecked());
  s.setValue(key("showCities"), showCities_->isChecked());
  s.setValue(key("maps"), maps_->isChecked());
  s.setValue(key("dpi"), dpi_->value());
  s.setValue(key("svg"), svg_->isChecked());
  s.setValue(key("outDir"), outDir_->text());
}

void RegionsPane::say(const QString &message, bool warning) {
  status_->setText(message);
  status_->setStyleSheet(warning ? QStringLiteral("color: #a00;") : QString());
}

void RegionsPane::refresh() {
  const AnalysisState &s = *state_;
  catalog_->setText(
      s.catalogFile.isEmpty()
          ? tr("Load an earthquake catalog at the top; its epicentres are "
               "what is split into regions.")
          : tr("Catalog: %1").arg(QFileInfo(s.catalogFile).fileName()));
  updateControls();
  if (run_ && run_->done) {
    run_->options.lang = s.lang;
    view_->update();
    showResult();
  }
}

void RegionsPane::updateControls() {
  const bool idle = running_ == nullptr;
  const int m = methods_->currentIndex(); // 0 both, 1 SOM, 2 hybrid
  regions_->setEnabled(m != 2);
  gridRows_->setEnabled(m != 1);
  gridCols_->setEnabled(m != 1);
  quantile_->setEnabled(m != 1);
  markerLat_->setEnabled(marker_->isChecked());
  markerLon_->setEnabled(marker_->isChecked());
  markerLabel_->setEnabled(marker_->isChecked());
  dpi_->setEnabled(maps_->isChecked());
  svg_->setEnabled(maps_->isChecked());
  start_->setEnabled(idle && !state_->catalogFile.isEmpty());
  stop_->setEnabled(!idle);
}

bool RegionsPane::options(RegionRunOptions *o, QString *error) {
  const AnalysisState &s = *state_;
  if (s.catalogFile.isEmpty()) {
    *error = tr("Load an earthquake catalog at the top first.");
    return false;
  }
  o->catalogPath = s.catalogFile;
  o->outDir = outDir_->text().trimmed();
  if (o->outDir.isEmpty()) {
    *error = tr("Choose an output folder.");
    return false;
  }
  const int m = methods_->currentIndex();
  o->som = m != 2;
  o->somMeanShift = m != 1;
  o->regions = regions_->value();
  o->gridRows = gridRows_->value();
  o->gridCols = gridCols_->value();
  o->iterations = iterations_->value();
  o->quantile = quantile_->value();
  o->seed = static_cast<quint64>(seed_->value());
  o->areaName = area_->text().trimmed();
  o->lang = s.lang;
  o->maps = maps_->isChecked();
  o->svg = svg_->isChecked();
  o->dpi = dpi_->value();
  if (marker_->isChecked())
    o->marker = MapMarker{markerLat_->value(), markerLon_->value(),
                          markerLabel_->text().trimmed()};
  try {
    const QString provinces = provincesEdit_->text().trimmed();
    if (provinces != provincesPath_) {
      provinces_.reset();
      if (!provinces.isEmpty())
        provinces_ =
            std::make_shared<const ShapeLayer>(readShapefile(provinces));
      provincesPath_ = provinces;
    }
    const QString cities = citiesEdit_->text().trimmed();
    if (cities != citiesPath_ || (cities.isEmpty() && cities_.empty())) {
      cities_ = cities.isEmpty() ? eafzCities() : loadCities(cities);
      citiesPath_ = cities;
    }
  } catch (const std::exception &e) {
    provincesPath_ = QStringLiteral("\n"); // read again next time
    citiesPath_ = QStringLiteral("\n");
    *error = QString::fromUtf8(e.what());
    return false;
  }
  o->provinces = provinces_;
  o->provincesSource = provinces_ ? provincesPath_ : QString();
  if (showCities_->isChecked()) {
    o->cities = cities_;
    o->citiesSource = citiesPath_.isEmpty()
                          ? QStringLiteral("built-in (16 cities, EAFZ)")
                          : citiesPath_;
  }
  if (showCountries_->isChecked()) {
    o->countries = std::shared_ptr<const ShapeLayer>(&shippedCountries(),
                                                     [](const ShapeLayer *) {});
    o->countriesSource =
        QStringLiteral("shipped (Natural Earth 5.1.1, 10m countries, tur)");
  }
  if (showFaults_->isChecked()) {
    if (!s.faults.empty()) {
      o->faults =
          std::make_shared<const std::vector<FaultLine>>(faultLines(s.faults));
      o->faultsSource = s.faultsPath;
    } else {
      o->faults = shippedFaults();
      o->faultsSource = QStringLiteral("shipped (GEM GAF-DB, EAFZ)");
    }
  }
  return true;
}

void RegionsPane::run() {
  if (running_)
    return;
  auto next = std::make_shared<Run>();
  QString error;
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  const bool ok = options(&next->options, &error);
  QGuiApplication::restoreOverrideCursor();
  if (!ok) {
    say(error, true);
    return;
  }
  save();
  log_->clear();
  const RegionRunOptions &o = next->options;
  auto *job = new TaskJob(
      tr("Regions %1").arg(QFileInfo(o.catalogPath).completeBaseName()),
      QStringLiteral("analysis"),
      o.som && o.somMeanShift ? tr("SOM and SOM + Mean Shift")
      : o.som                 ? tr("SOM, %n region(s)", nullptr, o.regions)
                              : tr("SOM + Mean Shift"),
      [next](TaskContext &ctx) -> int {
        RegionRunHooks hooks;
        hooks.log = [&](const QString &m) { ctx.log(m); };
        hooks.shouldStop = [&] { return ctx.cancelled(); };
        hooks.progress = [&](int done, int total, const QString &what) {
          if (total > 0)
            ctx.progress(double(done) / total, what);
        };
        try {
          next->result = runRegions(next->options, hooks);
          next->done = true;
          return next->result.stopped ? 1 : 0;
        } catch (const std::exception &e) {
          ctx.log(QStringLiteral("[ERROR] ") + QString::fromUtf8(e.what()));
          return 1;
        }
      });
  running_ = ctx_.jobs->add(job);
  connect(running_, &Job::lineReceived, this,
          [this](const QString &l) { log_->appendPlainText(l); });
  connect(running_, &Job::stateChanged, this, [this, next] {
    if (!running_ || running_->state() == Job::State::Running ||
        running_->state() == Job::State::Queued)
      return;
    const bool ok = running_->state() == Job::State::Done;
    running_ = nullptr;
    if (next->done)
      run_ = next;
    finished(ok);
  });
  say(tr("Running…"));
  updateControls();
}

void RegionsPane::finished(bool ok) {
  if (run_ && run_->done)
    showResult();
  say(ok ? tr("Done: %1").arg(run_ ? run_->result.outDir : QString())
         : tr("The run did not finish; see the log."),
      !ok);
  updateControls();
}

void RegionsPane::showResult() {
  const QLocale locale;
  const RegionRunResult &r = run_->result;
  int rows = 0;
  for (const Regions &g : r.results)
    rows += static_cast<int>(g.regions.size());
  table_->setRowCount(rows);
  int row = 0;
  for (const Regions &g : r.results)
    for (const RegionSummary &s : g.regions) {
      const QStringList cells{g.method == RegionMethod::Som
                                  ? QStringLiteral("SOM")
                                  : QStringLiteral("SOM–Mean Shift"),
                              QString::number(s.id),
                              QStringLiteral("%1°N, %2°E")
                                  .arg(s.center[0], 0, 'f', 4)
                                  .arg(s.center[1], 0, 'f', 4),
                              QStringLiteral("%1 – %2")
                                  .arg(s.latMin, 0, 'f', 4)
                                  .arg(s.latMax, 0, 'f', 4),
                              QStringLiteral("%1 – %2")
                                  .arg(s.lonMin, 0, 'f', 4)
                                  .arg(s.lonMax, 0, 'f', 4),
                              locale.toString(s.count),
                              QString::fromLatin1("%1_region_%2.csv")
                                  .arg(methodPrefix(g.method))
                                  .arg(s.id)};
      for (int c = 0; c < cells.size(); ++c) {
        auto *item = new QTableWidgetItem(cells[c]);
        if (c == 1 || c == 5)
          item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, c, item);
      }
      ++row;
    }
  table_->resizeColumnsToContents();

  // The maps on offer: the two study-area maps, then one per method.
  const int keep = mapChoice_->currentData().toInt();
  const QSignalBlocker block(mapChoice_);
  mapChoice_->clear();
  for (int i = 0; i < static_cast<int>(r.results.size()); ++i)
    mapChoice_->addItem(r.results[static_cast<size_t>(i)].method ==
                                RegionMethod::Som
                            ? tr("Regions: SOM")
                            : tr("Regions: SOM + Mean Shift"),
                        i);
  mapChoice_->addItem(tr("Study area with faults and boundaries"), -1);
  mapChoice_->addItem(tr("Study area, events only"), -2);
  const int at = mapChoice_->findData(keep);
  mapChoice_->setCurrentIndex(at >= 0 ? at : 0);
  showMap();
}

void RegionsPane::showMap() {
  if (!run_ || !run_->done || !run_->result.table) {
    view_->clearFigure();
    return;
  }
  const std::shared_ptr<Run> run = run_;
  const int which = mapChoice_->currentData().toInt();
  view_->setFigure(
      [run, which](QPainter &p, const QRectF &rect) {
        const Regions *regions =
            which >= 0 && which < static_cast<int>(run->result.results.size())
                ? &run->result.results[static_cast<size_t>(which)]
                : nullptr;
        const RegionMapSpec spec = regionMapSpec(
            run->options, *run->result.table, regions, which != -2);
        drawRegionMap(p, rect, spec);
      },
      regionMapSize(100));
}

void RegionsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void RegionsPane::retranslate() {
  for (const auto &l : labels_)
    l.first->setText(tr(l.second));
  methodBox_->setTitle(tr("Regionalisation"));
  mapBox_->setTitle(tr("Maps"));
  methods_->setItemText(0, tr("SOM and SOM + Mean Shift"));
  methods_->setItemText(1, tr("SOM (a fixed number of regions)"));
  methods_->setItemText(2, tr("SOM + Mean Shift (finds the number)"));
  ui::setHelp(methods_,
              tr("How the epicentres are grouped into regions. SOM (a "
                 "self-organising map) makes the number of regions you "
                 "ask for; SOM + Mean Shift first summarises the catalog "
                 "with a larger map and then finds the number of regions "
                 "itself from where the events are dense."));
  ui::setHelp(regions_,
              tr("For SOM: the number of units of the map. Each unit "
                 "becomes a region if events fall to it, so there can be "
                 "fewer regions than asked for. Default 5."));
  ui::setHelp({gridRows_, gridCols_},
              tr("For SOM + Mean Shift: rows and columns of the map that "
                 "summarises the catalog before Mean Shift groups its "
                 "units. Default 10 × 10."));
  ui::setHelp(quantile_,
              tr("For Mean Shift: the share of the map's units used as "
                 "neighbours when its bandwidth is estimated. A larger "
                 "value gives fewer, larger regions. Default 0.15."));
  ui::setHelp(iterations_,
              tr("How many training steps the self-organising maps take. "
                 "Applies to both methods. Default 5000."));
  ui::setHelp(seed_, tr("Starting value of the random numbers. The same "
                        "seed gives the same regions. Default 42."));
  area_->setPlaceholderText(
      tr("first line of the map titles, e.g. East Anatolian Fault Zone"));
  marker_->setText(tr("Marker"));
  ui::setHelp(area_, tr("The first line of the map titles, e.g. the name "
                        "of the fault zone."));
  ui::setHelp(marker_, tr("Adds one labelled point to the maps, for "
                          "example a station or a city."));
  ui::setHelp(markerLat_, tr("Latitude of the marker in degrees, north "
                             "positive."));
  ui::setHelp(markerLon_, tr("Longitude of the marker in degrees, east "
                             "positive."));
  ui::setHelp(markerLabel_, tr("The text written beside the marker."));
  ui::setHelp({provincesEdit_, provincesBrowse_},
              tr("Optional: a polygon shapefile (.shp) of province "
                 "boundaries, drawn on the maps."));
  ui::setHelp({citiesEdit_, citiesBrowse_},
              tr("Optional: a CSV of lat, lon, name for the city labels. "
                 "An align column with the word right puts a label left "
                 "of its point. Empty uses sixteen cities around the East "
                 "Anatolian Fault."));
  ui::setHelp(showCountries_, tr("Draws the country outlines shipped with "
                                 "the application (Natural Earth)."));
  ui::setHelp(showCities_, tr("Writes the city names on the maps."));
  ui::setHelp(maps_, tr("Writes the maps as PNG pictures into the output "
                        "folder. Unticked, only the region tables are "
                        "written."));
  ui::setHelp(dpi_, tr("Resolution of the PNG maps in dots per inch. Maps "
                       "are 12 × 10 inches, so 200 dpi gives 2400 × 2000 "
                       "pixels."));
  ui::setHelp({outDir_, outBrowse_},
              tr("Where the maps, the catalog of each region, the summary "
                 "table and the run record are written."));
  ui::setHelp(mapChoice_, tr("Which of the run's maps is shown: the study "
                             "area, or the regions of one method."));
  start_->setToolTip(ui::helpText(
      tr("Groups the epicentres of the catalog loaded at the top into "
         "regions and draws the maps, as a job.")));
  stop_->setToolTip(ui::helpText(tr("Stops the run.")));
  markerLabel_->setPlaceholderText(tr("label"));
  provincesEdit_->setPlaceholderText(tr("province polygons (.shp), optional"));
  citiesEdit_->setPlaceholderText(
      tr("CSV of lat, lon, name; empty: 16 cities around the East Anatolian "
         "Fault"));
  showCountries_->setText(tr("countries"));
  showFaults_->setText(tr("faults"));
  ui::setHelp(showFaults_,
              tr("Draws the faults: the shapefile loaded at the top of the "
                 "page, or the shipped East Anatolian traces when none is "
                 "loaded."));
  showCities_->setText(tr("cities"));
  maps_->setText(tr("Write PNG maps"));
  dpi_->setSuffix(tr(" dpi"));
  svg_->setText(tr("and SVG"));
  ui::setHelp(svg_, tr("Also writes an SVG vector copy beside every PNG. "
                       "Large for big catalogs."));
  start_->setText(tr("Run"));
  ui::setPrimary(start_);
  log_->setPlaceholderText(tr("The run log appears here."));
  stop_->setText(tr("Stop"));
  open_->setText(tr("Open output folder"));
  table_->setHorizontalHeaderLabels({tr("Method"), tr("Region"), tr("Centre"),
                                     tr("Latitude"), tr("Longitude"),
                                     tr("Events"), tr("File")});
  log_->setPlaceholderText(tr("The run log appears here."));
  refresh();
}

} // namespace skg
