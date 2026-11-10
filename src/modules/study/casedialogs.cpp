#include "modules/study/casedialogs.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>

#include "core/ui.h"

namespace skg {

namespace {

QTableWidget *plainTable(QWidget *parent) {
  auto *t = new QTableWidget(parent);
  t->verticalHeader()->hide();
  t->setShowGrid(false);
  t->setAlternatingRowColors(true);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->horizontalHeader()->setStretchLastSection(true);
  t->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  return t;
}

QSpinBox *spanBox(QWidget *parent) {
  auto *s = new QSpinBox(parent);
  s->setRange(1, 3650);
  s->setValue(365);
  s->setSuffix(QCoreApplication::translate("skg::AddFromCatalogDialog",
                                           " days before the event"));
  return s;
}

} // namespace

void fillStationTable(QTableWidget *table,
                      const QList<study::StationRef> &stations,
                      const QStringList &ticked) {
  const QSignalBlocker block(table);
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels(
      {QCoreApplication::translate("skg::AddFromCatalogDialog", "Use"),
       QCoreApplication::translate("skg::AddFromCatalogDialog", "Station"),
       QCoreApplication::translate("skg::AddFromCatalogDialog", "Distance"),
       QCoreApplication::translate("skg::AddFromCatalogDialog", "Azimuth")});
  table->setRowCount(int(stations.size()));
  for (int i = 0; i < int(stations.size()); ++i) {
    const study::StationRef &s = stations[i];
    auto *use = new QTableWidgetItem;
    use->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled |
                  Qt::ItemIsSelectable);
    use->setCheckState(ticked.contains(s.code) ? Qt::Checked : Qt::Unchecked);
    use->setData(Qt::UserRole, s.code);
    table->setItem(i, 0, use);
    table->setItem(i, 1, new QTableWidgetItem(s.code));
    table->setItem(
        i, 2,
        new QTableWidgetItem(
            QCoreApplication::translate("skg::AddFromCatalogDialog", "%1 km")
                .arg(s.distanceKm, 0, 'f', 0)));
    table->setItem(i, 3,
                   new QTableWidgetItem(
                       QStringLiteral("%1°").arg(s.azimuthDeg, 0, 'f', 0)));
  }
  table->setColumnWidth(0, 64);
}

QList<study::StationRef> tickedStations(const QTableWidget *table,
                                        const QList<study::StationRef> &all) {
  QList<study::StationRef> out;
  for (int i = 0; i < table->rowCount(); ++i) {
    const QTableWidgetItem *it = table->item(i, 0);
    if (!it || it->checkState() != Qt::Checked)
      continue;
    const QString code = it->data(Qt::UserRole).toString();
    for (const study::StationRef &s : all)
      if (s.code == code)
        out << s;
  }
  return out;
}

