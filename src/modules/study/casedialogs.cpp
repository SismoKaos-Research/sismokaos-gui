#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <exception>
#include <numeric>

#include "core/defaults.h"
#include "core/ui.h"
#include "study/catalogs.h"
#include "study/waveindex.h"

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

// Where the code under the cursor starts and ends in a list of codes
// separated by commas, semicolons or spaces.
QPair<int, int> codeAt(const QString &text, int cursor) {
  const auto isSep = [](QChar c) {
    return c == QLatin1Char(',') || c == QLatin1Char(';') || c.isSpace();
  };
  int start = qBound(0, cursor, int(text.size()));
  while (start > 0 && !isSep(text[start - 1]))
    --start;
  int end = qBound(0, cursor, int(text.size()));
  while (end < text.size() && !isSep(text[end]))
    ++end;
  return {start, end};
}

// Completion for a list of station codes: only the code being typed is
// completed, the earlier ones stay. The popup says where each station is;
// only its code is inserted.
void attachCodeCompleter(QLineEdit *edit,
                         const std::vector<analysis::Station> &stations) {
  auto *model = new QStandardItemModel(edit);
  for (const analysis::Station &st : stations) {
    QStringList where;
    if (!st.province.isEmpty())
      where << st.province;
    if (!st.district.isEmpty())
      where << st.district;
    auto *item = new QStandardItem(
        where.isEmpty() ? st.code
                        : st.code + QStringLiteral(" — ") +
                              where.join(QLatin1Char('/')));
    item->setData(st.code, Qt::UserRole);
    model->appendRow(item);
  }
  model->sort(0);
  auto *completer = new QCompleter(model, edit);
  completer->setObjectName(QStringLiteral("nearCompleter"));
  completer->setCompletionRole(Qt::UserRole);
  completer->setCaseSensitivity(Qt::CaseInsensitive);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setWidget(edit); // not setCompleter(): that replaces the text
  QObject::connect(edit, &QLineEdit::textEdited, completer,
                   [edit, completer](const QString &text) {
                     const auto [start, end] =
                         codeAt(text, edit->cursorPosition());
                     const QString prefix =
                         text.mid(start, edit->cursorPosition() - start);
                     completer->setCompletionPrefix(prefix);
                     if (prefix.isEmpty()) {
                       completer->popup()->hide();
                       return;
                     }
                     completer->complete();
                   });
  QObject::connect(
      completer, qOverload<const QModelIndex &>(&QCompleter::activated), edit,
      [edit](const QModelIndex &index) {
        const QString code = index.data(Qt::UserRole).toString();
        const QString text = edit->text();
        const auto [start, end] = codeAt(text, edit->cursorPosition());
        edit->setText(text.left(start) + code + text.mid(end));
        edit->setCursorPosition(start + int(code.size()));
      });
}

} // namespace

bool hasWaveformsOf(const study::Study &study, const study::StationRef &s) {
  return study::hasWaveforms(study, s);
}

QString waveformNote(const study::Study &study, const study::StationRef &s,
                     const QDateTime &from, const QDateTime &to) {
  const int total = int(std::max<qint64>(1, from.daysTo(to)));
  if (!s.dataDir.isEmpty()) {
    const QString dir = study.resolved(s.dataDir);
    return QDir(dir).exists()
               ? QCoreApplication::translate("skg::AddFromCatalogDialog", "own folder: %1").arg(dir)
               : QCoreApplication::translate("skg::AddFromCatalogDialog", "⚠ folder not found: %1")
                     .arg(dir);
  }
  const auto index = study::WaveIndex::shared(study.dir);
  if (index->hasStation(s.code)) {
    bool approx = false;
    const int days = index->daysCovered(s.code, from, to, &approx);
    return approx ? QCoreApplication::translate(
                        "skg::AddFromCatalogDialog", "found: at least %1 of %2 days in span "
                             "(coverage not read yet)")
                        .arg(days)
                        .arg(total)
                  : QCoreApplication::translate(
                        "skg::AddFromCatalogDialog", "found: %1 of %2 days in span")
                        .arg(days)
                        .arg(total);
  }
  if (study::hasWaveforms(study, s))
    return QCoreApplication::translate("skg::AddFromCatalogDialog", "folder found, not scanned yet");
  return QCoreApplication::translate("skg::AddFromCatalogDialog", "not found");
}

