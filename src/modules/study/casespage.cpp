#include "modules/study/casespage.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QPointer>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <exception>

#include "core/defaults.h"
#include "core/theme.h"
#include "core/ui.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"
#include "modules/study/coveragedialog.h"
#include "modules/study/studysetup.h"
#include "modules/study/studyswitcher.h"
#include "modules/study/waveforms.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/catalogs.h"
#include "study/runs.h"
#include "tdvms/catalog.h"
#include "study/store.h"
#include "study/waveindex.h"

namespace skg {

using study::Case;
using study::Study;

std::function<bool(QWidget *, const QString &, const QString &)>
    CasesPage::confirm =
        [](QWidget *parent, const QString &title, const QString &text) {
          return QMessageBox::question(parent, title, text) == QMessageBox::Yes;
        };

int CasesPage::runsMadeStale(const Study &before, const Study &after,
                             const QString &caseId) {
  const Case *c0 = before.findCase(caseId);
  const Case *c1 = after.findCase(caseId);
  if (!c0 || !c1)
    return 0;
  QStringList rows;
  for (const study::StationRef &st : c0->stations)
    rows << st.code;
  if (rows.isEmpty())
    rows << QString();
  int n = 0;
  for (const study::Recipe &r : before.recipes)
    for (const QString &station : std::as_const(rows)) {
      if (study::cellOf(before, *c0, station, r).state !=
          study::CellState::Done)
        continue;
      // A station that is unticked keeps its results on disk; it is the
      // runs that stay in the table and stop being current that count.
      bool kept = station.isEmpty();
      for (const study::StationRef &st : c1->stations)
        kept = kept || st.code == station;
      if (kept && study::cellOf(after, *c1, station, r).state !=
                      study::CellState::Done)
        ++n;
    }
  return n;
}

CasesPage::CasesPage(AppContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  scan_ = new WaveScan(ctx_, this);
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
  catalog_->setObjectName(QStringLiteral("studyCatalog"));
  catalog_->setReadOnly(true);
  waveSummary_ = new QLabel(inputs_);
  waveSummary_->setObjectName(QStringLiteral("waveSummary"));
  waveSummary_->setWordWrap(true);
  catalogBrowse_ = new QPushButton(inputs_);
  catalogBrowse_->setObjectName(QStringLiteral("catalogOwn"));
  catalogGet_ = new QPushButton(inputs_);
  catalogGet_->setObjectName(QStringLiteral("catalogGet"));
  catalogUpdate_ = new QPushButton(inputs_);
  catalogUpdate_->setObjectName(QStringLiteral("catalogUpdate"));
  setup_ = new QPushButton(inputs_);
  setup_->setObjectName(QStringLiteral("studyInputs"));
  waveButton_ = new QPushButton(inputs_);
  waveButton_->setObjectName(QStringLiteral("waveformsButton"));
  catalogTime_ = new QComboBox(inputs_);
  catalogTime_->setObjectName(QStringLiteral("catalogTime"));
  catalogTime_->addItem(QString(), 0.0);
  catalogTime_->addItem(QString(), 3.0);
  catalogTime_->addItem(QString(), QVariant()); // "Other…"
  auto *grid = new QGridLayout(inputs_);
  grid->addWidget(catalogLabel_, 0, 0);
  grid->addWidget(catalog_, 0, 1);
  auto *catalogButtons = new QHBoxLayout;
  catalogButtons->addWidget(catalogUpdate_);
  catalogButtons->addWidget(catalogBrowse_);
  catalogButtons->addWidget(catalogGet_);
  catalogButtons->addWidget(catalogTime_);
  auto *waveButtons = new QHBoxLayout;
  waveButtons->addStretch(1);
  waveButtons->addWidget(waveButton_);
  waveButtons->addWidget(setup_);
  grid->addLayout(catalogButtons, 0, 2);
  grid->addWidget(dataLabel_, 1, 0);
  grid->addWidget(waveSummary_, 1, 1);
  grid->addLayout(waveButtons, 1, 2);
  grid->setColumnStretch(1, 1);

  banner_ = new QLabel(main);
  banner_->setObjectName(QStringLiteral("banner"));
  banner_->setWordWrap(true);
  banner_->hide();

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
  coverageButton_ = new QPushButton(main);
  coverageButton_->setObjectName(QStringLiteral("stationCoverage"));
  bar->addWidget(coverageButton_);
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
  stationsTitle_ = new QLabel(card);
  stationsTitle_->setObjectName(QStringLiteral("cardTitle"));
  stationTable_ = new QTableWidget(card);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->verticalHeader()->hide();
  stationTable_->setShowGrid(false);
  stationTable_->setAlternatingRowColors(true);
  stationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  stationTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  stationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  stationTable_->horizontalHeader()->setStretchLastSection(true);
  stationTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                         Qt::AlignVCenter);
  stationTable_->setMinimumHeight(120);
  stationHint_ = new QLabel(card);
  stationHint_->setObjectName(QStringLiteral("stationHint"));
  stationHint_->setWordWrap(true);
  folder_ = new QPushButton(card);
  folder_->setObjectName(QStringLiteral("stationFolder"));
  folderClear_ = new QPushButton(card);
  getWave_ = new QPushButton(card);
  getWave_->setObjectName(QStringLiteral("getWaveforms"));
  next_ = new QPushButton(card);
  next_->setObjectName(QStringLiteral("nextRecipes"));
  auto *stationButtons = new QHBoxLayout;
  stationButtons->addWidget(folder_);
  stationButtons->addWidget(folderClear_);
  stationButtons->addStretch(1);
  stationButtons->addWidget(getWave_);
  stationButtons->addWidget(next_);
  zoneLabel_ = new QLabel(card);
  zoneLabel_->setObjectName(QStringLiteral("fieldLabel"));
  zone_ = new QComboBox(card);
  zone_->addItem(QString(), int(study::Zone::Kind::Circle));
  zone_->addItem(QString(), int(study::Zone::Kind::Fault));
  radius_ = new QDoubleSpinBox(card);
  radius_->setObjectName(QStringLiteral("caseRadius"));
  radius_->setRange(1, 2000);
  radius_->setDecimals(0);
  spanLabel_ = new QLabel(card);
  spanLabel_->setObjectName(QStringLiteral("fieldLabel"));
  span_ = new QSpinBox(card);
  span_->setObjectName(QStringLiteral("caseSpan"));
  span_->setRange(1, 3650);
  // The detail of the selected case: what it is on the left, its stations on
  // the right.
  auto *about = new QVBoxLayout;
  about->setSpacing(8);
  about->addWidget(name_);
  about->addWidget(facts_);
  auto *zoneRow = new QHBoxLayout;
  zoneRow->addWidget(zoneLabel_);
  zoneRow->addWidget(zone_, 1);
  zoneRow->addWidget(radius_);
  about->addLayout(zoneRow);
  auto *spanRow = new QHBoxLayout;
  spanRow->addWidget(spanLabel_);
  spanRow->addWidget(span_, 1);
  about->addLayout(spanRow);
  about->addStretch(1);
  auto *stationsCol = new QVBoxLayout;
  stationsCol->setSpacing(8);
  stationsCol->addWidget(stationsTitle_);
  stationsCol->addWidget(stationTable_, 1);
  stationsCol->addWidget(stationHint_);
  stationsCol->addLayout(stationButtons);
  auto *cardLayout = new QHBoxLayout(card);
  cardLayout->setContentsMargins(16, 14, 16, 14);
  cardLayout->setSpacing(20);
  cardLayout->addLayout(about, 2);
  cardLayout->addLayout(stationsCol, 3);

