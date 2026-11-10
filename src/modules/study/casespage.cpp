#include "modules/study/casespage.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <exception>

#include "core/theme.h"
#include "core/ui.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"
#include "modules/study/studyswitcher.h"
#include "study/store.h"

namespace skg {

using study::Case;
using study::Study;

CasesPage::CasesPage(AppContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  stack_ = new QStackedWidget(this);

  // No study open.
  auto *empty = new QWidget(this);
  noStudy_ = new QLabel(empty);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  newStudy_ = new QPushButton(empty);
  ui::setPrimary(newStudy_);
  openStudy_ = new QPushButton(empty);
  auto *emptyButtons = new QHBoxLayout;
  emptyButtons->addStretch(1);
  emptyButtons->addWidget(newStudy_);
  emptyButtons->addWidget(openStudy_);
  emptyButtons->addStretch(1);
  auto *emptyLayout = new QVBoxLayout(empty);
  emptyLayout->addStretch(2);
  emptyLayout->addWidget(noStudy_);
  emptyLayout->addSpacing(12);
  emptyLayout->addLayout(emptyButtons);
  emptyLayout->addStretch(3);
  stack_->addWidget(empty);

  // The study.
  auto *main = new QWidget(this);
  inputs_ = new QGroupBox(main);
  catalogLabel_ = new QLabel(inputs_);
  dataLabel_ = new QLabel(inputs_);
  catalog_ = new QLineEdit(inputs_);
  catalog_->setReadOnly(true);
  dataRoot_ = new QLineEdit(inputs_);
  dataRoot_->setReadOnly(true);
  catalogBrowse_ = new QPushButton(QStringLiteral("…"), inputs_);
  dataBrowse_ = new QPushButton(QStringLiteral("…"), inputs_);
  ui::setCompact(catalogBrowse_);
  ui::setCompact(dataBrowse_);
  catalogTime_ = new QComboBox(inputs_);
  catalogTime_->addItem(QString(), 0.0);
  catalogTime_->addItem(QString(), 3.0);
  auto *grid = new QGridLayout(inputs_);
  grid->addWidget(catalogLabel_, 0, 0);
  grid->addWidget(catalog_, 0, 1);
  grid->addWidget(catalogBrowse_, 0, 2);
  grid->addWidget(catalogTime_, 0, 3);
  grid->addWidget(dataLabel_, 1, 0);
  grid->addWidget(dataRoot_, 1, 1);
  grid->addWidget(dataBrowse_, 1, 2);
  grid->setColumnStretch(1, 1);

  add_ = new QPushButton(main);
  ui::setPrimary(add_);
  addRef_ = new QPushButton(main);
  remove_ = new QPushButton(main);
  summary_ = new QLabel(main);
  summary_->setObjectName(QStringLiteral("fieldLabel"));
  auto *bar = new QHBoxLayout;
  bar->addWidget(add_);
  bar->addWidget(addRef_);
  bar->addWidget(remove_);
  bar->addStretch(1);
  bar->addWidget(summary_);

  table_ = new QTableWidget(main);
  table_->setObjectName(QStringLiteral("caseTable"));
  table_->setColumnCount(7);
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);