void fillStationTable(QTableWidget *table,
                      const QList<study::StationRef> &stations,
                      const QStringList &ticked,
                      const StationNote &waveforms) {
  const QSignalBlocker block(table);
  QStringList headers = {
      QCoreApplication::translate("skg::AddFromCatalogDialog", "Use"),
      QCoreApplication::translate("skg::AddFromCatalogDialog", "Station"),
      QCoreApplication::translate("skg::AddFromCatalogDialog", "Distance"),
      QCoreApplication::translate("skg::AddFromCatalogDialog", "Azimuth")};
  if (waveforms)
    headers << QCoreApplication::translate("skg::AddFromCatalogDialog",
                                           "Waveforms");
  table->setColumnCount(int(headers.size()));
  table->setHorizontalHeaderLabels(headers);
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
    if (waveforms)
      table->setItem(i, 4, new QTableWidgetItem(waveforms(s)));
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

QStringList stationsWithWaveforms(const study::Study &study) {
  QStringList codes;
  for (const study::WaveStation &s :
       study::WaveIndex::shared(study.dir)->stations())
    if (!s.code.isEmpty() && !codes.contains(s.code))
      codes << s.code;
  codes.sort();
  return codes;
}

AddFromCatalogDialog::AddFromCatalogDialog(const study::Study &study,
                                           QWidget *parent,
                                           const QStringList &nearStations)
    : QDialog(parent), study_(study), builtIn_(study.catalog.isEmpty()) {
  setWindowTitle(tr("Add a case from the catalog"));
  // The study's own catalog, or the built-in list of large earthquakes when
  // it has none: enough to pick the cases from.
  events_ = study::pickingEvents(study);
  try {
    stations_ = study.stations();
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

  // Second filter row: only earthquakes close to the stations one has data
  // for. Empty means no such filter.
  nearCodes_ = new QLineEdit(this);
  nearCodes_->setObjectName(QStringLiteral("nearStations"));
  nearCodes_->setPlaceholderText(
      tr("only near station(s): codes separated by commas"));
  nearCodes_->setClearButtonEnabled(true);
  attachCodeCompleter(nearCodes_, stations_);
  nearKm_ = new QDoubleSpinBox(this);
  nearKm_->setObjectName(QStringLiteral("nearKm"));
  nearKm_->setRange(5, 1000);
  nearKm_->setDecimals(0);
  nearKm_->setValue(100);
  nearKm_->setPrefix(tr("within "));
  nearKm_->setSuffix(tr(" km"));
  nearNote_ = new QLabel(this);
  nearNote_->setObjectName(QStringLiteral("fieldLabel"));
  nearNote_->setWordWrap(true);
  nearNote_->hide();
  mine_ = new QPushButton(tr("Stations I have data for"), this);
  mine_->setObjectName(QStringLiteral("nearMine"));
  mine_->setAutoDefault(false);
  haveData_ = stationsWithWaveforms(study);
  mine_->setEnabled(!haveData_.isEmpty());
  connect(mine_, &QPushButton::clicked, this,
          [this] { nearCodes_->setText(haveData_.join(QStringLiteral(", "))); });
  auto *nearRow = new QHBoxLayout;
  nearRow->addWidget(nearCodes_, 1);
  nearRow->addWidget(nearKm_);
  nearRow->addWidget(mine_);

  table_ = plainTable(this);
  table_->setObjectName(QStringLiteral("eventTable"));
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels({tr("Origin time (UTC)"), tr("M"),
                                     tr("Depth"), tr("Nearest station"),
                                     tr("Place")});
  table_->setColumnWidth(0, 170);
  table_->setColumnWidth(1, 60);
  table_->setColumnWidth(2, 80);
  table_->setColumnWidth(3, 140);

  name_ = new QLineEdit(this);
  span_ = spanBox(this);
  maxKm_ = new QDoubleSpinBox(this);
  maxKm_->setRange(10, 1000);
  maxKm_->setDecimals(0);
  maxKm_->setValue(defaults::kStationRadiusKm);
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
  ok_ = buttons->button(QDialogButtonBox::Ok);
  ok_->setText(tr("Add case"));
  ui::setPrimary(ok_);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  range_ = new QLabel(this);
  range_->setObjectName(QStringLiteral("fieldLabel"));
  if (!events_.empty()) {
    const auto [lo, hi] = std::minmax_element(
        events_.begin(), events_.end(),
        [](const analysis::Event &a, const analysis::Event &b) {
          return a.epoch < b.epoch;
        });
    const auto day = [](double epoch) {
      return QDateTime::fromSecsSinceEpoch(qint64(epoch), QTimeZone::UTC)
          .toString(QStringLiteral("yyyy-MM-dd"));
    };
    range_->setText(
        builtIn_
            ? tr("Built-in list of large earthquakes: %1 to %2, %3 events of "
                 "magnitude 5 and above. Enough to pick the cases; the "
                 "analyses of small earthquakes need the study's own catalog.")
                  .arg(day(lo->epoch), day(hi->epoch))
                  .arg(events_.size())
            : tr("The catalog covers %1 to %2 (%3 events).")
                  .arg(day(lo->epoch), day(hi->epoch))
                  .arg(events_.size()));
    range_->setWordWrap(true);
  }
  stationNote_ = new QLabel(this);
  stationNote_->setObjectName(QStringLiteral("fieldLabel"));
  warning_ = new QLabel(this);
  warning_->setObjectName(QStringLiteral("caseWarning"));
  warning_->setWordWrap(true);

  // The map beside the tables: the list, then the chosen earthquake.
  preview_ = new CasePreview(this);
  auto *outer = new QHBoxLayout(this);
  auto *layout = new QVBoxLayout;
  outer->addLayout(layout, 5);
  outer->addWidget(preview_, 4);
  layout->addLayout(filter);
  layout->addLayout(nearRow);
  layout->addWidget(nearNote_);
  layout->addWidget(table_, 3);
  layout->addWidget(range_);
  layout->addLayout(form);
  layout->addWidget(stationTable_, 2);
  layout->addWidget(stationNote_);
  layout->addWidget(warning_);
  layout->addWidget(buttons);
  resize(1240, 760);
  ui::polishPage(this);

  ui::setHelp(minMag_, tr("Lists only earthquakes of at least this "
                          "magnitude. Lower it to see smaller events; the "
                          "table shows at most the 500 largest."));
  ui::setHelp(place_, tr("Lists only earthquakes whose place name in the "
                         "catalog contains this text, e.g. a province."));
  mine_->setToolTip(ui::helpText(
      haveData_.isEmpty()
          ? tr("Fills the filter with the stations found in the study's "
               "waveform folders. None are known yet: add a waveform folder "
               "on the Cases page first.")
          : tr("Fills the filter with the %n station(s) found in the study's "
               "waveform folders, so only earthquakes you can study with the "
               "data you have are listed.",
               nullptr, int(haveData_.size()))));
  ui::setHelp(nearCodes_,
              tr("Lists only earthquakes close to at least one of these "
                 "stations, e.g. the ones you have waveforms for. Type "
                 "station codes of the built-in AFAD list separated by "
                 "commas; codes are completed as you type. Those stations "
                 "start ticked for the earthquake you pick."));
  ui::setHelp(nearKm_, tr("How close to one of the stations above an "
                          "earthquake must be to be listed, in km."));
  table_->setToolTip(ui::helpText(
      tr("The catalog's earthquakes that pass the filters, largest first. "
         "Select the one to study.")));
  ui::setHelp(name_, tr("The case's name in tables and figures. Filled from "
                        "the earthquake's place and year; change it "
                        "freely."));
  ui::setHelp(span_, tr("How many days before the earthquake are studied. "
                        "It can be changed later on the Cases page."));
  ui::setHelp(maxKm_, tr("Stations of the built-in AFAD list within this "
                         "distance of the epicentre are offered in the "
                         "table below."));
  stationTable_->setToolTip(ui::helpText(
      tr("Tick the stations the earthquake is studied from. Distance and "
         "azimuth are from the epicentre to the station.")));

  connect(minMag_, &QDoubleSpinBox::valueChanged, this,
          &AddFromCatalogDialog::refill);
  connect(place_, &QLineEdit::textChanged, this, &AddFromCatalogDialog::refill);
  connect(nearCodes_, &QLineEdit::textChanged, this,
          &AddFromCatalogDialog::refill);
  connect(nearKm_, &QDoubleSpinBox::valueChanged, this,
          &AddFromCatalogDialog::refill);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &AddFromCatalogDialog::eventChosen);
  connect(maxKm_, &QDoubleSpinBox::valueChanged, this,
          &AddFromCatalogDialog::eventChosen);
  connect(span_, &QSpinBox::valueChanged, this,
          &AddFromCatalogDialog::eventChosen);
  connect(stationTable_, &QTableWidget::itemChanged, this,
          &AddFromCatalogDialog::updateNotes);
  if (!nearStations.isEmpty())
    nearCodes_->setText(nearStations.join(QStringLiteral(", ")));
  connect(preview_, &CasePreview::eventClicked, this, [this](int row) {
    if (row >= 0 && row < table_->rowCount()) {
      table_->selectRow(row);
      table_->scrollToItem(table_->item(row, 0));
    }
  });
  connect(preview_, &CasePreview::stationClicked, this,
          [this](const QString &code) {
            for (int i = 0; i < stationTable_->rowCount(); ++i) {
              QTableWidgetItem *use = stationTable_->item(i, 0);
              if (use && use->data(Qt::UserRole).toString() == code)
                use->setCheckState(use->checkState() == Qt::Checked
                                       ? Qt::Unchecked
                                       : Qt::Checked);
            }
          });
  refill();
}

void AddFromCatalogDialog::refill() {
  const double minMag = minMag_->value();
  const QString needle = place_->text().trimmed();
  // Resolve the typed station codes; unknown ones are named, not ignored.
  filterStations_.clear();
  QStringList unknown;
  static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
  for (const QString &code : nearCodes_->text().split(sep, Qt::SkipEmptyParts)) {
    const auto it = std::find_if(
        stations_.begin(), stations_.end(), [&code](const analysis::Station &s) {
          return s.code.compare(code, Qt::CaseInsensitive) == 0;
        });
    if (it == stations_.end())
      unknown << code;
    else if (std::none_of(filterStations_.begin(), filterStations_.end(),
                          [&it](const analysis::Station &s) {
                            return s.code == it->code;
                          }))
      filterStations_.push_back(*it);
  }
  const bool byStation = !filterStations_.empty();
  const bool typedOnlyUnknown = !unknown.isEmpty() && !byStation;
  nearKm_->setEnabled(byStation);
  nearNote_->setVisible(!unknown.isEmpty());
  nearNote_->setText(unknown.isEmpty()
                         ? QString()
                         : tr("not in the station list: %1")
                               .arg(unknown.join(QStringLiteral(", "))));
  const double nearKm = nearKm_->value();
  shown_.clear();
  std::vector<QString> nearestText(events_.size());
  for (int i = 0; i < int(events_.size()); ++i) {
    const analysis::Event &e = events_[static_cast<size_t>(i)];
    if (e.mag + 1e-9 < minMag)
      continue;
    if (!needle.isEmpty() && !e.place.contains(needle, Qt::CaseInsensitive))
      continue;
    if (typedOnlyUnknown)
      continue; // a filter was asked for and matches no station
    if (byStation) {
      const auto hits =
          analysis::stationsWithin(filterStations_, e.lat, e.lon, nearKm);
      if (hits.empty())
        continue;
      const auto best = std::min_element(
          hits.begin(), hits.end(),
          [](const analysis::StationDistance &a,
             const analysis::StationDistance &b) { return a.km < b.km; });
      nearestText[static_cast<size_t>(i)] =
          tr("%1 · %2 km").arg(best->station.code).arg(best->km, 0, 'f', 0);
    }
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
    table_->setItem(
        row, 3,
        new QTableWidgetItem(
            nearestText[static_cast<size_t>(shown_[static_cast<size_t>(row)])]));
    table_->setItem(row, 4, new QTableWidgetItem(e.place));
  }
  table_->setColumnHidden(3, !byStation);
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
    stationNote_->clear();
    updateNotes();
    return;
  }
  const analysis::Event &e =
      events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])];
  name_->setText(study::caseFromEvent(e).name);
  // Stations stay ticked from the previous choice; the ones the events were
  // filtered by start ticked, since they are why this event is listed.
  QStringList keep;
  for (const study::StationRef &s : tickedStations(stationTable_, near_))
    keep << s.code;
  double radius = maxKm_->value();
  if (!filterStations_.empty()) {
    radius = std::max(radius, nearKm_->value());
    for (const analysis::Station &s : filterStations_)
      keep << s.code;
  }
  const QList<study::StationRef> all = study::nearestStations(
      stations_, e.lat, e.lon, radius, std::numeric_limits<int>::max());
  near_ = all.mid(0, study::kStationCandidates);
  // Ticked stations beyond the cut stay offered.
  for (const study::StationRef &s : all.mid(study::kStationCandidates))
    if (keep.contains(s.code))
      near_ << s;
  // Nothing chosen yet: start with the nearest stations that have
  // waveforms, or just the nearest when none has.
  if (keep.isEmpty()) {
    for (const study::StationRef &s : std::as_const(near_))
      if (keep.size() < 3 && hasWaveformsOf(study_, s))
        keep << s.code;
    if (keep.isEmpty())
      for (const study::StationRef &s : near_.mid(0, 3))
        keep << s.code;
  }
  const QDateTime t1 = QDateTime::fromSecsSinceEpoch(
      qint64(std::floor(e.epoch)), QTimeZone::UTC);
  const QDateTime t0 = t1.addDays(-span_->value());
  fillStationTable(stationTable_, near_, keep,
                   [this, t0, t1](const study::StationRef &s) {
                     return waveformNote(study_, s, t0, t1);
                   });
  stationTable_->resizeColumnToContents(4);
  stationNote_->setText(
      all.size() > near_.size()
          ? tr("The nearest %1 of %2 stations within %3 km are shown.")
                .arg(near_.size())
                .arg(all.size())
                .arg(radius, 0, 'f', 0)
          : tr("%n station(s) within %1 km.", nullptr, int(all.size()))
                .arg(radius, 0, 'f', 0));
  updateNotes();
}