AddFromCatalogDialog::AddFromCatalogDialog(const study::Study &study,
                                           QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Add a case from the catalog"));
  events_ =
      analysis::loadEvents(study.catalogPath(), study.catalogUtcOffsetHours);
  try {
    stations_ = analysis::loadStations(study.stationListPath());
  } catch (const std::exception &) {
    stations_.clear(); // the case can still be added, without stations
  }

  minMag_ = new QDoubleSpinBox(this);
  minMag_->setRange(0.0, 9.5);
  minMag_->setSingleStep(0.1);
  minMag_->setDecimals(1);
  minMag_->setValue(5.0);
  minMag_->setPrefix(QStringLiteral("M ≥ "));
  place_ = new QLineEdit(this);
  place_->setPlaceholderText(tr("place contains…"));
  count_ = new QLabel(this);
  count_->setObjectName(QStringLiteral("fieldLabel"));
  auto *filter = new QHBoxLayout;
  filter->addWidget(minMag_);
  filter->addWidget(place_, 1);
  filter->addWidget(count_);

  table_ = plainTable(this);
  table_->setObjectName(QStringLiteral("eventTable"));
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels(
      {tr("Origin time (UTC)"), tr("M"), tr("Depth"), tr("Place")});
  table_->setColumnWidth(0, 170);
  table_->setColumnWidth(1, 60);
  table_->setColumnWidth(2, 80);

  name_ = new QLineEdit(this);
  span_ = spanBox(this);
  maxKm_ = new QDoubleSpinBox(this);
  maxKm_->setRange(10, 1000);
  maxKm_->setDecimals(0);
  maxKm_->setValue(150);
  maxKm_->setPrefix(tr("stations within "));
  maxKm_->setSuffix(tr(" km"));
  stationTable_ = plainTable(this);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->setSelectionMode(QAbstractItemView::NoSelection);
  stationTable_->setMinimumHeight(170);

  auto *form = new QFormLayout;
  form->addRow(tr("Name"), name_);
  form->addRow(tr("Span"), span_);
  form->addRow(tr("Stations"), maxKm_);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Add case"));
  ui::setPrimary(buttons->button(QDialogButtonBox::Ok));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(filter);
  layout->addWidget(table_, 3);
  layout->addLayout(form);
  layout->addWidget(stationTable_, 2);
  layout->addWidget(buttons);
  resize(760, 720);

  connect(minMag_, &QDoubleSpinBox::valueChanged, this,
          &AddFromCatalogDialog::refill);
  connect(place_, &QLineEdit::textChanged, this, &AddFromCatalogDialog::refill);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &AddFromCatalogDialog::eventChosen);
  connect(maxKm_, &QDoubleSpinBox::valueChanged, this,
          &AddFromCatalogDialog::eventChosen);
  refill();
}

void AddFromCatalogDialog::refill() {
  const double minMag = minMag_->value();
  const QString needle = place_->text().trimmed();
  shown_.clear();
  for (int i = 0; i < int(events_.size()); ++i) {
    const analysis::Event &e = events_[static_cast<size_t>(i)];
    if (e.mag + 1e-9 < minMag)
      continue;
    if (!needle.isEmpty() && !e.place.contains(needle, Qt::CaseInsensitive))
      continue;
    shown_.push_back(i);
  }
  // Largest first; the table shows at most the first 500.
  std::sort(shown_.begin(), shown_.end(), [this](int a, int b) {
    const analysis::Event &x = events_[static_cast<size_t>(a)];
    const analysis::Event &y = events_[static_cast<size_t>(b)];
    return x.mag != y.mag ? x.mag > y.mag : x.epoch > y.epoch;
  });
  const int total = int(shown_.size());
  if (shown_.size() > 500)
    shown_.resize(500);
  const QSignalBlocker block(table_);
  table_->setRowCount(int(shown_.size()));
  for (int row = 0; row < int(shown_.size()); ++row) {
    const analysis::Event &e =
        events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])];
    const QDateTime t = QDateTime::fromSecsSinceEpoch(
        qint64(std::floor(e.epoch)), QTimeZone::UTC);
    table_->setItem(row, 0,
                    new QTableWidgetItem(
                        t.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    table_->setItem(row, 1,
                    new QTableWidgetItem(QString::number(e.mag, 'f', 1)));
    table_->setItem(
        row, 2, new QTableWidgetItem(tr("%1 km").arg(e.depthKm, 0, 'f', 0)));
    table_->setItem(row, 3, new QTableWidgetItem(e.place));
  }
  count_->setText(total > int(shown_.size())
                      ? tr("%1 earthquakes, the largest %2 shown")
                            .arg(total)
                            .arg(shown_.size())
                      : tr("%n earthquake(s)", nullptr, total));
  table_->clearSelection();
  eventChosen();
}