  // The overview: every case on one map, beside the table.
  auto *mapCard = new QFrame(main);
  mapCard->setObjectName(QStringLiteral("card"));
  map_ = new CaseMap(ctx_.theme, mapCard);
  auto *mapLayout = new QVBoxLayout(mapCard);
  mapLayout->setContentsMargins(16, 12, 16, 12);
  mapLayout->addWidget(map_);

  auto *top = new QSplitter(Qt::Horizontal, main);
  top->addWidget(table_);
  top->addWidget(mapCard);
  top->setStretchFactor(0, 1);
  top->setStretchFactor(1, 1);
  top->setChildrenCollapsible(false);
  table_->setMinimumWidth(380);
  top->setSizes({520, 560});
  auto *split = new QSplitter(Qt::Vertical, main);
  split->addWidget(top);
  split->addWidget(card);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);
  split->setChildrenCollapsible(false);
  split->setSizes({390, 350});

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addWidget(banner_);
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
  connect(catalogGet_, &QPushButton::clicked, this, &CasesPage::getCatalog);
  connect(catalogUpdate_, &QPushButton::clicked, this,
          &CasesPage::updateBuiltIn);
  connect(setup_, &QPushButton::clicked, this, &CasesPage::openSetup);
  connect(waveButton_, &QPushButton::clicked, this, &CasesPage::openWaveforms);
  connect(catalogTime_, &QComboBox::activated, this, &CasesPage::offsetChosen);
  connect(folder_, &QPushButton::clicked, this,
          &CasesPage::chooseStationFolder);
  connect(folderClear_, &QPushButton::clicked, this,
          &CasesPage::clearStationFolder);
  connect(getWave_, &QPushButton::clicked, this, &CasesPage::getWaveforms);
  connect(next_, &QPushButton::clicked, this, [this] {
    if (ctx_.nav)
      ctx_.nav->show(QStringLiteral("runs"));
  });
  connect(stationTable_, &QTableWidget::itemSelectionChanged, this,
          &CasesPage::updateStationButtons);
  connect(scan_, &WaveScan::updated, this, [this] {
    updateWaveSummary();
    showCase();
  });
  connect(add_, &QPushButton::clicked, this, &CasesPage::addFromCatalog);
  connect(addRef_, &QPushButton::clicked, this, &CasesPage::addReference);
  connect(remove_, &QPushButton::clicked, this, &CasesPage::removeCase);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &CasesPage::showCase);
  connect(coverageButton_, &QPushButton::clicked, this, [this] {
    CoverageDialog dialog(ctx_, this);
    dialog.exec();
  });
  // The map and the tables select each other.
  connect(map_, &CaseMap::caseClicked, this, [this](const QString &id) {
    for (int i = 0; i < table_->rowCount(); ++i)
      if (table_->item(i, 0)->data(Qt::UserRole).toString() == id) {
        table_->selectRow(i);
        table_->scrollToItem(table_->item(i, 0));
      }
  });
  connect(map_, &CaseMap::stationClicked, this, [this](const QString &code) {
    for (int i = 0; i < stationTable_->rowCount(); ++i)
      if (stationTable_->item(i, 1) &&
          stationTable_->item(i, 1)->text() == code) {
        stationTable_->selectRow(i);
        stationTable_->scrollToItem(stationTable_->item(i, 1));
      }
  });
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
  dataLabel_->setText(tr("Waveforms"));
  catalog_->setPlaceholderText(tr("AFAD catalog export, CSV or .gz"));
  catalogBrowse_->setText(tr("Use my own file…"));
  catalogGet_->setText(tr("Get the catalog…"));
  catalogUpdate_->setText(tr("Update…"));
  setup_->setText(tr("Study inputs…"));
  waveButton_->setText(tr("Waveforms…"));
  catalogTime_->setItemText(0, tr("times in UTC"));
  catalogTime_->setItemText(1, tr("times in Turkey time (+3)"));
  catalogTime_->setItemText(2, tr("another offset…"));
  folder_->setText(tr("Choose folder…"));
  folderClear_->setText(tr("Use the study's folders"));
  add_->setText(tr("Add from catalog…"));
  addRef_->setText(tr("Add reference date…"));
  remove_->setText(tr("Remove"));
  coverageButton_->setText(tr("Station coverage…"));
  coverageButton_->setToolTip(ui::helpText(
      tr("A map of what your stations cover: the area within a distance of "
         "the stations you have waveforms of (or of the stations ticked in "
         "the cases), and which earthquakes of the catalog fall inside "
         "it.")));
  table_->setHorizontalHeaderLabels({tr("Case"), tr("Type"),
                                     tr("Origin time (UTC)"), tr("M"),
                                     tr("Depth"), tr("Stations"), tr("Span")});
  ui::setEmptyHint(table_,
                   tr("No cases yet.\nAdd from catalog… picks an "
                      "earthquake, from the built-in list or your own "
                      "catalog."));
  stationsTitle_->setText(tr("STATIONS NEAR THE EPICENTRE"));
  zoneLabel_->setText(tr("Catalog zone"));
  zone_->setItemText(0, tr("Circle around the epicentre, radius"));
  zone_->setItemText(1, tr("Fault zone (East Anatolian), buffer"));
  radius_->setSuffix(tr(" km"));
  spanLabel_->setText(tr("Span"));
  span_->setSuffix(tr(" days"));

  ui::setHelp({catalogLabel_, catalog_},
              tr("The earthquake catalog of this study. Without a file of "
                 "its own the study uses the built-in list of large "
                 "earthquakes, which is enough to pick cases. The Indicators "
                 "and the Features calibration count small earthquakes and "
                 "need a full catalog: an AFAD export, or a download."));
  catalogBrowse_->setToolTip(ui::helpText(
      tr("Chooses a catalog file of your own: an AFAD export as CSV, plain "
         "or .gz. Cases are picked from it and the analyses read their "
         "events from it.")));
  catalogGet_->setToolTip(ui::helpText(
      tr("Downloads the earthquakes around this study's cases from an FDSN "
         "event service into the study folder and makes that file the "
         "study's catalog.")));
  catalogUpdate_->setToolTip(ui::helpText(
      tr("Brings the built-in lists up to date over the network: the "
         "stations from the AFAD portal and the large earthquakes from "
         "USGS.")));
  setup_->setToolTip(ui::helpText(
      tr("The catalog and the waveform folders of the study in one dialog, "
         "as when the study was created.")));
  ui::setHelp(catalogTime_,
              tr("Whether the times in the catalog file are UTC or Turkey "
                 "time (UTC+3). AFAD exports are UTC. A wrong choice shifts "
                 "every event by three hours."));
  ui::setHelp({dataLabel_, waveSummary_, waveButton_},
              tr("For the Features recipes: the folders holding the "
                 "waveforms you already have, and what was found in them. "
                 "The station of each file is read from its miniSEED "
                 "headers, so any folder layout works. Not needed for "
                 "Indicators."));
  folder_->setToolTip(ui::helpText(
      tr("For the selected, ticked station: use the files of one folder you "
         "choose, whatever the study's waveform folders hold. For data whose "
         "headers carry a different station code.")));
  folderClear_->setToolTip(ui::helpText(
      tr("Drops the folder chosen for the selected station, so its files "
         "are looked up in the study's waveform folders again.")));
  getWave_->setToolTip(ui::helpText(
      tr("Step 2: opens the Data page with this case's stations and span "
         "filled in, to download what is missing.")));
  next_->setToolTip(ui::helpText(
      tr("Step 3: goes to the Runs page, where recipes are made and run on "
         "the cases. Features recipes need the waveforms first; Indicators "
         "recipes do not.")));
  updateStationButtons();
  add_->setToolTip(ui::helpText(
      tr("Adds a case by picking an earthquake from the study's catalog and "
         "ticking the stations near it.")));
  addRef_->setToolTip(ui::helpText(
      tr("Adds a case with no earthquake: a date at a place, studied the "
         "same way so it can be read beside the earthquakes.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected case out of the study. Its result folders "
         "stay on disk.")));
  ui::setHelp(name_, tr("The case's name, as shown in the tables and the "
                        "figures. Changes are saved at once."));
  stationTable_->setToolTip(ui::helpText(
      tr("Tick the stations this case is studied from. Each ticked station "
         "is a row of its own on the Runs and Compare pages. Waveforms says "
         "how many days of the span the study's waveform folders hold for "
         "the station.")));
  ui::setHelp({zoneLabel_, zone_},
              tr("Which catalog events count for this case in the Indicators "
                 "recipes: those within a circle around the epicentre, or "
                 "those within a buffer on both sides of the shipped East "
                 "Anatolian fault traces."));
  ui::setHelp(radius_, tr("Radius of the circle, or width of the buffer on "
                          "each side of the fault traces, in km. Larger "
                          "takes in more events."));
  ui::setHelp({spanLabel_, span_},
              tr("How many days before the event are studied. The Indicators "
                 "recipes use the catalog from one window before the span "
                 "up to, but not including, the event. Changing it marks "
                 "the case's finished runs stale."));
}

