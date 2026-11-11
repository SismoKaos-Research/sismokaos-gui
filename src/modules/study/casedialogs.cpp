#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDateEdit>
#include <QGuiApplication>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
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
#include "study/runs.h"
#include "study/waveindex.h"
#include "text/text.h"

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
    item->setData(text::fold(st.code), Qt::UserRole + 1);
    model->appendRow(item);
  }
  model->sort(0);
  auto *completer = new QCompleter(model, edit);
  completer->setObjectName(QStringLiteral("nearCompleter"));
  // Matched on the folded code, so case and Turkish letters do not matter.
  completer->setCompletionRole(Qt::UserRole + 1);
  completer->setCaseSensitivity(Qt::CaseSensitive);
  completer->setCompletionMode(QCompleter::PopupCompletion);
  completer->setWidget(edit); // not setCompleter(): that replaces the text
  QObject::connect(edit, &QLineEdit::textEdited, completer,
                   [edit, completer](const QString &text) {
                     const auto [start, end] =
                         codeAt(text, edit->cursorPosition());
                     const QString prefix =
                         text.mid(start, edit->cursorPosition() - start);
                     completer->setCompletionPrefix(text::fold(prefix));
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

bool hasWaveformsIn(const study::Study &study, const study::StationRef &s,
                    const QDateTime &from, const QDateTime &to) {
  if (!study::hasWaveforms(study, s))
    return false;
  return !study::chooseWaveFiles(study, *study::WaveIndex::shared(study.dir), s,
                                 from, to)
              .files.isEmpty();
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
    bool approx = false, atLeast = false;
    const int days = index->daysCovered(s.code, from, to, &approx, &atLeast);
    const study::SpanData span = study::spanData(*index, s.code, from, to);
    // The station has data, but none of it in the span: not the same as a
    // station nothing is known of, and not one to run a case on.
    if (days == 0 && !atLeast && span.first.isValid() &&
        !hasWaveformsIn(study, s, from, to))
      return QCoreApplication::translate(
                 "skg::AddFromCatalogDialog",
                 "⚠ data, but none in the span (it has %1 to %2)")
          .arg(span.first.toString(Qt::ISODate),
               span.last.toString(Qt::ISODate));
    // Exact once the scan has read the station's files; "about" while some
    // of the days are estimated from the periods in the file names; "at
    // least" when some file's name says nothing about its period.
    if (approx && !atLeast)
      return QCoreApplication::translate(
                 "skg::AddFromCatalogDialog",
                 "about %1 of %2 days in span — exact scan running")
          .arg(days)
          .arg(total);
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

QString waveformCell(const study::Study &study, const study::StationRef &s,
                     const QDateTime &from, const QDateTime &to) {
  QString text = waveformNote(study, s, from, to);
  // Little of the span: usable, and marked.
  if (s.dataDir.isEmpty() &&
      study::spanData(*study::WaveIndex::shared(study.dir), s.code, from, to)
          .thin())
    text += QStringLiteral("  ") +
            QCoreApplication::translate("skg::AddFromCatalogDialog",
                                        "⚠ little of the span");
  return text;
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

QStringList stationsToStartWith(const study::Study &study,
                                const QList<study::StationRef> &near,
                                const QDateTime &from, const QDateTime &to,
                                int n) {
  QStringList keep;
  for (const study::StationRef &s : near)
    if (keep.size() < n && hasWaveformsIn(study, s, from, to))
      keep << s.code;
  if (keep.isEmpty())
    for (const study::StationRef &s : near)
      if (keep.size() < n && !hasWaveformsOf(study, s))
        keep << s.code;
  return keep;
}

namespace {

QDateTime utcOf(double epoch) {
  return QDateTime::fromSecsSinceEpoch(qint64(std::floor(epoch)),
                                       QTimeZone::UTC);
}

QStringList splitCodes(const QString &text) {
  static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
  return text.split(sep, Qt::SkipEmptyParts);
}

QDateEdit *dayEdit(QWidget *parent) {
  auto *d = new QDateEdit(parent);
  d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  d->setCalendarPopup(true);
  return d;
}

} // namespace

AddFromCatalogDialog::AddFromCatalogDialog(const study::Study &study,
                                           QWidget *parent,
                                           const QStringList &nearStations,
                                           QSettings *settings)
    : QDialog(parent), study_(study), settings_(settings),
      builtIn_(study.catalog.isEmpty()) {
  setWindowTitle(tr("Add cases from the catalog"));
  key_ = QStringLiteral("addCase/") +
         QString::fromLatin1(QCryptographicHash::hash(study.dir.toUtf8(),
                                                      QCryptographicHash::Sha1)
                                 .toHex()
                                 .left(16)) +
         QLatin1Char('/');
  // The study's own catalog, or the built-in list of large earthquakes when
  // it has none: enough to pick the cases from.
  events_ = study::pickingEvents(study);
  try {
    stations_ = study.stations();
  } catch (const std::exception &) {
    stations_.clear(); // the case can still be added, without stations
  }
  haveData_ = stationsWithWaveforms(study);
  {
    const auto index = study::WaveIndex::shared(study.dir);
    const QDateTime any0(QDate(1970, 1, 1), QTime(0, 0), QTimeZone::UTC);
    for (const analysis::Station &st : stations_)
      if (haveData_.contains(st.code)) {
        haveStations_.push_back(st);
        const study::SpanData d =
            study::spanData(*index, st.code, any0, any0.addDays(1));
        haveRange_.insert(st.code, {d.first, d.last});
      }
  }

  minMag_ = new QDoubleSpinBox(this);
  minMag_->setObjectName(QStringLiteral("minMag"));
  minMag_->setRange(0.0, 9.5);
  minMag_->setSingleStep(0.1);
  minMag_->setDecimals(1);
  minMag_->setValue(5.0);
  minMag_->setPrefix(QStringLiteral("M ≥ "));
  place_ = new QLineEdit(this);
  place_->setObjectName(QStringLiteral("placeFilter"));
  place_->setPlaceholderText(tr("place contains…"));
  place_->setClearButtonEnabled(true);
  count_ = new QLabel(this);
  count_->setObjectName(QStringLiteral("fieldLabel"));
  auto *filter = new QHBoxLayout;
  filter->addWidget(minMag_);
  filter->addWidget(place_, 1);
  filter->addWidget(count_);

  // Second row: when, how deep, and whether the waveforms one has cover it.
  from_ = dayEdit(this);
  from_->setObjectName(QStringLiteral("dateFrom"));
  to_ = dayEdit(this);
  to_->setObjectName(QStringLiteral("dateTo"));
  QDate first = QDate(1900, 1, 1), last = QDate::currentDate();
  if (!events_.empty()) {
    const auto [lo, hi] = std::minmax_element(
        events_.begin(), events_.end(),
        [](const analysis::Event &a, const analysis::Event &b) {
          return a.epoch < b.epoch;
        });
    first = utcOf(lo->epoch).date();
    last = utcOf(hi->epoch).date();
  }
  from_->setDate(first);
  to_->setDate(last);
  maxDepth_ = new QDoubleSpinBox(this);
  maxDepth_->setObjectName(QStringLiteral("maxDepth"));
  maxDepth_->setRange(0, 700);
  maxDepth_->setDecimals(0);
  maxDepth_->setSingleStep(5);
  maxDepth_->setValue(0);
  maxDepth_->setSpecialValueText(tr("any depth"));
  maxDepth_->setPrefix(tr("depth ≤ "));
  maxDepth_->setSuffix(tr(" km"));
  covered_ = new QCheckBox(this);
  covered_->setObjectName(QStringLiteral("spanCovered"));
  const bool estimated =
      study::WaveIndex::shared(study.dir)->pendingCoverage() > 0;
  covered_->setText(estimated ? tr("only where my waveforms cover the span "
                                   "(estimated)")
                              : tr("only where my waveforms cover the span"));
  covered_->setEnabled(!haveStations_.empty());
  auto *whenRow = new QHBoxLayout;
  whenRow->addWidget(ui::buddyLabel(tr("from"), from_, this));
  whenRow->addWidget(from_);
  whenRow->addWidget(ui::buddyLabel(tr("to"), to_, this));
  whenRow->addWidget(to_);
  whenRow->addWidget(maxDepth_);
  whenRow->addWidget(covered_);
  whenRow->addStretch(1);

  // Third row: only earthquakes close to the stations one has data for.
  // Empty means no such filter.
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
  mine_->setEnabled(!haveData_.isEmpty());
  connect(mine_, &QPushButton::clicked, this,
          [this] { nearCodes_->setText(haveData_.join(QStringLiteral(", "))); });
  auto *nearRow = new QHBoxLayout;
  nearRow->addWidget(nearCodes_, 1);
  nearRow->addWidget(nearKm_);
  nearRow->addWidget(mine_);

  table_ = plainTable(this);
  table_->setObjectName(QStringLiteral("eventTable"));
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setColumnCount(5);
  table_->setHorizontalHeaderLabels({tr("Origin time (UTC)"), tr("M"),
                                     tr("Depth"), tr("Nearest station"),
                                     tr("Place")});
  table_->setColumnWidth(0, 170);
  table_->setColumnWidth(1, 60);
  table_->setColumnWidth(2, 80);
  table_->setColumnWidth(3, 140);
  table_->horizontalHeader()->setSectionsClickable(true);
  table_->horizontalHeader()->setSortIndicatorShown(true);
  more_ = new QPushButton(tr("Show more"), this);
  more_->setObjectName(QStringLiteral("showMore"));
  more_->setAutoDefault(false);

  name_ = new QLineEdit(this);
  name_->setObjectName(QStringLiteral("caseName"));
  span_ = spanBox(this);
  span_->setObjectName(QStringLiteral("spanDays"));
  maxKm_ = new QDoubleSpinBox(this);
  maxKm_->setObjectName(QStringLiteral("stationKm"));
  maxKm_->setRange(10, 1000);
  maxKm_->setDecimals(0);
  maxKm_->setValue(defaults::kStationRadiusKm);
  maxKm_->setPrefix(tr("stations within "));
  maxKm_->setSuffix(tr(" km"));
  nearestN_ = new QSpinBox(this);
  nearestN_->setObjectName(QStringLiteral("nearestN"));
  nearestN_->setRange(0, 20);
  nearestN_->setValue(3);
  nearestN_->setPrefix(tr("start with the nearest "));
  stationTable_ = plainTable(this);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->setSelectionMode(QAbstractItemView::NoSelection);
  stationTable_->setMinimumHeight(170);
  planTable_ = plainTable(this);
  planTable_->setObjectName(QStringLiteral("planTable"));
  planTable_->setSelectionMode(QAbstractItemView::NoSelection);
  planTable_->setEditTriggers(QAbstractItemView::AllEditTriggers);
  planTable_->setColumnCount(4);
  planTable_->setHorizontalHeaderLabels(
      {tr("Earthquake"), tr("Name"), tr("Stations"), tr("Note")});
  planTable_->setColumnWidth(0, 140);
  planTable_->setColumnWidth(1, 180);
  planTable_->setColumnWidth(2, 120);
  planTable_->setWordWrap(true);
  lower_ = new QStackedWidget(this);
  lower_->addWidget(stationTable_);
  lower_->addWidget(planTable_);

  auto *stationsRow = new QHBoxLayout;
  stationsRow->addWidget(maxKm_, 1);
  stationsRow->addWidget(nearestN_, 1);
  auto *form = new QFormLayout;
  form->addRow(tr("Name"), name_);
  form->addRow(tr("Span"), span_);
  form->addRow(tr("Stations"), stationsRow);

  dup_ = new QComboBox(this);
  dup_->setObjectName(QStringLiteral("duplicates"));
  dup_->addItem(tr("leave them out"), int(Duplicates::Skip));
  dup_->addItem(tr("replace them: new span and stations, same results folder"),
                int(Duplicates::Replace));
  dup_->addItem(tr("add them again as separate cases"),
                int(Duplicates::AddAgain));
  dupLabel_ = ui::buddyLabel(tr("Already in the study:"), dup_, this);
  auto *dupRow = new QHBoxLayout;
  dupRow->addWidget(dupLabel_);
  dupRow->addWidget(dup_, 1);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  ok_->setObjectName(QStringLiteral("addCases"));
  ok_->setText(tr("Add case"));
  ui::setPrimary(ok_);
  // Wide enough for "Add 88 cases" from the start: the box does not lay its
  // buttons out again when a text grows.
  ok_->setMinimumWidth(
      ok_->fontMetrics().horizontalAdvance(tr("Add %n case(s)", nullptr, 88)) +
      56);
  keep_ = buttons->addButton(tr("Add and keep open"),
                             QDialogButtonBox::ActionRole);
  keep_->setObjectName(QStringLiteral("addAndStay"));
  keep_->setAutoDefault(false);
  keep_->hide(); // shown once there is someone to hand the cases to
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(keep_, &QPushButton::clicked, this,
          &AddFromCatalogDialog::addAndStay);

  range_ = new QLabel(this);
  range_->setObjectName(QStringLiteral("fieldLabel"));
  if (!events_.empty()) {
    range_->setText(
        builtIn_
            ? tr("Built-in list of large earthquakes: %1 to %2, %3 events of "
                 "magnitude 5 and above. Enough to pick the cases; the "
                 "analyses of small earthquakes need the study's own catalog.")
                  .arg(first.toString(Qt::ISODate), last.toString(Qt::ISODate))
                  .arg(events_.size())
            : tr("The catalog covers %1 to %2 (%3 events).")
                  .arg(first.toString(Qt::ISODate), last.toString(Qt::ISODate))
                  .arg(events_.size()));
    range_->setWordWrap(true);
  }
  auto *underTable = new QHBoxLayout;
  underTable->addWidget(range_, 1);
  underTable->addWidget(more_, 0, Qt::AlignTop);
  stationNote_ = new QLabel(this);
  stationNote_->setObjectName(QStringLiteral("fieldLabel"));
  stationNote_->setWordWrap(true);
  warning_ = new QLabel(this);
  warning_->setObjectName(QStringLiteral("caseWarning"));
  warning_->setWordWrap(true);
  added_ = new QLabel(this);
  added_->setObjectName(QStringLiteral("addedNote"));
  added_->setWordWrap(true);
  added_->hide();

  // The map beside the tables: the list, then the chosen earthquake.
  preview_ = new CasePreview(this);
  auto *outer = new QHBoxLayout(this);
  auto *layout = new QVBoxLayout;
  outer->addLayout(layout, 5);
  outer->addWidget(preview_, 4);
  layout->addLayout(filter);
  layout->addLayout(whenRow);
  layout->addLayout(nearRow);
  layout->addWidget(nearNote_);
  layout->addWidget(table_, 3);
  layout->addLayout(underTable);
  layout->addLayout(form);
  layout->addWidget(lower_, 2);
  layout->addWidget(stationNote_);
  layout->addLayout(dupRow);
  layout->addWidget(warning_);
  layout->addWidget(added_);
  layout->addWidget(buttons);
  resize(1240, 800);
  ui::polishPage(this);

  ui::setHelp(minMag_, tr("Lists only earthquakes of at least this "
                          "magnitude. Lower it to see smaller events."));
  ui::setHelp(place_, tr("Lists only earthquakes whose place name in the "
                         "catalog contains this text, e.g. a province. "
                         "Capitals and Turkish letters do not matter: "
                         "\"izmir\" finds İzmir, \"dogan\" finds Doğanyol."));
  ui::setHelp(from_, tr("Lists only earthquakes on or after this day (UTC). "
                        "It starts at the first day of the catalog."));
  ui::setHelp(to_, tr("Lists only earthquakes on or before this day (UTC). "
                      "It starts at the last day of the catalog."));
  ui::setHelp(maxDepth_, tr("Lists only earthquakes no deeper than this, in "
                            "km. At 0 the depth does not matter."));
  ui::setHelp(covered_,
              haveStations_.empty()
                  ? tr("Lists only earthquakes for which a station you have "
                       "waveforms of, within the station distance below, has "
                       "data in the span before the earthquake. No station "
                       "with waveforms is known yet: add a waveform folder "
                       "on the Cases page first.")
                  : tr("Lists only earthquakes you can study with the data "
                       "you have: a station you have waveforms of lies within "
                       "the station distance below and has data in the span "
                       "before the earthquake. While the exact scan of the "
                       "waveform folders is still running, this goes by the "
                       "periods in the file names and says \"estimated\"."));
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
      tr("The catalog's earthquakes that pass the filters. Click a column "
         "heading to sort by it. Select the one to study, or several with "
         "Ctrl or Shift to add them together.")));
  more_->setToolTip(ui::helpText(
      tr("The list shows the first rows of the current order only, to stay "
         "quick. This shows four times as many.")));
  ui::setHelp(name_, tr("The case's name in tables and figures. Filled from "
                        "the earthquake's place and year; change it "
                        "freely. With several earthquakes chosen, each name "
                        "is changed in the table below."));
  ui::setHelp(span_, tr("How many days before the earthquake are studied. "
                        "It can be changed later on the Cases page."));
  ui::setHelp(maxKm_, tr("Stations of the station list within this "
                         "distance of the epicentre are offered."));
  ui::setHelp(nearestN_,
              tr("How many stations each earthquake starts with: the nearest "
                 "ones that have waveforms in the span, or, when none has, "
                 "the nearest ones whose data is still to be got. With one "
                 "earthquake chosen they are ticked in the table below, with "
                 "several they are listed per earthquake and can be changed "
                 "there."));
  stationTable_->setToolTip(ui::helpText(
      tr("Tick the stations the earthquake is studied from. Distance and "
         "azimuth are from the epicentre to the station.")));
  planTable_->setToolTip(ui::helpText(
      tr("One row per chosen earthquake, with what it would be added as. "
         "Double-click a name to change it, or a station list to type other "
         "station codes, separated by commas.")));
  ui::setHelp(dup_, tr("What happens to a chosen earthquake that is already "
                       "a case of this study. Replacing keeps the case's "
                       "folder and results and gives it the span and "
                       "stations chosen here; its finished runs then show as "
                       "stale where those changed."));
  keep_->setToolTip(ui::helpText(
      tr("Adds what is chosen and leaves this dialog open with its filters, "
         "to go on to the next earthquakes.")));

  // What was used last time in this study.
  if (settings_) {
    minMag_->setValue(settings_->value(key_ + "minMag", 5.0).toDouble());
    maxDepth_->setValue(settings_->value(key_ + "maxDepth", 0.0).toDouble());
    nearKm_->setValue(settings_->value(key_ + "nearKm", 100.0).toDouble());
    span_->setValue(settings_->value(key_ + "span", 365).toInt());
    maxKm_->setValue(
        settings_->value(key_ + "stationKm", defaults::kStationRadiusKm)
            .toDouble());
    nearestN_->setValue(settings_->value(key_ + "nearestN", 3).toInt());
    covered_->setChecked(covered_->isEnabled() &&
                         settings_->value(key_ + "covered", false).toBool());
    sortColumn_ = qBound(0, settings_->value(key_ + "sortColumn", 1).toInt(), 4);
    sortDescending_ = settings_->value(key_ + "sortDescending", true).toBool();
    const QSize size = settings_->value(key_ + "size").toSize();
    if (size.isValid())
      resize(size);
  }

  for (QDoubleSpinBox *b : {minMag_, maxDepth_, nearKm_})
    connect(b, &QDoubleSpinBox::valueChanged, this,
            &AddFromCatalogDialog::refill);
  for (QLineEdit *e : {place_, nearCodes_})
    connect(e, &QLineEdit::textChanged, this, &AddFromCatalogDialog::refill);
  for (QDateEdit *d : {from_, to_})
    connect(d, &QDateEdit::dateChanged, this, &AddFromCatalogDialog::refill);
  connect(covered_, &QCheckBox::toggled, this, &AddFromCatalogDialog::refill);
  connect(more_, &QPushButton::clicked, this, [this] {
    limit_ *= 4;
    keepChosenOnce_ = true;
    refill();
  });
  connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this,
          [this](int column) {
            if (column == sortColumn_) {
              sortDescending_ = !sortDescending_;
            } else {
              sortColumn_ = column;
              // Newest and largest first; nearest, shallowest and A first.
              sortDescending_ = column <= 1;
            }
            keepChosenOnce_ = true;
            refill();
          });
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &AddFromCatalogDialog::eventChosen);
  // The span and the distance decide what "covered" means, too.
  const auto ruleChanged = [this] {
    if (covered_->isChecked())
      refill();
    else
      eventChosen();
  };
  connect(maxKm_, &QDoubleSpinBox::valueChanged, this, ruleChanged);
  connect(span_, &QSpinBox::valueChanged, this, ruleChanged);
  connect(nearestN_, &QSpinBox::valueChanged, this, [this] {
    // A new rule starts the single choice's ticks over.
    fillStationTable(stationTable_, {}, {});
    near_.clear();
    eventChosen();
  });
  connect(stationTable_, &QTableWidget::itemChanged, this,
          &AddFromCatalogDialog::updateNotes);
  connect(planTable_, &QTableWidget::cellChanged, this,
          &AddFromCatalogDialog::planEdited);
  connect(dup_, &QComboBox::currentIndexChanged, this, [this] {
    if (chosenRows().size() > 1)
      fillPlan();
    updateNotes();
  });
  if (!nearStations.isEmpty())
    nearCodes_->setText(nearStations.join(QStringLiteral(", ")));
  connect(preview_, &CasePreview::eventClicked, this, [this](int row) {
    if (row < 0 || row >= table_->rowCount())
      return;
    // Ctrl adds to the choice or takes out of it, as in the table.
    if (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) {
      const QModelIndex index = table_->model()->index(row, 0);
      table_->selectionModel()->select(
          index, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
    } else {
      table_->selectRow(row);
    }
    table_->scrollToItem(table_->item(row, 0));
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

void AddFromCatalogDialog::setAdder(
    std::function<bool(const QList<study::Case> &)> adder) {
  adder_ = std::move(adder);
  keep_->setVisible(bool(adder_));
}

void AddFromCatalogDialog::remember() const {
  if (!settings_)
    return;
  settings_->setValue(key_ + "minMag", minMag_->value());
  settings_->setValue(key_ + "maxDepth", maxDepth_->value());
  settings_->setValue(key_ + "nearKm", nearKm_->value());
  settings_->setValue(key_ + "span", span_->value());
  settings_->setValue(key_ + "stationKm", maxKm_->value());
  settings_->setValue(key_ + "nearestN", nearestN_->value());
  settings_->setValue(key_ + "covered", covered_->isChecked());
  settings_->setValue(key_ + "sortColumn", sortColumn_);
  settings_->setValue(key_ + "sortDescending", sortDescending_);
  settings_->setValue(key_ + "size", size());
}

void AddFromCatalogDialog::done(int r) {
  remember();
  QDialog::done(r);
}

QList<int> AddFromCatalogDialog::chosenRows() const {
  QList<int> rows;
  if (!table_->selectionModel())
    return rows;
  for (const QModelIndex &i : table_->selectionModel()->selectedRows())
    if (i.row() >= 0 && i.row() < int(shown_.size()))
      rows << i.row();
  std::sort(rows.begin(), rows.end());
  return rows;
}

QString AddFromCatalogDialog::existingCase(const analysis::Event &e) const {
  for (const study::Case &c : study_.cases)
    if (!c.reference &&
        std::abs(double(c.time.toSecsSinceEpoch()) - e.epoch) < 2.0 &&
        std::abs(c.lat - e.lat) < 1e-3 && std::abs(c.lon - e.lon) < 1e-3)
      return c.id;
  return {};
}

QList<study::StationRef>
AddFromCatalogDialog::stationsNear(const analysis::Event &e,
                                   double *radiusOut) const {
  double radius = maxKm_->value();
  if (!filterStations_.empty())
    radius = std::max(radius, nearKm_->value());
  if (radiusOut)
    *radiusOut = radius;
  return study::nearestStations(stations_, e.lat, e.lon, radius,
                                std::numeric_limits<int>::max());
}

bool AddFromCatalogDialog::coveredByMine(const analysis::Event &e) const {
  const QDateTime t1 = utcOf(e.epoch);
  const QDateTime t0 = t1.addDays(-span_->value());
  for (const analysis::StationDistance &hit : analysis::stationsWithin(
           haveStations_, e.lat, e.lon, maxKm_->value())) {
    // The station's data from first to last day, before the closer look.
    const auto range = haveRange_.value(hit.station.code);
    if (range.first.isValid() &&
        (range.second < t0.date() || range.first > t1.date()))
      continue;
    study::StationRef ref;
    ref.code = hit.station.code;
    ref.lat = hit.station.lat;
    ref.lon = hit.station.lon;
    if (hasWaveformsIn(study_, ref, t0, t1))
      return true;
  }
  return false;
}

void AddFromCatalogDialog::refill() {
  const double minMag = minMag_->value();
  const QString needle = text::fold(place_->text().trimmed());
  const double from =
      double(QDateTime(from_->date(), QTime(0, 0), QTimeZone::UTC)
                 .toSecsSinceEpoch());
  const double to =
      double(QDateTime(to_->date().addDays(1), QTime(0, 0), QTimeZone::UTC)
                 .toSecsSinceEpoch());
  const double maxDepth = maxDepth_->value();
  const bool covered = covered_->isChecked();
  // What is chosen stays chosen across a new order or a wider list; a new
  // filter starts over, and the map shows the new list.
  QSet<int> keepChosen;
  if (keepChosenOnce_)
    for (int row : chosenRows())
      keepChosen.insert(shown_[static_cast<size_t>(row)]);
  keepChosenOnce_ = false;
  // Resolve the typed station codes; unknown ones are named, not ignored.
  filterStations_.clear();
  QStringList unknown;
  for (const QString &code : splitCodes(nearCodes_->text())) {
    const QString key = text::fold(code);
    const auto it = std::find_if(
        stations_.begin(), stations_.end(), [&key](const analysis::Station &s) {
          return text::fold(s.code) == key;
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
  std::vector<double> nearestKm(events_.size(), 0.0);
  for (int i = 0; i < int(events_.size()); ++i) {
    const analysis::Event &e = events_[static_cast<size_t>(i)];
    if (e.mag + 1e-9 < minMag)
      continue;
    if (e.epoch < from || e.epoch >= to)
      continue;
    if (maxDepth > 0 && e.depthKm > maxDepth)
      continue;
    if (!needle.isEmpty() && !text::fold(e.place).contains(needle))
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
      nearestKm[static_cast<size_t>(i)] = best->km;
    }
    if (covered && !coveredByMine(e))
      continue;
    shown_.push_back(i);
  }
  // In the order asked for; ties by magnitude, then the newer first.
  const int column = sortColumn_;
  const bool descending = sortDescending_;
  std::sort(shown_.begin(), shown_.end(),
            [this, column, descending, &nearestKm](int a, int b) {
              const analysis::Event &x = events_[static_cast<size_t>(a)];
              const analysis::Event &y = events_[static_cast<size_t>(b)];
              int order = 0;
              const auto cmp = [](double p, double q) {
                return p < q ? -1 : p > q ? 1 : 0;
              };
              switch (column) {
              case 0:
                order = cmp(x.epoch, y.epoch);
                break;
              case 2:
                order = cmp(x.depthKm, y.depthKm);
                break;
              case 3:
                order = cmp(nearestKm[static_cast<size_t>(a)],
                            nearestKm[static_cast<size_t>(b)]);
                break;
              case 4:
                order = QString::compare(text::fold(x.place),
                                         text::fold(y.place));
                break;
              default:
                order = cmp(x.mag, y.mag);
              }
              if (order != 0)
                return descending ? order > 0 : order < 0;
              return x.mag != y.mag ? x.mag > y.mag : x.epoch > y.epoch;
            });
  passing_ = int(shown_.size());
  if (int(shown_.size()) > limit_) {
    // Chosen rows beyond the cut stay listed, after it.
    std::vector<int> cut(shown_.begin(), shown_.begin() + limit_);
    for (auto it = shown_.begin() + limit_; it != shown_.end(); ++it)
      if (keepChosen.contains(*it))
        cut.push_back(*it);
    shown_ = cut;
  }
  {
    const QSignalBlocker block(table_);
    const QSignalBlocker blockSelection(table_->selectionModel());
    table_->clearSelection();
    table_->setRowCount(int(shown_.size()));
    for (int row = 0; row < int(shown_.size()); ++row) {
      const int ev = shown_[static_cast<size_t>(row)];
      const analysis::Event &e = events_[static_cast<size_t>(ev)];
      table_->setItem(row, 0,
                      new QTableWidgetItem(utcOf(e.epoch).toString(
                          QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
      table_->setItem(row, 1,
                      new QTableWidgetItem(QString::number(e.mag, 'f', 1)));
      table_->setItem(
          row, 2, new QTableWidgetItem(tr("%1 km").arg(e.depthKm, 0, 'f', 0)));
      table_->setItem(
          row, 3, new QTableWidgetItem(nearestText[static_cast<size_t>(ev)]));
      table_->setItem(row, 4, new QTableWidgetItem(e.place));
      if (keepChosen.contains(ev))
        table_->selectionModel()->select(
            table_->model()->index(row, 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    table_->setColumnHidden(3, !byStation);
    table_->horizontalHeader()->setSortIndicator(
        sortColumn_, sortDescending_ ? Qt::DescendingOrder : Qt::AscendingOrder);
  }
  const bool cut = passing_ > int(shown_.size());
  count_->setText(cut ? tr("%1 earthquakes, the first %2 shown")
                            .arg(passing_)
                            .arg(shown_.size())
                      : tr("%n earthquake(s)", nullptr, passing_));
  more_->setVisible(cut);
  table_->viewport()->update();
  eventChosen();
}

void AddFromCatalogDialog::eventChosen() {
  const QList<int> rows = chosenRows();
  lower_->setCurrentIndex(rows.size() > 1 ? 1 : 0);
  name_->setEnabled(rows.size() <= 1);
  if (rows.size() != 1) {
    near_.clear();
    fillStationTable(stationTable_, near_, {});
    stationNote_->clear();
    if (rows.size() > 1) {
      name_->clear();
      name_->setPlaceholderText(
          tr("%n earthquakes: each is named in the table below", nullptr,
             int(rows.size())));
      fillPlan();
    } else {
      name_->setPlaceholderText(QString());
    }
    updateNotes();
    return;
  }
  name_->setPlaceholderText(QString());
  const analysis::Event &e = events_[static_cast<size_t>(
      shown_[static_cast<size_t>(rows.first())])];
  name_->setText(study::caseFromEvent(e).name);
  // Stations stay ticked from the previous choice; the ones the events were
  // filtered by start ticked, since they are why this event is listed.
  QStringList keep;
  for (const study::StationRef &s : tickedStations(stationTable_, near_))
    keep << s.code;
  for (const analysis::Station &s : filterStations_)
    keep << s.code;
  double radius = 0;
  const QList<study::StationRef> all = stationsNear(e, &radius);
  near_ = all.mid(0, study::kStationCandidates);
  // Ticked stations beyond the cut stay offered.
  for (const study::StationRef &s : all.mid(study::kStationCandidates))
    if (keep.contains(s.code))
      near_ << s;
  const QDateTime t1 = utcOf(e.epoch);
  const QDateTime t0 = t1.addDays(-span_->value());
  // Nothing chosen yet: the rule decides.
  if (keep.isEmpty())
    keep = stationsToStartWith(study_, near_, t0, t1, nearestN_->value());
  fillStationTable(stationTable_, near_, keep,
                   [this, t0, t1](const study::StationRef &s) {
                     return waveformCell(study_, s, t0, t1);
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

QList<AddFromCatalogDialog::Choice> AddFromCatalogDialog::choices() const {
  QList<Choice> out;
  const QList<int> rows = chosenRows();
  for (int row : rows) {
    Choice ch;
    ch.event = shown_[static_cast<size_t>(row)];
    const analysis::Event &e = events_[static_cast<size_t>(ch.event)];
    ch.c = study::caseFromEvent(e);
    ch.c.spanDays = span_->value();
    ch.existingId = existingCase(e);
    if (rows.size() == 1) {
      if (!name_->text().trimmed().isEmpty())
        ch.c.name = name_->text().trimmed();
      ch.c.stations = tickedStations(stationTable_, near_);
    } else {
      const QString name = planNames_.value(ch.event).trimmed();
      if (!name.isEmpty())
        ch.c.name = name;
      const QDateTime t1 = utcOf(e.epoch);
      const QDateTime t0 = t1.addDays(-span_->value());
      if (planStations_.contains(ch.event)) {
        // Typed codes: any station of the list, however far.
        const QList<study::StationRef> all = study::nearestStations(
            stations_, e.lat, e.lon, 1e9, std::numeric_limits<int>::max());
        for (const QString &code : planStations_.value(ch.event)) {
          const QString key = text::fold(code);
          bool found = false;
          for (const study::StationRef &s : all)
            if (text::fold(s.code) == key) {
              if (std::none_of(ch.c.stations.begin(), ch.c.stations.end(),
                               [&s](const study::StationRef &x) {
                                 return x.code == s.code;
                               }))
                ch.c.stations << s;
              found = true;
              break;
            }
          if (!found)
            ch.unknown << code;
        }
      } else {
        const QList<study::StationRef> all =
            stationsNear(e).mid(0, study::kStationCandidates);
        QStringList keep;
        for (const analysis::Station &s : filterStations_)
          keep << s.code;
        if (keep.isEmpty())
          keep = stationsToStartWith(study_, all, t0, t1, nearestN_->value());
        for (const study::StationRef &s : all)
          if (keep.contains(s.code))
            ch.c.stations << s;
      }
    }
    out << ch;
  }
  return out;
}

QList<study::Case> AddFromCatalogDialog::collect() const {
  const Duplicates policy = Duplicates(dup_->currentData().toInt());
  QList<study::Case> out;
  for (const Choice &ch : choices()) {
    study::Case c = ch.c;
    if (!ch.existingId.isEmpty()) {
      if (policy == Duplicates::Skip)
        continue;
      if (policy == Duplicates::Replace)
        c.id = ch.existingId;
    }
    out << c;
  }
  return out;
}

void AddFromCatalogDialog::fillPlan() {
  fillingPlan_ = true;
  const QList<Choice> list = choices();
  const Duplicates policy = Duplicates(dup_->currentData().toInt());
  planTable_->setRowCount(int(list.size()));
  // The cells are written into, not replaced: this also runs while one of
  // them is being edited.
  const auto cell = [this](int row, int column, bool editable) {
    QTableWidgetItem *it = planTable_->item(row, column);
    if (!it) {
      it = new QTableWidgetItem;
      planTable_->setItem(row, column, it);
    }
    it->setFlags(editable ? Qt::ItemIsEnabled | Qt::ItemIsEditable |
                                Qt::ItemIsSelectable
                          : Qt::ItemIsEnabled);
    return it;
  };
  for (int row = 0; row < int(list.size()); ++row) {
    const Choice &ch = list[row];
    const analysis::Event &e = events_[static_cast<size_t>(ch.event)];
    QTableWidgetItem *what = cell(row, 0, false);
    what->setText(QStringLiteral("%1 · M %2")
                      .arg(utcOf(e.epoch).toString(QStringLiteral("yyyy-MM-dd")))
                      .arg(e.mag, 0, 'f', 1));
    what->setData(Qt::UserRole, ch.event);
    cell(row, 1, true)->setText(ch.c.name);
    QStringList codes;
    for (const study::StationRef &s : ch.c.stations)
      codes << s.code;
    codes << ch.unknown;
    cell(row, 2, true)->setText(codes.join(QStringLiteral(", ")));
    QStringList notes;
    if (!ch.existingId.isEmpty()) {
      const study::Case *old = study_.findCase(ch.existingId);
      const QString name = old ? old->name : ch.existingId;
      notes << (policy == Duplicates::Skip
                    ? tr("⚠ already a case (%1): left out").arg(name)
                    : policy == Duplicates::Replace
                          ? tr("⚠ already a case (%1): replaced").arg(name)
                          : tr("⚠ already a case (%1): added again").arg(name));
    }
    if (!ch.unknown.isEmpty())
      notes << tr("⚠ not in the station list: %1")
                   .arg(ch.unknown.join(QStringLiteral(", ")));
    if (ch.c.stations.isEmpty())
      notes << tr("⚠ no station: Indicators only");
    else {
      int lacking = 0;
      const QDateTime t1 = ch.c.time, t0 = ch.c.spanStart();
      for (const study::StationRef &s : ch.c.stations)
        lacking += hasWaveformsIn(study_, s, t0, t1) ? 0 : 1;
      if (lacking > 0)
        notes << tr("%n station(s) without waveforms in the span yet",
                    nullptr, lacking);
    }
    QTableWidgetItem *note = cell(row, 3, false);
    note->setText(notes.join(QStringLiteral(" · ")));
    note->setToolTip(note->text());
  }
  fillingPlan_ = false;
}

void AddFromCatalogDialog::planEdited(int row, int column) {
  if (fillingPlan_ || row < 0 || !planTable_->item(row, 0) ||
      !planTable_->item(row, column))
    return;
  const int ev = planTable_->item(row, 0)->data(Qt::UserRole).toInt();
  const QString typed = planTable_->item(row, column)->text();
  if (column == 1) {
    if (typed.trimmed().isEmpty())
      planNames_.remove(ev);
    else
      planNames_.insert(ev, typed.trimmed());
  } else if (column == 2) {
    planStations_.insert(ev, splitCodes(typed));
  } else {
    return;
  }
  fillPlan();
  updateNotes();
}

void AddFromCatalogDialog::updateNotes() {
  const QList<Choice> list = choices();
  const QList<study::Case> toAdd = collect();
  int already = 0;
  for (const Choice &ch : list)
    already += ch.existingId.isEmpty() ? 0 : 1;
  dup_->setVisible(already > 0);
  dupLabel_->setVisible(already > 0);
  const bool can = !toAdd.isEmpty();
  ok_->setEnabled(can);
  keep_->setEnabled(can);
  ok_->setText(toAdd.size() > 1
                   ? tr("Add %n case(s)", nullptr, int(toAdd.size()))
                   : tr("Add case"));
  ok_->setToolTip(
      can ? QString()
          : ui::helpText(list.isEmpty()
                             ? tr("Select an earthquake in the list first.")
                             : tr("Every chosen earthquake is already a case "
                                  "of this study. Choose under \"Already in "
                                  "the study\" whether to replace them or to "
                                  "add them again.")));
  QStringList notes;
  // The span, and the longest window an Indicators recipe adds before it,
  // against what the catalog covers.
  double months = 0;
  for (const study::Recipe &r : study_.recipes)
    if (r.tool == QLatin1String("indicators"))
      months = std::max(
          months, r.settings.value(QStringLiteral("window_months")).toDouble());
  double first = 0;
  if (!events_.empty())
    first = std::min_element(events_.begin(), events_.end(),
                             [](const analysis::Event &a,
                                const analysis::Event &b) {
                               return a.epoch < b.epoch;
                             })
                ->epoch;
  const auto spanStartOf = [this, months](const analysis::Event &e) {
    return e.epoch - (span_->value() + months * 30.44) * 86400.0;
  };
  if (list.size() == 1) {
    const Choice &ch = list.first();
    const analysis::Event &e = events_[static_cast<size_t>(ch.event)];
    if (!ch.existingId.isEmpty()) {
      const study::Case *old = study_.findCase(ch.existingId);
      notes << tr("⚠ This earthquake is already a case: %1.")
                   .arg(old ? old->name : ch.existingId);
    }
    if (ch.c.stations.isEmpty())
      notes << tr("⚠ No station is ticked: only the Indicators recipes will "
                  "run for this case.");
    const double need = spanStartOf(e);
    if (need < first && !builtIn_)
      notes << tr("⚠ The span%1 starts on %2, before the catalog's first "
                  "event (%3). The indicators would be computed on a "
                  "shorter period.")
                   .arg(months > 0 ? tr(" plus a %1 month window").arg(months)
                                   : QString(),
                        utcOf(need).toString(QStringLiteral("yyyy-MM-dd")),
                        utcOf(first).toString(QStringLiteral("yyyy-MM-dd")));
  } else if (list.size() > 1) {
    int bare = 0, early = 0;
    for (const Choice &ch : list) {
      bare += ch.c.stations.isEmpty() ? 1 : 0;
      early += spanStartOf(events_[static_cast<size_t>(ch.event)]) < first &&
                       !builtIn_
                   ? 1
                   : 0;
    }
    if (already > 0)
      notes << tr("⚠ %n of the chosen earthquakes are already cases.",
                  nullptr, already);
    if (bare > 0)
      notes << tr("⚠ %n of them would have no station: only the Indicators "
                  "recipes run for those.",
                  nullptr, bare);
    if (early > 0)
      notes << tr("⚠ For %n of them the span starts before the catalog's "
                  "first event; the indicators would be computed on a "
                  "shorter period.",
                  nullptr, early);
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
                .arg(utcOf(e.epoch).toString(QStringLiteral("yyyy-MM-dd")),
                     e.place);
    p.label = QStringLiteral("M %1 · %2")
                  .arg(e.mag, 0, 'f', 1)
                  .arg(utcOf(e.epoch).toString(QStringLiteral("yyyy-MM-dd")));
    list << p;
  }
  const QList<int> rows = chosenRows();
  if (rows.size() != 1) {
    QList<CoverageStation> filter;
    for (const analysis::Station &s : filterStations_)
      filter << CoverageStation{s.code, s.lat, s.lon};
    preview_->setList(list, filter, nearKm_->value(),
                      QSet<int>(rows.begin(), rows.end()));
    return;
  }
  const study::Case c = study::caseFromEvent(events_[static_cast<size_t>(
      shown_[static_cast<size_t>(rows.first())])]);
  QStringList ticked;
  for (const study::StationRef &s : tickedStations(stationTable_, near_))
    ticked << s.code;
  preview_->setCase(
      c, near_, ticked,
      [this](const study::StationRef &s) { return hasWaveformsOf(study_, s); },
      list);
}

void AddFromCatalogDialog::accept() {
  results_ = collect();
  if (results_.isEmpty())
    return;
  QDialog::accept();
}

void AddFromCatalogDialog::addAndStay() {
  const QList<study::Case> cases = collect();
  if (cases.isEmpty() || !adder_)
    return;
  QStringList names;
  for (const study::Case &c : cases)
    names << c.name;
  if (adder_(cases)) {
    added_->setText(tr("Added: %1. Choose the next ones, or close.")
                        .arg(names.join(QStringLiteral(", "))));
    planNames_.clear();
    planStations_.clear();
    table_->clearSelection();
    fillStationTable(stationTable_, {}, {});
    near_.clear();
    eventChosen();
  } else {
    added_->setText(tr("⚠ They could not be added; the Cases page says why."));
  }
  added_->show();
}

ReferenceDateDialog::ReferenceDateDialog(const study::Study &study,
                                         QWidget *parent)
    : QDialog(parent), study_(study) {
  setWindowTitle(tr("Add a reference date"));
  try {
    stations_ = study.stations();
  } catch (const std::exception &) {
    stations_.clear();
  }
  auto *note = new QLabel(
      tr("A reference date is a case with no earthquake: the same recipes "
         "run on the span before it, so it can be read beside the "
         "earthquakes."),
      this);
  note->setWordWrap(true);
  name_ = new QLineEdit(tr("Reference"), this);
  time_ = new QDateTimeEdit(this);
  time_->setObjectName(QStringLiteral("referenceTime"));
  time_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  time_->setTimeZone(QTimeZone::UTC);
  time_->setDateTime(QDateTime(QDate::currentDate().addYears(-1), QTime(0, 0),
                               QTimeZone::UTC));
  time_->setCalendarPopup(true);
  place_ = new QComboBox(this);
  place_->setObjectName(QStringLiteral("referencePlace"));
  for (const study::Case &c : study.cases)
    place_->addItem(tr("the place and stations of %1").arg(c.name), c.id);
  place_->addItem(tr("these coordinates"), QString());
  lat_ = new QDoubleSpinBox(this);
  lat_->setRange(-90, 90);
  lat_->setDecimals(4);
  lat_->setValue(defaults::kCentreLat);
  lon_ = new QDoubleSpinBox(this);
  lon_->setRange(-180, 180);
  lon_->setDecimals(4);
  lon_->setValue(defaults::kCentreLon);
  auto *coords = new QHBoxLayout;
  coords->addWidget(lat_);
  coords->addWidget(lon_);
  span_ = spanBox(this);
  span_->setSuffix(tr(" days before the date"));
  stationTable_ = plainTable(this);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->setSelectionMode(QAbstractItemView::NoSelection);
  stationTable_->setMinimumHeight(170);
  warning_ = new QLabel(this);
  warning_->setObjectName(QStringLiteral("caseWarning"));
  warning_->setWordWrap(true);

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
  preview_ = new CasePreview(this);
  auto *outer = new QHBoxLayout(this);
  auto *layout = new QVBoxLayout;
  outer->addLayout(layout, 5);
  outer->addWidget(preview_, 4);
  layout->addWidget(note);
  layout->addLayout(form);
  layout->addWidget(stationTable_, 1);
  layout->addWidget(warning_);
  layout->addWidget(buttons);
  resize(1100, 640);
  ui::polishPage(this);
  ui::setHelp(name_, tr("The case's name in tables and figures."));
  ui::setHelp(time_, tr("The date and time (UTC) that takes the place of "
                        "an earthquake's origin time: days are counted back "
                        "from it."));
  ui::setHelp(place_, tr("Where the reference date is studied: the "
                         "epicentre, zone and stations of an existing "
                         "case, or coordinates of its own with the stations "
                         "near them."));
  ui::setHelp({lat_, lon_}, tr("Latitude and longitude in degrees (north "
                               "and east positive) of the place, when it is "
                               "not taken from an existing case."));
  ui::setHelp(span_, tr("How many days before the date are studied. It can "
                        "be changed later on the Cases page."));
  stationTable_->setToolTip(ui::helpText(
      tr("Tick the stations the date is studied from. A station that has "
         "waveforms, but none in the span before this date, is not ticked to "
         "start with: a run on it could not start.")));

  connect(place_, &QComboBox::currentIndexChanged, this,
          &ReferenceDateDialog::placeChanged);
  connect(lat_, &QDoubleSpinBox::valueChanged, this,
          &ReferenceDateDialog::placeChanged);
  connect(lon_, &QDoubleSpinBox::valueChanged, this,
          &ReferenceDateDialog::placeChanged);
  connect(time_, &QDateTimeEdit::dateTimeChanged, this,
          &ReferenceDateDialog::placeChanged);
  connect(span_, &QSpinBox::valueChanged, this,
          &ReferenceDateDialog::placeChanged);
  connect(stationTable_, &QTableWidget::itemChanged, this,
          &ReferenceDateDialog::updateNotes);
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
  placeChanged();
}

study::Case ReferenceDateDialog::draft() const {
  study::Case c = study::referenceCase(
      name_->text().trimmed().isEmpty() ? tr("Reference")
                                        : name_->text().trimmed(),
      time_->dateTime().toUTC(), lat_->value(), lon_->value());
  if (const study::Case *src =
          study_.findCase(place_->currentData().toString())) {
    c.lat = src->lat;
    c.lon = src->lon;
    c.place = src->place;
    c.zone = src->zone;
  }
  c.spanDays = span_->value();
  return c;
}

void ReferenceDateDialog::placeChanged() {
  const study::Case *src = study_.findCase(place_->currentData().toString());
  lat_->setEnabled(src == nullptr);
  lon_->setEnabled(src == nullptr);
  const study::Case c = draft();
  const QDateTime t0 = c.spanStart(), t1 = c.time;
  // The stations of the case it borrows the place of come first, then the
  // nearest of the list.
  near_.clear();
  QStringList own;
  if (src)
    for (const study::StationRef &s : src->stations) {
      near_ << s;
      own << s.code;
    }
  for (const study::StationRef &s : study::nearestStations(
           stations_, c.lat, c.lon, defaults::kStationRadiusKm))
    if (!own.contains(s.code))
      near_ << s;
  // Of the borrowed stations, those that can run for this span; without a
  // case to borrow from, the nearest three that can. Never one whose data
  // lies outside the span.
  QStringList keep;
  if (src) {
    for (const study::StationRef &s : src->stations)
      if (hasWaveformsIn(study_, s, t0, t1) || !hasWaveformsOf(study_, s))
        keep << s.code;
  } else {
    keep = stationsToStartWith(study_, near_, t0, t1, 3);
  }
  fillStationTable(stationTable_, near_, keep,
                   [this, t0, t1](const study::StationRef &s) {
                     return waveformCell(study_, s, t0, t1);
                   });
  stationTable_->resizeColumnToContents(4);
  updateNotes();
}

void ReferenceDateDialog::updateNotes() {
  const study::Case c = draft();
  const QList<study::StationRef> ticked =
      tickedStations(stationTable_, near_);
  QStringList notes;
  if (ticked.isEmpty())
    notes << tr("⚠ No station is ticked: only the Indicators recipes will "
                "run for this case.");
  QStringList outside;
  for (const study::StationRef &s : ticked)
    if (hasWaveformsOf(study_, s) &&
        !hasWaveformsIn(study_, s, c.spanStart(), c.time))
      outside << s.code;
  if (!outside.isEmpty())
    notes << tr("⚠ %1: waveforms, but none in the span before this date. A "
                "run on such a station cannot start.")
                 .arg(outside.join(QStringLiteral(", ")));
  if (const study::Case *src =
          study_.findCase(place_->currentData().toString())) {
    int dropped = 0;
    for (const study::StationRef &s : src->stations)
      dropped += std::none_of(ticked.begin(), ticked.end(),
                              [&s](const study::StationRef &x) {
                                return x.code == s.code;
                              })
                     ? 1
                     : 0;
    if (dropped > 0)
      notes << tr("%n station(s) of %1 are not ticked here.", nullptr, dropped)
                   .arg(src->name);
    // Inside the span of the case it is compared with, it is no quiet
    // reference for it.
    if (!src->reference && c.time > src->spanStart() &&
        c.spanStart() < src->time)
      notes << tr("⚠ The span before this date overlaps the span of %1 "
                  "itself (%2 to %3).")
                   .arg(src->name, src->spanStart().date().toString(Qt::ISODate),
                        src->time.date().toString(Qt::ISODate));
  }
  warning_->setText(notes.join(QLatin1Char('\n')));
  warning_->setVisible(!notes.isEmpty());
  if (preview_) {
    QStringList codes;
    for (const study::StationRef &s : ticked)
      codes << s.code;
    preview_->setCase(c, near_, codes, [this](const study::StationRef &s) {
      return hasWaveformsOf(study_, s);
    });
  }
}

void ReferenceDateDialog::accept() {
  study::Case c = draft();
  // A borrowed station keeps what its case knew of it (its own folder).
  c.stations = tickedStations(stationTable_, near_);
  result_ = c;
  QDialog::accept();
}

} // namespace skg