  auto *card = new QFrame(main);
  card->setObjectName(QStringLiteral("card"));
  detail_ = card;
  name_ = new QLineEdit(card);
  name_->setObjectName(QStringLiteral("caseTitle"));
  facts_ = new QLabel(card);
  facts_->setObjectName(QStringLiteral("fieldLabel"));
  facts_->setWordWrap(true);
  map_ = new CaseMap(ctx_.theme, card);
  stationsTitle_ = new QLabel(card);
  stationsTitle_->setObjectName(QStringLiteral("cardTitle"));
  stationTable_ = new QTableWidget(card);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->verticalHeader()->hide();
  stationTable_->setShowGrid(false);
  stationTable_->setAlternatingRowColors(true);
  stationTable_->setSelectionMode(QAbstractItemView::NoSelection);
  stationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  stationTable_->horizontalHeader()->setStretchLastSection(true);
  stationTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                         Qt::AlignVCenter);
  stationTable_->setMinimumHeight(150);
  stationTable_->setMaximumHeight(210);
  zoneLabel_ = new QLabel(card);
  zoneLabel_->setObjectName(QStringLiteral("fieldLabel"));
  zone_ = new QComboBox(card);
  zone_->addItem(QString(), int(study::Zone::Kind::Circle));
  zone_->addItem(QString(), int(study::Zone::Kind::Fault));
  radius_ = new QDoubleSpinBox(card);
  radius_->setRange(1, 2000);
  radius_->setDecimals(0);
  spanLabel_ = new QLabel(card);
  spanLabel_->setObjectName(QStringLiteral("fieldLabel"));
  span_ = new QSpinBox(card);
  span_->setRange(1, 3650);
  auto *zoneRow = new QHBoxLayout;
  zoneRow->addWidget(zoneLabel_);
  zoneRow->addWidget(zone_, 1);
  zoneRow->addWidget(radius_);
  zoneRow->addSpacing(12);
  zoneRow->addWidget(spanLabel_);
  zoneRow->addWidget(span_);
  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(16, 14, 16, 14);
  cardLayout->setSpacing(8);
  cardLayout->addWidget(name_);
  cardLayout->addWidget(facts_);
  cardLayout->addWidget(map_, 1);
  cardLayout->addWidget(stationsTitle_);
  cardLayout->addWidget(stationTable_);
  cardLayout->addLayout(zoneRow);

  auto *split = new QSplitter(Qt::Horizontal, main);
  split->addWidget(table_);
  split->addWidget(card);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);
  split->setChildrenCollapsible(false);
  table_->setMinimumWidth(420);
  split->setSizes({560, 520});

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addWidget(inputs_);
  mainLayout->addLayout(bar);
  mainLayout->addWidget(split, 1);
  stack_->addWidget(main);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  connect(newStudy_, &QPushButton::clicked, this,
          [this] { StudySwitcher::newStudy(this, ctx_.study); });
  connect(openStudy_, &QPushButton::clicked, this,
          [this] { StudySwitcher::openStudy(this, ctx_.study); });
  connect(catalogBrowse_, &QPushButton::clicked, this,
          &CasesPage::chooseCatalog);
  connect(dataBrowse_, &QPushButton::clicked, this, &CasesPage::chooseDataRoot);
  connect(catalogTime_, &QComboBox::activated, this, [this](int i) {
    const double h = catalogTime_->itemData(i).toDouble();
    ctx_.study->update([h](Study &s) { s.catalogUtcOffsetHours = h; });
  });
  connect(add_, &QPushButton::clicked, this, &CasesPage::addFromCatalog);
  connect(addRef_, &QPushButton::clicked, this, &CasesPage::addReference);
  connect(remove_, &QPushButton::clicked, this, &CasesPage::removeCase);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &CasesPage::showCase);
  connect(name_, &QLineEdit::editingFinished, this, &CasesPage::saveCase);
  connect(stationTable_, &QTableWidget::itemChanged, this,
          &CasesPage::saveCase);
  connect(zone_, &QComboBox::activated, this, &CasesPage::saveCase);
  connect(radius_, &QDoubleSpinBox::editingFinished, this,
          &CasesPage::saveCase);
  connect(span_, &QSpinBox::editingFinished, this, &CasesPage::saveCase);
  connect(ctx_.study, &study::StudyStore::changed, this, &CasesPage::refresh);
  connect(ctx_.theme, &Theme::changed, this, &CasesPage::showCase);

  retranslate();
  refresh();
}

void CasesPage::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refresh();
  }
  QWidget::changeEvent(e);
}

void CasesPage::retranslate() {
  noStudy_->setText(
      tr("A study runs the same analyses on several earthquakes and puts "
         "the results side by side.\nStart a new study in an empty folder, "
         "or open an existing one."));
  newStudy_->setText(tr("New study…"));
  openStudy_->setText(tr("Open study…"));
  inputs_->setTitle(tr("Study inputs"));
  catalogLabel_->setText(tr("Earthquake catalog"));
  dataLabel_->setText(tr("Waveform folder"));
  catalog_->setPlaceholderText(tr("AFAD catalog export, CSV or .gz"));
  dataRoot_->setPlaceholderText(
      tr("a folder with one sub-folder of miniSEED per station"));
  catalogTime_->setItemText(0, tr("times in UTC"));
  catalogTime_->setItemText(1, tr("times in Turkey time (+3)"));
  add_->setText(tr("Add from catalog…"));
  addRef_->setText(tr("Add reference date…"));
  remove_->setText(tr("Remove"));
  table_->setHorizontalHeaderLabels({tr("Case"), tr("Type"),
                                     tr("Origin time (UTC)"), tr("M"),
                                     tr("Depth"), tr("Stations"), tr("Span")});
  ui::setEmptyHint(table_,
                   tr("No cases yet.\nChoose the earthquake catalog above, "
                      "then Add from catalog…"));
  stationsTitle_->setText(tr("STATIONS NEAR THE EPICENTRE"));
  zoneLabel_->setText(tr("Catalog zone"));
  zone_->setItemText(0, tr("Circle around the epicentre, radius"));
  zone_->setItemText(1, tr("Fault zone (East Anatolian), buffer"));
  radius_->setSuffix(tr(" km"));
  spanLabel_->setText(tr("Span"));
  span_->setSuffix(tr(" days"));
}

