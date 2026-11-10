#include "modules/analysis/analysismodule.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "analysis/label.h"
#include "analysis/regions.h"
#include "modules/analysis/catalogpane.h"
#include "modules/analysis/figurespane.h"
#include "modules/analysis/gridpane.h"
#include "modules/analysis/labelspane.h"
#include "modules/analysis/regionspane.h"

namespace skg {

namespace {
const QString kTable = QStringLiteral("analysis/table");
const QString kCatalog = QStringLiteral("analysis/catalog");
const QString kCatalogTz = QStringLiteral("analysis/catalogTz");
const QString kStations = QStringLiteral("analysis/stations");
const QString kFaults = QStringLiteral("analysis/faults");
const QString kEmbeddedStations = QStringLiteral(":/tdvms/data/stations.csv");
} // namespace

QWidget *AnalysisModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new AnalysisWidget(ctx, parent);
}

AnalysisWidget::AnalysisWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::make_shared<AnalysisState>()) {
  auto browse = [this](QLineEdit *&edit, QPushButton *&button) {
    edit = new QLineEdit(this);
    button = new QPushButton(QStringLiteral("…"), this);
    button->setFixedWidth(32);
    auto *w = new QWidget(this);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit, 1);
    l->addWidget(button);
    return w;
  };
  tableLabel_ = new QLabel(this);
  catalogLabel_ = new QLabel(this);
  stationsLabel_ = new QLabel(this);
  faultsLabel_ = new QLabel(this);
  stationLabel_ = new QLabel(this);
  QWidget *tableRow = browse(table_, tableBrowse_);
  QWidget *catalogRow = browse(catalog_, catalogBrowse_);
  catalogTz_ = new QComboBox(this);
  catalogTz_->addItem(QString(), 0.0);
  catalogTz_->addItem(QString(), 3.0);
  auto *catalogLine = new QWidget(this);
  {
    auto *l = new QHBoxLayout(catalogLine);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(catalogRow, 1);
    l->addWidget(catalogTz_);
  }
  QWidget *stationsRow = browse(stations_, stationsBrowse_);
  QWidget *faultsRow = browse(faults_, faultsBrowse_);
  stationCode_ = new QLineEdit(this);
  stationCode_->setMaximumWidth(90);
  manualPosition_ = new QCheckBox(this);
  lat_ = new QDoubleSpinBox(this);
  lat_->setRange(-90, 90);
  lat_->setDecimals(5);
  lon_ = new QDoubleSpinBox(this);
  lon_->setRange(-180, 180);
  lon_->setDecimals(5);
  auto *stationLine = new QWidget(this);
  {
    auto *l = new QHBoxLayout(stationLine);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(stationCode_);
    l->addWidget(manualPosition_);
    l->addWidget(lat_);
    l->addWidget(lon_);
    l->addStretch(1);
  }
  status_ = new QLabel(this);
  status_->setWordWrap(true);
  auto *form = new QFormLayout;
  form->addRow(tableLabel_, tableRow);
  form->addRow(catalogLabel_, catalogLine);
  form->addRow(stationsLabel_, stationsRow);
  form->addRow(stationLabel_, stationLine);
  form->addRow(faultsLabel_, faultsRow);
  form->addRow(QString(), status_);

  catalogPane_ = new CatalogPane(state_, this);
  labelsPane_ = new LabelsPane(ctx_, state_, this);
  figuresPane_ = new FiguresPane(ctx_, state_, this);
  gridPane_ = new GridPane(ctx_, state_, this);
  regionsPane_ = new RegionsPane(ctx_, state_, this);
  tabs_ = new QTabWidget(this);
  tabs_->addTab(catalogPane_, QString());
  tabs_->addTab(labelsPane_, QString());
  tabs_->addTab(figuresPane_, QString());
  tabs_->addTab(gridPane_, QString());
  tabs_->addTab(regionsPane_, QString());
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(tabs_, 1);

  // --- behaviour ----------------------------------------------------------
  connect(tableBrowse_, &QPushButton::clicked, this, [this] {
    const QString start = table_->text().isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(table_->text()).absolutePath();
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose a feature table"), start,
        tr("Feature tables (*.npy *.csv);;All files (*)"));
    if (!p.isEmpty()) {
      table_->setText(p);
      loadTable();
    }
  });
  connect(table_, &QLineEdit::editingFinished, this,
          &AnalysisWidget::loadTable);
  connect(catalogBrowse_, &QPushButton::clicked, this, [this] {
    const QString start = catalog_->text().isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(catalog_->text()).absolutePath();
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose the earthquake catalog"), start,
        tr("Catalogs (*.csv *.csv.gz);;All files (*)"));
    if (!p.isEmpty()) {
      catalog_->setText(p);
      loadCatalog();
    }
  });
  connect(catalog_, &QLineEdit::editingFinished, this,
          &AnalysisWidget::loadCatalog);
  connect(catalogTz_, &QComboBox::currentIndexChanged, this, [this] {
    ctx_.settings->setValue(kCatalogTz, catalogTz_->currentData().toDouble());
    loadCatalog();
  });
  connect(stationsBrowse_, &QPushButton::clicked, this, [this] {
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose a station list"), QDir::homePath(),
        tr("Station lists (*.csv);;All files (*)"));
    if (!p.isEmpty()) {
      stations_->setText(p);
      ctx_.settings->setValue(kStations, p);
      resolveStation();
    }
  });
  connect(stations_, &QLineEdit::editingFinished, this, [this] {
    ctx_.settings->setValue(kStations, stations_->text());
    resolveStation();
  });
  connect(stationCode_, &QLineEdit::editingFinished, this,
          &AnalysisWidget::resolveStation);
  connect(manualPosition_, &QCheckBox::toggled, this, [this](bool on) {
    lat_->setEnabled(on);
    lon_->setEnabled(on);
    resolveStation();
  });
  connect(lat_, &QDoubleSpinBox::editingFinished, this,
          &AnalysisWidget::resolveStation);
  connect(lon_, &QDoubleSpinBox::editingFinished, this,
          &AnalysisWidget::resolveStation);
  connect(faultsBrowse_, &QPushButton::clicked, this, [this] {
    const QString p = QFileDialog::getOpenFileName(
        this, tr("Choose the fault shapefile"), QDir::homePath(),
        tr("Shapefiles (*.shp);;All files (*)"));
    if (!p.isEmpty()) {
      faults_->setText(p);
      loadFaults();
    }
  });
  connect(faults_, &QLineEdit::editingFinished, this,
          &AnalysisWidget::loadFaults);

  retranslate();
  lat_->setEnabled(false);
  lon_->setEnabled(false);
  state_->lang =
      ctx_.settings->value(QStringLiteral("ui/language"), QStringLiteral("tr"))
          .toString();
  catalogTz_->setCurrentIndex(
      catalogTz_->findData(ctx_.settings->value(kCatalogTz, 0.0).toDouble()));
  stations_->setText(ctx_.settings->value(kStations).toString());
  catalog_->setText(ctx_.settings->value(kCatalog).toString());
  faults_->setText(ctx_.settings->value(kFaults).toString());
  table_->setText(ctx_.settings->value(kTable).toString());
  if (!catalog_->text().isEmpty())
    loadCatalog();
  if (!faults_->text().isEmpty())
    loadFaults();
  if (!table_->text().isEmpty())
    loadTable();
}

