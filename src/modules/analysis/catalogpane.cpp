#include "modules/analysis/catalogpane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableView>
#include <QTimeZone>
#include <QVBoxLayout>

#include "analysis/map.h"
#include "modules/analysis/figureview.h"

namespace skg {

using namespace analysis;

EventsModel::EventsModel(QObject *parent) : QAbstractTableModel(parent) {}

void EventsModel::setEvents(const std::vector<EventAtStation> &events,
                            const FaultIndex *faults, const QString &lang) {
  beginResetModel();
  rows_.clear();
  lang_ = lang;
  for (const EventAtStation &e : events) {
    Row r{e, {}, {}, -1};
    if (faults && !faults->empty())
      if (const auto n = faults->nearest(e.event.lat, e.event.lon)) {
        r.faultName = n->fault->label();
        r.faultKm = n->km;
        r.faultDir = n->direction(lang);
      }
    rows_.push_back(std::move(r));
  }
  endResetModel();
}

int EventsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : int(rows_.size());
}
int EventsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant EventsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() ||
      (role != Qt::DisplayRole && role != Qt::TextAlignmentRole))
    return {};
  const Row &r = rows_[size_t(index.row())];
  if (role == Qt::TextAlignmentRole)
    return int((index.column() == Mag || index.column() == Depth ||
                index.column() == Distance || index.column() == FaultKm)
                   ? Qt::AlignRight | Qt::AlignVCenter
                   : Qt::AlignLeft | Qt::AlignVCenter);
  switch (index.column()) {
  case Time:
    return QDateTime::fromMSecsSinceEpoch(qint64(r.e.event.epoch * 1000),
                                          QTimeZone::utc())
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  case Mag:
    return QString::number(r.e.event.mag, 'f', 1);
  case Depth:
    return QString::number(r.e.event.depthKm, 'f', 1);
  case Place:
    return r.e.event.place;
  case Distance:
    return QString::number(r.e.km, 'f', 1);
  case Direction:
    return compassDirection(r.e.bearing, lang_);
  case Band:
    return r.e.band >= 0 ? scapBands()[size_t(r.e.band)].name : QString();
  case FaultName:
    return r.faultName;
  case FaultKm:
    return r.faultKm < 0 ? QString() : QString::number(r.faultKm, 'f', 1);
  case FaultDir:
    return r.faultDir;
  }
  return {};
}

QVariant EventsModel::headerData(int section, Qt::Orientation o,
                                 int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case Time:
    return tr("Time (UTC)");
  case Mag:
    return tr("M");
  case Depth:
    return tr("Depth (km)");
  case Place:
    return tr("Place");
  case Distance:
    return tr("Distance (km)");
  case Direction:
    return tr("Direction");
  case Band:
    return tr("Band");
  case FaultName:
    return tr("Nearest fault");
  case FaultKm:
    return tr("Fault (km)");
  case FaultDir:
    return tr("Side of fault");
  }
  return {};
}

CatalogPane::CatalogPane(std::shared_ptr<AnalysisState> state, QWidget *parent)
    : QWidget(parent), state_(std::move(state)) {
  model_ = new EventsModel(this);
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->setSortingEnabled(false);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(EventsModel::Place,
                                                   QHeaderView::Stretch);
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  faultInfo_ = new QLabel(this);
  faultInfo_->setWordWrap(true);
  map_ = new FigureView(this);
  extent_ = new QDoubleSpinBox(this);
  extent_->setRange(0, 5000);
  extent_->setDecimals(0);
  extent_->setSingleStep(50);
  extent_->setSpecialValueText(tr("fit"));
  showFaults_ = new QCheckBox(this);
  showFaults_->setChecked(true);
  exportPng_ = new QPushButton(this);
  exportSvg_ = new QPushButton(this);
  exportCsv_ = new QPushButton(this);
  auto *mapBar = new QHBoxLayout;
  mapBar->addWidget(new QLabel(tr("View half-width (km)"), this));
  mapBar->addWidget(extent_);
  mapBar->addWidget(showFaults_);
  mapBar->addStretch(1);
  mapBar->addWidget(exportPng_);
  mapBar->addWidget(exportSvg_);
  auto *left = new QWidget(this);
  auto *ll = new QVBoxLayout(left);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->addWidget(summary_);
  ll->addWidget(faultInfo_);
  ll->addWidget(table_, 1);
  auto *tableBar = new QHBoxLayout;
  tableBar->addStretch(1);
  tableBar->addWidget(exportCsv_);
  ll->addLayout(tableBar);
  auto *right = new QWidget(this);
  auto *rl = new QVBoxLayout(right);
  rl->setContentsMargins(0, 0, 0, 0);
  rl->addLayout(mapBar);
  rl->addWidget(map_, 1);
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  connect(extent_, &QDoubleSpinBox::valueChanged, this, &CatalogPane::refresh);
  connect(showFaults_, &QCheckBox::toggled, this, &CatalogPane::refresh);
  connect(exportPng_, &QPushButton::clicked, this,
          [this] { exportMap(false); });
  connect(exportSvg_, &QPushButton::clicked, this, [this] { exportMap(true); });
  connect(exportCsv_, &QPushButton::clicked, this, &CatalogPane::exportTable);
  retranslate();
}