void AddFromCatalogDialog::updateNotes() {
  const int row = table_->currentRow();
  const bool has = row >= 0 && row < int(shown_.size()) &&
                   !table_->selectedItems().isEmpty();
  ok_->setEnabled(has);
  ok_->setToolTip(has ? QString()
                      : ui::helpText(tr("Select an earthquake in the list "
                                        "first.")));
  QStringList notes;
  if (has) {
    const analysis::Event &e =
        events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])];
    for (const study::Case &c : study_.cases)
      if (!c.reference &&
          std::abs(double(c.time.toSecsSinceEpoch()) - e.epoch) < 2.0 &&
          std::abs(c.lat - e.lat) < 1e-3 && std::abs(c.lon - e.lon) < 1e-3)
        notes << tr("⚠ This earthquake is already a case: %1.").arg(c.name);
    if (tickedStations(stationTable_, near_).isEmpty())
      notes << tr("⚠ No station is ticked: only the Indicators recipes will "
                  "run for this case.");
    // The span, and the longest window an Indicators recipe adds before it,
    // against what the catalog covers.
    double months = 0;
    for (const study::Recipe &r : study_.recipes)
      if (r.tool == QLatin1String("indicators"))
        months = std::max(
            months,
            r.settings.value(QStringLiteral("window_months")).toDouble());
    const double first =
        std::min_element(events_.begin(), events_.end(),
                         [](const analysis::Event &a,
                            const analysis::Event &b) {
                           return a.epoch < b.epoch;
                         })
            ->epoch;
    const double need = e.epoch - (span_->value() + months * 30.44) * 86400.0;
    if (need < first && !builtIn_)
      notes << tr("⚠ The span%1 starts on %2, before the catalog's first "
                  "event (%3). The indicators would be computed on a "
                  "shorter period.")
                   .arg(months > 0 ? tr(" plus a %1 month window").arg(months)
                                   : QString(),
                        QDateTime::fromSecsSinceEpoch(qint64(need),
                                                      QTimeZone::UTC)
                            .toString(QStringLiteral("yyyy-MM-dd")),
                        QDateTime::fromSecsSinceEpoch(qint64(first),
                                                      QTimeZone::UTC)
                            .toString(QStringLiteral("yyyy-MM-dd")));
  }
  warning_->setText(notes.join(QLatin1Char('\n')));
  warning_->setVisible(!notes.isEmpty());
  updatePreview();
}