void AnalysisWidget::say(const QString &message, bool warning) {
  status_->setText(message);
  status_->setStyleSheet(warning ? QStringLiteral("color: #a00;") : QString());
}

void AnalysisWidget::loadTable() {
  const QString path = table_->text().trimmed();
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    state_->table = {};
    state_->tablePath.clear();
    recompute();
    return;
  }
  QGuiApplication::setOverrideCursor(Qt::WaitCursor);
  try {
    state_->table = analysis::loadTable(path);
    state_->tablePath = path;
    ctx_.settings->setValue(kTable, path);
    // The fx run metadata beside the table carries the station and window.
    QString meta = path;
    meta.replace(QRegularExpression(QStringLiteral(
                     "_(chaos_)?features(_etiketli)?\\.(npy|csv)$")),
                 QStringLiteral("_run_metadata.json"));
    QFile f(meta);
    if (meta != path && f.open(QIODevice::ReadOnly)) {
      const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      const QString station =
          o.value(QStringLiteral("station"))
              .toString(o.value(QStringLiteral("STATION")).toString());
      if (!station.isEmpty())
        stationCode_->setText(station);
      const double win =
          o.value(QStringLiteral("win_sec"))
              .toDouble(o.value(QStringLiteral("WIN_SEC")).toDouble(200));
      state_->winSec = win > 0 ? win : 200;
    }
    if (stationCode_->text().isEmpty()) {
      // SCAP-style tables sit under <results>/<STATION>/<event>/<channel>/.
      const QStringList parts =
          QFileInfo(path).absolutePath().split('/', Qt::SkipEmptyParts);
      if (parts.size() >= 3 && parts.last().size() == 1)
        stationCode_->setText(parts[parts.size() - 3]);
    }
    say(tr("%1: %n window(s), %2 columns", nullptr, state_->table.rows)
            .arg(QFileInfo(path).fileName())
            .arg(state_->table.fields.size()));
  } catch (const std::exception &e) {
    state_->table = {};
    state_->tablePath.clear();
    say(tr("Cannot read the table: %1").arg(QString::fromUtf8(e.what())), true);
  }
  QGuiApplication::restoreOverrideCursor();
  resolveStation(); // recomputes
}