QString CasesPage::currentId() const {
  const int row = table_->currentRow();
  if (row < 0 || table_->selectedItems().isEmpty())
    return {};
  return table_->item(row, 0)->data(Qt::UserRole).toString();
}

const std::vector<analysis::Station> &CasesPage::stationList() {
  const QString path = ctx_.study->study().stationListPath();
  if (path != stationsFrom_) {
    stationsFrom_ = path;
    try {
      stations_ = analysis::loadStations(path);
    } catch (const std::exception &) {
      stations_.clear(); // cases keep the stations they already name
    }
  }
  return stations_;
}

void CasesPage::refresh() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  if (!open)
    return;
  const Study &s = ctx_.study->study();
  const QString keep = currentId();
  updating_ = true;
  catalog_->setText(s.catalogPath());
  dataRoot_->setText(s.resolved(s.dataRoot));
  catalogTime_->setCurrentIndex(s.catalogUtcOffsetHours == 3.0 ? 1 : 0);
  add_->setEnabled(!s.catalog.isEmpty());
  table_->setRowCount(int(s.cases.size()));
  int rows = 0, select = -1;
  for (int i = 0; i < int(s.cases.size()); ++i) {
    const Case &c = s.cases[i];
    rows += int(c.stations.size());
    QStringList codes;
    for (const study::StationRef &st : c.stations)
      codes << st.code;
    const QStringList cells = {
        c.name,
        c.reference ? tr("reference") : tr("earthquake"),
        c.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
        std::isnan(c.magnitude) ? QStringLiteral("–")
                                : QString::number(c.magnitude, 'f', 1),
        std::isnan(c.depthKm) ? QStringLiteral("–")
                              : tr("%1 km").arg(c.depthKm, 0, 'f', 0),
        codes.isEmpty() ? QStringLiteral("–")
                        : codes.join(QStringLiteral(", ")),
        tr("%n day(s)", nullptr, c.spanDays)};
    for (int col = 0; col < int(cells.size()); ++col) {
      auto *item = new QTableWidgetItem(cells[col]);
      item->setData(Qt::UserRole, c.id);
      table_->setItem(i, col, item);
    }
    if (c.id == keep)
      select = i;
  }
  table_->resizeColumnsToContents();
  if (select < 0 && !s.cases.isEmpty())
    select = 0;
  if (select >= 0)
    table_->selectRow(select);
  summary_->setText(
      tr("%1 cases · %2 station rows").arg(s.cases.size()).arg(rows));
  updating_ = false;
  showCase();
}