void CatalogPane::refresh() {
  const AnalysisState &s = *state_;
  model_->setEvents(s.eventsAtStation, s.faults.empty() ? nullptr : &s.faults,
                    s.lang);
  if (!s.position) {
    summary_->setText(tr("Set the station (a feature table from the Features "
                         "tab names it) to see the events "
                         "around it."));
    faultInfo_->clear();
    map_->clearFigure();
    return;
  }
  QString text = tr("%1 at %2, %3")
                     .arg(s.position->code)
                     .arg(s.position->lat, 0, 'f', 4)
                     .arg(s.position->lon, 0, 'f', 4);
  if (s.spanEnd > s.spanStart)
    text += tr("; table from %1 to %2 (UTC)")
                .arg(QDateTime::fromMSecsSinceEpoch(qint64(s.spanStart * 1000),
                                                    QTimeZone::utc())
                         .toString("yyyy-MM-dd HH:mm"),
                     QDateTime::fromMSecsSinceEpoch(qint64(s.spanEnd * 1000),
                                                    QTimeZone::utc())
                         .toString("yyyy-MM-dd HH:mm"));
  if (s.events.empty())
    text += tr("; no catalog loaded");
  else {
    text += tr("; %n event(s) in SCAP's bands", nullptr,
               int(s.eventsAtStation.size()));
    if (s.mainEvent)
      text += tr("; main event M%1 %2 at %3 km (%4)")
                  .arg(s.mainEvent->event.mag, 0, 'f', 1)
                  .arg(s.mainEvent->event.place)
                  .arg(s.mainEvent->km, 0, 'f', 1)
                  .arg(QDateTime::fromMSecsSinceEpoch(
                           qint64(s.mainEvent->event.epoch * 1000),
                           QTimeZone::utc())
                           .toString("yyyy-MM-dd HH:mm"));
    else
      text += tr("; no M5+ event in the period");
  }
  summary_->setText(text);
  if (s.faults.empty())
    faultInfo_->setText(tr("No fault lines loaded."));
  else if (const auto n = s.faults.nearest(s.position->lat, s.position->lon))
    faultInfo_->setText(tr("Nearest fault to the station: %1, %2 km, station "
                           "lies %3 of it (%4, %5)")
                            .arg(n->fault->label())
                            .arg(n->km, 0, 'f', 1)
                            .arg(n->direction(s.lang),
                                 Fault::typeName(n->fault->typeCode),
                                 n->fault->slip));
  MapSpec spec;
  spec.lang = s.lang;
  spec.station = *s.position;
  spec.events = s.eventsAtStation;
  spec.mainEvent = s.mainEvent;
  spec.extentKm = extent_->value();
  spec.faults = s.faults.empty() ? nullptr : &s.faults;
  spec.showFaults = showFaults_->isChecked();
  spec.title = s.position->code;
  const auto faults = state_; // keeps the state alive for the lambda
  map_->setFigure(
      [spec, faults](QPainter &p, const QRectF &r) { drawMap(p, r, spec); },
      QSize(800, 700));
}

void CatalogPane::exportMap(bool svg) {
  if (!map_->hasFigure())
    return;
  const QString suggested =
      QStringLiteral("%1_map.%2")
          .arg(state_->station.isEmpty() ? "station" : state_->station,
               svg ? "svg" : "png");
  const QString path =
      QFileDialog::getSaveFileName(this, tr("Save the map"), suggested,
                                   svg ? tr("SVG (*.svg)") : tr("PNG (*.png)"));
  if (path.isEmpty())
    return;
  const bool ok = svg ? map_->exportSvg(path) : map_->exportPng(path, 2.5);
  if (!ok)
    QMessageBox::warning(this, tr("Export"),
                         tr("Could not write %1.").arg(path));
}

void CatalogPane::exportTable() {
  if (model_->rowCount() == 0)
    return;
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save the event table"),
      QStringLiteral("%1_events.csv").arg(state_->station), tr("CSV (*.csv)"));
  if (path.isEmpty())
    return;
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("Export"),
                         tr("Could not write %1.").arg(path));
    return;
  }
  f.write("\xEF\xBB\xBF");
  QStringList header;
  for (int c = 0; c < model_->columnCount(); ++c)
    header << model_->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
  f.write(header.join(',').toUtf8() + '\n');
  for (int r = 0; r < model_->rowCount(); ++r) {
    QStringList cells;
    for (int c = 0; c < model_->columnCount(); ++c) {
      QString v = model_->data(model_->index(r, c), Qt::DisplayRole).toString();
      if (v.contains(',') || v.contains('"'))
        v = '"' + v.replace('"', "\"\"") + '"';
      cells << v;
    }
    f.write(cells.join(',').toUtf8() + '\n');
  }
  f.commit();
}

void CatalogPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void CatalogPane::retranslate() {
  ui::setEmptyHint(table_, tr("No earthquakes listed yet.\nChoose a catalog "
                              "and a station at the top of the page."));
  showFaults_->setText(tr("show faults"));
  exportPng_->setText(tr("Map as PNG…"));
  exportSvg_->setText(tr("Map as SVG…"));
  exportCsv_->setText(tr("Events as CSV…"));
}

} // namespace skg