void AnalysisWidget::loadCatalog() {
  const QString path = catalog_->text().trimmed();
  state_->events.clear();
  state_->catalogPath.clear();
  state_->catalogFile.clear();
  if (!path.isEmpty() && QFileInfo::exists(path)) {
    state_->catalogFile = path;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    try {
      state_->catalogOffsetHours = catalogTz_->currentData().toDouble();
      state_->events = analysis::loadEvents(path, state_->catalogOffsetHours);
      state_->catalogPath = path;
      ctx_.settings->setValue(kCatalog, path);
      say(tr("Catalog: %n event(s)", nullptr, int(state_->events.size())));
    } catch (const std::exception &e) {
      // A table of coordinates alone is still a catalog for Regions.
      try {
        const int n = analysis::loadPointTable(path).size();
        ctx_.settings->setValue(kCatalog, path);
        say(tr("%1 has no Date and Magnitude columns: its %n epicentre(s) "
               "can be split into regions, the other panes need those "
               "columns.",
               nullptr, n)
                .arg(QFileInfo(path).fileName()));
      } catch (const std::exception &) {
        state_->catalogFile.clear();
        say(tr("Cannot read the catalog: %1").arg(QString::fromUtf8(e.what())),
            true);
      }
    }
    QGuiApplication::restoreOverrideCursor();
  }
  recompute();
}

void AnalysisWidget::loadFaults() {
  const QString path = faults_->text().trimmed();
  state_->faults = analysis::FaultIndex();
  state_->faultsPath.clear();
  if (!path.isEmpty() && QFileInfo::exists(path)) {
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    try {
      state_->faults = analysis::loadFaults(path);
      state_->faultsPath = path;
      ctx_.settings->setValue(kFaults, path);
      say(tr("Faults: %n segment(s)", nullptr,
             int(state_->faults.faults().size())));
    } catch (const std::exception &e) {
      say(tr("Cannot read the shapefile: %1").arg(QString::fromUtf8(e.what())),
          true);
    }
    QGuiApplication::restoreOverrideCursor();
  }
  recompute();
}