QString CasesPage::currentId() const {
  const int row = table_->currentRow();
  if (row < 0 || table_->selectedItems().isEmpty())
    return {};
  return table_->item(row, 0)->data(Qt::UserRole).toString();
}

const std::vector<analysis::Station> &CasesPage::stationList() {
  // The station list plus the study's own extra stations; read again only
  // when either changes.
  const Study &st = ctx_.study->study();
  QString key = st.stationListPath();
  for (const study::ExtraStation &e : st.extraStations)
    key += QStringLiteral("|%1:%2:%3").arg(e.code).arg(e.lat).arg(e.lon);
  if (key != stationsFrom_) {
    stationsFrom_ = key;
    try {
      stations_ = st.stations();
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
  const bool noCatalog = s.catalog.isEmpty();
  const bool lostCatalog = !noCatalog && !QFileInfo::exists(s.catalogPath());
  if (noCatalog) {
    const study::BuiltInCatalog b = study::builtInCatalog();
    // "updated" only when it was: the list as shipped is as old as the
    // release, which the date of its last earthquake bounds from below.
    catalog_->setText(
        (b.refreshed ? tr("Built-in list: M ≥ %1 since %2, updated %3")
                     : tr("Built-in list: M ≥ %1 since %2, as shipped (to %3)"))
            .arg(b.minMagnitude, 0, 'f', 0)
            .arg(b.first.year())
            .arg(b.updated.toString(Qt::ISODate)));
  } else {
    catalog_->setText(s.catalogPath());
  }
  catalog_->setCursorPosition(0);
  catalogUpdate_->setVisible(noCatalog);
  // The built-in list is in UTC; there is nothing to choose.
  catalogTime_->setVisible(!noCatalog);
  catalog_->setStyleSheet(
      lostCatalog ? QStringLiteral("color: %1;")
                        .arg(ctx_.theme->statusColor(QStringLiteral("serious"))
                                 .name())
                  : QString());
  add_->setEnabled(!lostCatalog);
  add_->setToolTip(ui::helpText(
      lostCatalog
          ? tr("The catalog file is not where the study says it is: %1. "
               "Choose it again above.")
                .arg(s.catalogPath())
          : noCatalog
                ? tr("Adds a case by picking an earthquake from the "
                     "built-in list of large earthquakes and ticking the "
                     "stations near it.")
                : tr("Adds a case by picking an earthquake from the study's "
                     "catalog and ticking the stations near it.")));
  // The offset: one of the two usual ones, or whatever the study says.
  while (catalogTime_->count() > 3)
    catalogTime_->removeItem(2);
  int offsetIndex = catalogTime_->findData(s.catalogUtcOffsetHours);
  if (offsetIndex < 0) {
    catalogTime_->insertItem(
        2,
        tr("times in UTC%1%2")
            .arg(s.catalogUtcOffsetHours < 0 ? QStringLiteral("−")
                                             : QStringLiteral("+"))
            .arg(std::abs(s.catalogUtcOffsetHours)),
        s.catalogUtcOffsetHours);
    offsetIndex = 2;
  }
  catalogTime_->setCurrentIndex(offsetIndex);
  updateWaveSummary();
  // Waveform folders that were never scanned, or that gained files since
  // (a download on the Data page): scan them, which reads only what is new.
  rescanIfChanged();
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
    map_->setStudy(s, {}); // every case, none selected
    stationTable_->setRowCount(0);
    stationHint_->clear();
    updating_ = false;
    updateStationButtons();
    return;
  }
  const QString keepStation = selectedStation();
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
  for (const study::StationRef &o : study::nearestStations(
           stationList(), c->lat, c->lon, defaults::kStationRadiusKm))
    if (!ticked.contains(o.code))
      candidates_ << o;
  const QDateTime from = c->spanStart(), to = c->time;
  fillStationTable(stationTable_, candidates_, ticked,
                   [&s, from, to](const study::StationRef &st) {
                     return waveformNote(s, st, from, to);
                   });
  stationTable_->resizeColumnToContents(4);
  for (int i = 0; i < stationTable_->rowCount(); ++i)
    if (stationTable_->item(i, 1)->text() == keepStation) {
      const QSignalBlocker block(stationTable_);
      stationTable_->selectRow(i);
    }
  int without = 0;
  for (const study::StationRef &st : c->stations)
    if (!hasWaveformsOf(s, st))
      ++without;
  stationHint_->setText(
      c->stations.isEmpty()
          ? tr("⚠ No station is ticked: only the Indicators recipes run for "
               "this case. Tick the stations it is studied from.")
          : without > 0
                ? tr("⚠ %n ticked station(s) have no waveforms in the "
                     "study's folders. Add the folder under Waveforms…, or "
                     "get the data.",
                     nullptr, without)
                : QString());
  stationHint_->setVisible(!stationHint_->text().isEmpty());
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
  Q_UNUSED(colors);
  Q_UNUSED(index);
  CaseMapSpec spec;
  spec.selectedId = c->id;
  spec.candidates = candidates_;
  spec.hasData = [&s](const study::StationRef &st) {
    return hasWaveformsOf(s, st);
  };
  map_->setStudy(s, spec);
  updating_ = false;
  updateStationButtons();
}

QString CasesPage::selectedStation() const {
  const int row = stationTable_->currentRow();
  if (row < 0 || stationTable_->selectedItems().isEmpty() ||
      !stationTable_->item(row, 1))
    return {};
  return stationTable_->item(row, 1)->text();
}

void CasesPage::updateStationButtons() {
  const Case *c = ctx_.study->isOpen()
                      ? ctx_.study->study().findCase(currentId())
                      : nullptr;
  const QString code = selectedStation();
  const study::StationRef *st = nullptr;
  if (c)
    for (const study::StationRef &x : c->stations)
      if (x.code == code)
        st = &x;
  folder_->setEnabled(st != nullptr);
  folderClear_->setEnabled(st && !st->dataDir.isEmpty());
  getWave_->setEnabled(c && !c->stations.isEmpty() && ctx_.nav);
  next_->setEnabled(c && ctx_.nav);
  // The next step follows the sidebar: Data (2) while a ticked station has
  // no waveforms, Runs (3) once they all have.
  bool lacking = false;
  if (c)
    for (const study::StationRef &x : c->stations)
      lacking = lacking || !hasWaveformsOf(ctx_.study->study(), x);
  getWave_->setText(lacking ? tr("Next: get waveforms →")
                            : tr("Get more waveforms…"));
  next_->setText(lacking || !c ? tr("Go on to recipes →")
                               : tr("Next: recipes →"));
  ui::setPrimary(getWave_, lacking);
  ui::setPrimary(next_, c && !lacking);
}

void CasesPage::rescanIfChanged() {
  const Study &s = ctx_.study->study();
  if (s.waveRoots.isEmpty() || scan_->running())
    return;
  // What the folders looked like when they were last scanned is remembered
  // between sessions, so opening the application does not scan for nothing.
  const QString key =
      QStringLiteral("waveforms/scanned/") +
      QString::fromLatin1(QCryptographicHash::hash(s.dir.toUtf8(),
                                                   QCryptographicHash::Sha1)
                              .toHex());
  QString stamp;
  for (const study::WaveRoot &r : s.waveRoots)
    stamp += r.id + QLatin1Char('=') +
             study::folderStamp(s.resolved(r.path)) + QLatin1Char(';');
  const auto index = study::WaveIndex::shared(s.dir);
  bool missing = false;
  for (const study::WaveRoot &r : s.waveRoots)
    missing = missing || !index->roots.contains(r.id);
  if (!missing && ctx_.settings->value(key).toString() == stamp)
    return;
  ctx_.settings->setValue(key, stamp);
  scan_->start();
}

void CasesPage::updateWaveSummary() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  QString text;
  if (s.waveRoots.isEmpty()) {
    text = tr("No waveform folder yet. Add the folder that holds your "
              "miniSEED files, or download with the Data page.");
  } else {
    const auto index = study::WaveIndex::shared(s.dir);
    int lost = 0;
    for (const study::WaveRoot &r : s.waveRoots)
      if (!QDir(s.resolved(r.path)).exists())
        ++lost;
    text = tr("%n folder(s)", nullptr, int(s.waveRoots.size())) +
           QStringLiteral(" · ") +
           tr("%n station(s) found", nullptr, int(index->stations().size()));
    if (lost > 0)
      text += QStringLiteral(" · ") +
              tr("⚠ %n folder(s) not found (moved?)", nullptr, lost);
    const int skipped = int(index->skipped().size());
    if (skipped > 0)
      text += QStringLiteral(" · ") + tr("%n file(s) skipped", nullptr, skipped);
    if (!scan_->status().isEmpty())
      text += QStringLiteral(" · ") + scan_->status();
    else if (index->pendingCoverage() > 0)
      text += QStringLiteral(" · ") + tr("coverage not read yet");
  }
  waveSummary_->setText(text);
}