void CasesPage::showCase() {
  if (updating_ || !ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const Case *c = s.findCase(currentId());
  detail_->setEnabled(c != nullptr);
  remove_->setEnabled(c != nullptr);
  if (!c) {
    updating_ = true;
    name_->clear();
    facts_->clear();
    map_->clear();
    stationTable_->setRowCount(0);
    updating_ = false;
    return;
  }
  updating_ = true;
  name_->setText(c->name);
  const QString when =
      c->time.toString(QStringLiteral("yyyy-MM-dd HH:mm")) + tr(" UTC");
  const QString where = QStringLiteral("%1°N, %2°E")
                            .arg(c->lat, 0, 'f', 3)
                            .arg(c->lon, 0, 'f', 3);
  facts_->setText(
      c->reference ? tr("Reference date · %1 · %2").arg(when, where)
                   : tr("M %1 · %2 · depth %3 km · %4%5")
                         .arg(c->magnitude, 0, 'f', 1)
                         .arg(when)
                         .arg(c->depthKm, 0, 'f', 0)
                         .arg(where, c->place.isEmpty()
                                         ? QString()
                                         : QStringLiteral(" · ") + c->place));
  // Candidates: the nearest stations, and whatever the case already uses.
  // The case's own stations come first.
  candidates_ = c->stations;
  QStringList ticked;
  for (const study::StationRef &st : c->stations)
    ticked << st.code;
  for (const study::StationRef &o :
       study::nearestStations(stationList(), c->lat, c->lon, 200.0))
    if (!ticked.contains(o.code))
      candidates_ << o;
  fillStationTable(stationTable_, candidates_, ticked);
  zone_->setCurrentIndex(c->zone.kind == study::Zone::Kind::Fault ? 1 : 0);
  radius_->setValue(c->zone.kind == study::Zone::Kind::Fault
                        ? c->zone.bufferKm
                        : c->zone.radiusKm);
  span_->setValue(c->spanDays);
  const QList<QColor> colors = ctx_.theme->seriesColors();
  int index = 0;
  for (int i = 0; i < int(s.cases.size()); ++i)
    if (s.cases[i].id == c->id)
      index = i;
  map_->setCase(*c, candidates_, colors[index % colors.size()]);
  updating_ = false;
}

void CasesPage::saveCase() {
  if (updating_ || !ctx_.study->isOpen())
    return;
  const QString id = currentId();
  if (id.isEmpty())
    return;
  const QString name = name_->text().trimmed();
  const QList<study::StationRef> stations =
      tickedStations(stationTable_, candidates_);
  const bool fault = zone_->currentIndex() == 1;
  const double km = radius_->value();
  const int span = span_->value();
  QString error;
  if (!ctx_.study->update(
          [&](Study &s) {
            for (Case &c : s.cases) {
              if (c.id != id)
                continue;
              if (!name.isEmpty())
                c.name = name;
              // A ticked station keeps what the case already knew about
              // it (its data folder).
              QList<study::StationRef> next;
              for (study::StationRef st : stations) {
                for (const study::StationRef &old : c.stations)
                  if (old.code == st.code)
                    st.dataDir = old.dataDir;
                next << st;
              }
              c.stations = next;
              c.zone.kind =
                  fault ? study::Zone::Kind::Fault : study::Zone::Kind::Circle;
              (fault ? c.zone.bufferKm : c.zone.radiusKm) = km;
              c.spanDays = span;
            }
          },
          &error))
    QMessageBox::warning(this, tr("Study"), error);
}

void CasesPage::addFromCatalog() {
  try {
    AddFromCatalogDialog dialog(ctx_.study->study(), this);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
      return;
    Case c = *dialog.result();
    QString error;
    QString id;
    if (!ctx_.study->update(
            [&](Study &s) {
              c.id = s.freeCaseId(c.name);
              id = c.id;
              s.cases << c;
            },
            &error)) {
      QMessageBox::warning(this, tr("Study"), error);
      return;
    }
    for (int i = 0; i < table_->rowCount(); ++i)
      if (table_->item(i, 0)->data(Qt::UserRole).toString() == id)
        table_->selectRow(i);
  } catch (const std::exception &e) {
    QMessageBox::warning(this, tr("Earthquake catalog"),
                         QString::fromUtf8(e.what()));
  }
}

void CasesPage::addReference() {
  ReferenceDateDialog dialog(ctx_.study->study(), this);
  if (dialog.exec() != QDialog::Accepted || !dialog.result())
    return;
  Case c = *dialog.result();
  QString error, id;
  if (!ctx_.study->update(
          [&](Study &s) {
            c.id = s.freeCaseId(c.name);
            id = c.id;
            s.cases << c;
          },
          &error)) {
    QMessageBox::warning(this, tr("Study"), error);
    return;
  }
  for (int i = 0; i < table_->rowCount(); ++i)
    if (table_->item(i, 0)->data(Qt::UserRole).toString() == id)
      table_->selectRow(i);
}

void CasesPage::removeCase() {
  const QString id = currentId();
  const Case *c = ctx_.study->study().findCase(id);
  if (!c)
    return;
  if (QMessageBox::question(
          this, tr("Remove case"),
          tr("Remove %1 from the study? Its result folders stay on disk.")
              .arg(c->name)) != QMessageBox::Yes)
    return;
  ctx_.study->update([&id](Study &s) {
    s.cases.removeIf([&id](const Case &x) { return x.id == id; });
  });
}

void CasesPage::chooseCatalog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Earthquake catalog"), ctx_.study->study().dir,
      tr("AFAD catalog (*.csv *.gz);;All files (*)"));
  if (path.isEmpty())
    return;
  ctx_.study->update([&path](Study &s) { s.catalog = s.stored(path); });
}

void CasesPage::chooseDataRoot() {
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Waveform folder"), ctx_.study->study().dir);
  if (path.isEmpty())
    return;
  ctx_.study->update([&path](Study &s) { s.dataRoot = s.stored(path); });
}

} // namespace skg