void AddFromCatalogDialog::eventChosen() {
  const int row = table_->currentRow();
  const bool has = row >= 0 && row < int(shown_.size()) &&
                   !table_->selectedItems().isEmpty();
  if (!has) {
    near_.clear();
    fillStationTable(stationTable_, near_, {});
    return;
  }
  const analysis::Event &e =
      events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])];
  name_->setText(study::caseFromEvent(e).name);
  const QStringList keep = [this] {
    QStringList k;
    for (const study::StationRef &s : tickedStations(stationTable_, near_))
      k << s.code;
    return k;
  }();
  near_ = study::nearestStations(stations_, e.lat, e.lon, maxKm_->value());
  fillStationTable(stationTable_, near_, keep);
}

void AddFromCatalogDialog::accept() {
  const int row = table_->currentRow();
  if (row < 0 || row >= int(shown_.size()) || table_->selectedItems().isEmpty())
    return;
  study::Case c = study::caseFromEvent(
      events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])]);
  if (!name_->text().trimmed().isEmpty())
    c.name = name_->text().trimmed();
  c.spanDays = span_->value();
  c.stations = tickedStations(stationTable_, near_);
  result_ = c;
  QDialog::accept();
}

ReferenceDateDialog::ReferenceDateDialog(const study::Study &study,
                                         QWidget *parent)
    : QDialog(parent), study_(study) {
  setWindowTitle(tr("Add a reference date"));
  auto *note = new QLabel(
      tr("A reference date is a case with no earthquake: the same recipes "
         "run on the span before it, so it can be read beside the "
         "earthquakes."),
      this);
  note->setWordWrap(true);
  name_ = new QLineEdit(tr("Reference"), this);
  time_ = new QDateTimeEdit(this);
  time_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  time_->setTimeZone(QTimeZone::UTC);
  time_->setDateTime(QDateTime(QDate::currentDate().addYears(-1), QTime(0, 0),
                               QTimeZone::UTC));
  time_->setCalendarPopup(true);
  place_ = new QComboBox(this);
  for (const study::Case &c : study.cases)
    place_->addItem(tr("the place and stations of %1").arg(c.name), c.id);
  place_->addItem(tr("these coordinates, no stations yet"), QString());
  lat_ = new QDoubleSpinBox(this);
  lat_->setRange(-90, 90);
  lat_->setDecimals(4);
  lat_->setValue(38.0);
  lon_ = new QDoubleSpinBox(this);
  lon_->setRange(-180, 180);
  lon_->setDecimals(4);
  lon_->setValue(39.0);
  auto *coords = new QHBoxLayout;
  coords->addWidget(lat_);
  coords->addWidget(lon_);
  span_ = spanBox(this);
  const auto sync = [this] {
    const bool custom = place_->currentData().toString().isEmpty();
    lat_->setEnabled(custom);
    lon_->setEnabled(custom);
  };
  connect(place_, &QComboBox::currentIndexChanged, this, sync);
  sync();

  auto *form = new QFormLayout;
  form->addRow(tr("Name"), name_);
  form->addRow(tr("Date (UTC)"), time_);
  form->addRow(tr("Place"), place_);
  form->addRow(tr("Latitude, longitude"), coords);
  form->addRow(tr("Span"), span_);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Add case"));
  ui::setPrimary(buttons->button(QDialogButtonBox::Ok));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(note);
  layout->addLayout(form);
  layout->addWidget(buttons);
  resize(520, 0);
}

void ReferenceDateDialog::accept() {
  const QString from = place_->currentData().toString();
  study::Case c = study::referenceCase(
      name_->text().trimmed().isEmpty() ? tr("Reference")
                                        : name_->text().trimmed(),
      time_->dateTime().toUTC(), lat_->value(), lon_->value());
  if (const study::Case *src = study_.findCase(from)) {
    c.lat = src->lat;
    c.lon = src->lon;
    c.place = src->place;
    c.zone = src->zone;
    c.stations = src->stations;
  }
  c.spanDays = span_->value();
  result_ = c;
  QDialog::accept();
}

} // namespace skg