void CasesPage::showBanner(const QString &text) {
  banner_->setStyleSheet(
      QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid "
                     "%3; border-radius: 6px; padding: 6px 10px; }")
          .arg(ctx_.theme->color(QStringLiteral("surface2")).name(),
               ctx_.theme->color(QStringLiteral("text")).name(),
               ctx_.theme->statusColor(QStringLiteral("serious")).name()));
  banner_->setText(text);
  banner_->setVisible(!text.isEmpty());
}

bool CasesPage::apply(const std::function<void(Study &)> &edit) {
  QString error;
  if (ctx_.study->update(edit, &error)) {
    if (banner_->property("saveError").toBool()) {
      banner_->setProperty("saveError", false);
      showBanner(QString());
    }
    return true;
  }
  banner_->setProperty("saveError", true);
  showBanner(tr("⚠ The study could not be saved, so the change was not "
                "kept: %1")
                 .arg(error));
  refresh(); // back to what is on disk
  return false;
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
  const auto edit = [&](Study &s) {
    for (Case &c : s.cases) {
      if (c.id != id)
        continue;
      if (!name.isEmpty())
        c.name = name;
      // A ticked station keeps what the case already knew about it (its
      // data folder).
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
  };
  // Nothing changed (a field lost focus): nothing to save or to ask.
  Study after = ctx_.study->study();
  edit(after);
  if (after.toToml() == ctx_.study->study().toToml())
    return;
  const int stale = runsMadeStale(ctx_.study->study(), after, id);
  if (stale > 0 &&
      !confirm(this, tr("Change the case"),
               tr("This change makes %n finished run(s) of this case stale; "
                  "they would have to be run again. Change it anyway?",
                  nullptr, stale))) {
    showCase(); // put the fields back
    return;
  }
  apply(edit);
}

void CasesPage::addFromCatalog() { addNear({}); }

void CasesPage::addNear(const QStringList &stations) {
  try {
    AddFromCatalogDialog dialog(ctx_.study->study(), this, stations);
    if (dialog.exec() != QDialog::Accepted || !dialog.result())
      return;
    Case c = *dialog.result();
    QString id;
    if (!apply([&](Study &s) {
          c.id = s.freeCaseId(c.name);
          id = c.id;
          s.cases << c;
        }))
      return;
    for (int i = 0; i < table_->rowCount(); ++i)
      if (table_->item(i, 0)->data(Qt::UserRole).toString() == id)
        table_->selectRow(i);
  } catch (const std::exception &e) {
    showBanner(tr("⚠ The earthquake catalog could not be read: %1")
                   .arg(QString::fromUtf8(e.what())));
  }
}

void CasesPage::addReference() {
  ReferenceDateDialog dialog(ctx_.study->study(), this);
  if (dialog.exec() != QDialog::Accepted || !dialog.result())
    return;
  Case c = *dialog.result();
  QString id;
  if (!apply([&](Study &s) {
        c.id = s.freeCaseId(c.name);
        id = c.id;
        s.cases << c;
      }))
    return;
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
  apply([&id](Study &s) {
    s.cases.removeIf([&id](const Case &x) { return x.id == id; });
  });
}

void CasesPage::chooseCatalog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Earthquake catalog"), ctx_.study->study().dir,
      tr("AFAD catalog (*.csv *.gz);;All files (*)"));
  if (path.isEmpty())
    return;
  apply([&path](Study &s) { s.catalog = s.stored(path); });
}