void AnalysisWidget::resolveStation() {
  state_->station = stationCode_->text().trimmed().toUpper();
  state_->position.reset();
  if (manualPosition_->isChecked()) {
    analysis::Station s;
    s.code = state_->station;
    s.lat = lat_->value();
    s.lon = lon_->value();
    state_->position = s;
  } else if (!state_->station.isEmpty()) {
    const QString list = stations_->text().trimmed().isEmpty()
                             ? kEmbeddedStations
                             : stations_->text().trimmed();
    try {
      const auto stations = analysis::loadStations(list);
      state_->position = analysis::findStation(stations, state_->station);
      if (state_->position) {
        const QSignalBlocker b1(lat_), b2(lon_);
        lat_->setValue(state_->position->lat);
        lon_->setValue(state_->position->lon);
      } else
        say(tr("Station %1 is not in %2; set its position by hand.")
                .arg(state_->station, list),
            true);
    } catch (const std::exception &e) {
      say(tr("Cannot read the station list: %1")
              .arg(QString::fromUtf8(e.what())),
          true);
    }
  }
  recompute();
}

void AnalysisWidget::recompute() {
  AnalysisState &s = *state_;
  s.spanStart = s.spanEnd = 0;
  s.eventsAtStation.clear();
  s.mainEvent.reset();
  if (s.hasTable()) {
    double lo = 1e18, hi = -1e18;
    for (int r = 0; r < s.table.rows; ++r)
      if (const auto span = analysis::windowSpan(s.table, r, s.winSec)) {
        lo = std::min(lo, span->start);
        hi = std::max(hi, span->end);
      }
    if (hi > lo) {
      s.spanStart = lo;
      s.spanEnd = hi;
    }
  }
  if (s.position && !s.events.empty()) {
    std::vector<analysis::Event> inRange;
    if (s.spanEnd > s.spanStart) {
      // Whole UTC days, as SCAP (the last day included).
      analysis::EventQuery q;
      q.startEpoch = std::floor(s.spanStart / 86400.0) * 86400.0;
      q.endEpoch = std::ceil(s.spanEnd / 86400.0) * 86400.0;
      inRange = analysis::selectEvents(s.events, q);
    } else
      inRange = s.events;
    s.eventsAtStation =
        analysis::eventsForStation(inRange, s.position->lat, s.position->lon);
    s.mainEvent = analysis::mainEvent(s.eventsAtStation);
  }
  refreshPanes();
}

void AnalysisWidget::refreshPanes() {
  catalogPane_->refresh();
  labelsPane_->refresh();
  figuresPane_->refresh();
  gridPane_->refresh();
  regionsPane_->refresh();
}

void AnalysisWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    state_->lang =
        ctx_.settings
            ->value(QStringLiteral("ui/language"), QStringLiteral("tr"))
            .toString();
    retranslate();
    refreshPanes();
  }
  QWidget::changeEvent(e);
}

void AnalysisWidget::retranslate() {
  tableLabel_->setText(tr("Feature table"));
  catalogLabel_->setText(tr("Earthquake catalog"));
  stationsLabel_->setText(tr("Station list"));
  stationLabel_->setText(tr("Station"));
  faultsLabel_->setText(tr("Fault lines"));
  table_->setPlaceholderText(
      tr("<name>_features.npy or .csv from the Features tab (or a SCAP CSV)"));
  catalog_->setPlaceholderText(tr("AFAD catalog export, CSV or .gz"));
  stations_->setPlaceholderText(tr("empty: the built-in AFAD station list"));
  faults_->setPlaceholderText(
      tr("MTA active fault shapefile (.shp), optional"));
  stationCode_->setPlaceholderText(tr("code"));
  manualPosition_->setText(tr("position:"));
  catalogTz_->setItemText(0, tr("times in UTC"));
  catalogTz_->setItemText(1, tr("times in Turkey time (UTC+3)"));
  tabs_->setTabText(0, tr("Catalog"));
  tabs_->setTabText(1, tr("Labels"));
  tabs_->setTabText(2, tr("Figures"));
  tabs_->setTabText(3, tr("Grid"));
  tabs_->setTabText(4, tr("Regions"));
}

} // namespace skg