void AddFromCatalogDialog::updatePreview() {
  if (!preview_)
    return;
  QList<PreviewEvent> list;
  for (int i = 0; i < int(shown_.size()); ++i) {
    const analysis::Event &e =
        events_[static_cast<size_t>(shown_[static_cast<size_t>(i)])];
    PreviewEvent p;
    p.row = i;
    p.lat = e.lat;
    p.lon = e.lon;
    p.magnitude = e.mag;
    p.tip = QStringLiteral("M %1 · %2 · %3")
                .arg(e.mag, 0, 'f', 1)
                .arg(QDateTime::fromSecsSinceEpoch(qint64(std::floor(e.epoch)),
                                                   QTimeZone::UTC)
                         .toString(QStringLiteral("yyyy-MM-dd")),
                     e.place);
    list << p;
  }
  const int row = table_->currentRow();
  const bool has = row >= 0 && row < int(shown_.size()) &&
                   !table_->selectedItems().isEmpty();
  if (!has) {
    QList<CoverageStation> filter;
    for (const analysis::Station &s : filterStations_)
      filter << CoverageStation{s.code, s.lat, s.lon};
    preview_->setList(list, filter, nearKm_->value());
    return;
  }
  const study::Case c = study::caseFromEvent(
      events_[static_cast<size_t>(shown_[static_cast<size_t>(row)])]);
  QStringList ticked;
  for (const study::StationRef &s : tickedStations(stationTable_, near_))
    ticked << s.code;
  preview_->setCase(
      c, near_, ticked,
      [this](const study::StationRef &s) { return hasWaveformsOf(study_, s); },
      list);
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
  ui::setHelp(name_, tr("The case's name in tables and figures."));
  ui::setHelp(time_, tr("The date and time (UTC) that takes the place of "
                        "an earthquake's origin time: days are counted back "
                        "from it."));
  ui::setHelp(place_, tr("Where the reference date is studied: the "
                         "epicentre, zone and stations of an existing "
                         "case, or coordinates of its own with stations "
                         "ticked later on the Cases page."));
  ui::setHelp({lat_, lon_}, tr("Latitude and longitude in degrees (north "
                               "and east positive) of the place, when it is "
                               "not taken from an existing case."));
  ui::setHelp(span_, tr("How many days before the date are studied. It can "
                        "be changed later on the Cases page."));
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