void CasesPage::offsetChosen(int index) {
  const Study &now = ctx_.study->study();
  double hours = catalogTime_->itemData(index).toDouble();
  if (!catalogTime_->itemData(index).isValid()) {
    bool ok = false;
    hours = QInputDialog::getDouble(
        this, tr("Catalog time"),
        tr("Hours the catalog's times are ahead of UTC:"),
        now.catalogUtcOffsetHours, -12.0, 14.0, 1, &ok);
    if (!ok) {
      refresh();
      return;
    }
  }
  const double old = now.catalogUtcOffsetHours;
  if (hours == old) {
    refresh();
    return;
  }
  int quakes = 0;
  for (const Case &c : now.cases)
    quakes += c.reference ? 0 : 1;
  // With cases in the study, their times were read with the old offset:
  // they are taken from the catalog again so they stay the same events.
  QHash<QString, QDateTime> times;
  int lost = 0;
  if (quakes > 0) {
    if (!confirm(this, tr("Catalog time"),
                 tr("The times of the %n earthquake case(s) were read from "
                    "the catalog with the old setting. They are read again "
                    "with the new one, which moves them by %1 hours and "
                    "makes their finished runs stale. Change it?",
                    nullptr, quakes)
                     .arg(old - hours))) {
      refresh();
      return;
    }
    try {
      const std::vector<analysis::Event> events =
          analysis::loadEvents(now.catalogPath(), hours);
      for (const Case &c : now.cases) {
        if (c.reference)
          continue;
        const double want = double(c.time.toSecsSinceEpoch()) +
                            (old - hours) * 3600.0;
        const analysis::Event *hit = nullptr;
        for (const analysis::Event &e : events)
          if (std::abs(e.epoch - want) < 2.0 &&
              std::abs(e.lat - c.lat) < 1e-3 && std::abs(e.lon - c.lon) < 1e-3)
            hit = &e;
        if (hit)
          times.insert(c.id, study::caseFromEvent(*hit).time);
        else
          ++lost;
      }
    } catch (const std::exception &) {
      lost = quakes; // no catalog to read them from
    }
  }
  if (apply([&](Study &s) {
        s.catalogUtcOffsetHours = hours;
        for (Case &c : s.cases)
          if (times.contains(c.id))
            c.time = times.value(c.id);
      }) &&
      lost > 0)
    showBanner(tr("⚠ %n earthquake case(s) were not found in the catalog "
                  "at the shifted time and keep the time they had. Check "
                  "them against the catalog.",
                  nullptr, lost));
}

void CasesPage::openWaveforms() {
  WaveformsDialog dialog(ctx_, scan_, this);
  dialog.exec();
  updateWaveSummary();
  showCase();
  // "Find earthquakes near these…": on to picking a case near the stations
  // that were selected there.
  if (!dialog.findNear().isEmpty())
    addNear(dialog.findNear());
}

void CasesPage::openSetup() {
  StudySetupDialog dialog(ctx_.study, this);
  dialog.exec();
}

void CasesPage::getCatalog() {
  CatalogFetchDialog dialog(ctx_.study->study(), this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QPointer<CasesPage> self(this);
  if (startCatalogFetch(ctx_, dialog.query(), dialog.serviceId(),
                        [self](bool ok, const QString &text) {
                          if (self)
                            self->showBanner((ok ? QString()
                                                 : QStringLiteral("⚠ ")) +
                                             text);
                        }))
    showBanner(tr("Downloading the catalog; progress is in the Jobs panel."));
}

void CasesPage::updateBuiltIn() {
  QPointer<CasesPage> self(this);
  Job *job = ctx_.jobs->add(new TaskJob(
      tr("catalog refresh"), QStringLiteral("tdvms"),
      tr("stations from the portal, M5+ events from USGS"),
      [](TaskContext &t) -> int {
        try {
          auto http = tdvms::makeQtHttp();
          const auto [stations, events] = tdvms::refreshCatalogs(*http);
          t.log(QStringLiteral("%1: %2; %3: %4")
                    .arg(tdvms::stationsPath())
                    .arg(stations)
                    .arg(tdvms::eventsPath())
                    .arg(events));
          return 0;
        } catch (const std::exception &e) {
          t.log(QString::fromUtf8(e.what()));
          return 1;
        }
      }));
  catalogUpdate_->setEnabled(false);
  connect(job, &Job::stateChanged, this, [self, job] {
    const Job::State st = job->state();
    if (!self || st == Job::State::Running || st == Job::State::Queued)
      return;
    self->catalogUpdate_->setEnabled(true);
    if (st == Job::State::Done) {
      self->showBanner(QString());
      self->refresh();
    } else {
      self->showBanner(tr("⚠ The built-in lists could not be updated: %1")
                           .arg(job->log().isEmpty() ? QString()
                                                     : job->log().last()));
    }
  });
}

void CasesPage::chooseStationFolder() {
  const QString id = currentId();
  const QString code = selectedStation();
  if (id.isEmpty() || code.isEmpty())
    return;
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Folder with the waveforms of %1").arg(code),
      ctx_.study->study().dir);
  if (path.isEmpty())
    return;
  apply([&](Study &s) {
    for (Case &c : s.cases)
      if (c.id == id)
        for (study::StationRef &st : c.stations)
          if (st.code == code)
            st.dataDir = s.stored(path);
  });
}

void CasesPage::clearStationFolder() {
  const QString id = currentId();
  const QString code = selectedStation();
  if (id.isEmpty() || code.isEmpty())
    return;
  apply([&](Study &s) {
    for (Case &c : s.cases)
      if (c.id == id)
        for (study::StationRef &st : c.stations)
          if (st.code == code)
            st.dataDir.clear();
  });
}

void CasesPage::getWaveforms() {
  const Case *c = ctx_.study->study().findCase(currentId());
  if (!c || !ctx_.nav)
    return;
  // What the Data page needs to plan the downloads: see DEVELOPING.
  QStringList codes;
  for (const study::StationRef &st : c->stations)
    codes << st.code;
  QVariantMap args;
  args.insert(QStringLiteral("stations"), codes);
  args.insert(QStringLiteral("start"),
              c->spanStart().toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("end"), c->time.toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("case"), c->name);
  // Where the case is, for the Data page's map.
  args.insert(QStringLiteral("lat"), c->lat);
  args.insert(QStringLiteral("lon"), c->lon);
  args.insert(QStringLiteral("zone_km"),
              c->zone.kind == study::Zone::Kind::Circle ? c->zone.radiusKm
                                                        : 0.0);
  // The network, where the waveform folders already show it.
  for (const study::WaveStation &w :
       study::WaveIndex::shared(ctx_.study->study().dir)->stations())
    if (codes.contains(w.code) && !w.network.isEmpty()) {
      args.insert(QStringLiteral("network"), w.network);
      break;
    }
  ctx_.nav->show(QStringLiteral("tdvms"), args);
}

} // namespace skg
