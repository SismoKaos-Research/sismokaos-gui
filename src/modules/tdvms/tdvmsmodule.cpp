#include <exception>

#include "modules/tdvms/stationmap.h"
#include "modules/tdvms/tdvmsmodule.h"

#include "core/ui.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/tdvms/casedownloads.h"
#include "modules/tdvms/coveragepane.h"
#include "modules/tdvms/fdsnpane.h"
#include "modules/tdvms/ledgermodel.h"
#include "modules/tdvms/mailboxdialog.h"
#include "modules/tdvms/plandialog.h"
#include <QSignalBlocker>
#include <QFrame>
#include "study/store.h"
#include "tdvms/catalog.h"
#include "tdvms/fetch.h"
#include "tdvms/guide.h"
#include "tdvms/plan.h"
#include "tdvms/portal.h"
#include "tdvms/report.h"
#include "tdvms/runner.h"
#include "tdvms/settings.h"
#include "tdvms/slots.h"
#include <algorithm>

namespace skg {

using namespace tdvms;

QWidget *TdvmsModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new TdvmsWidget(ctx, parent);
}

TdvmsWidget::TdvmsWidget(AppContext ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  // --- top bar: root and campaign ---------------------------------------
  root_ = new QLineEdit(this);
  rootBrowse_ = new QPushButton(this);
  campaign_ = new QComboBox(this);
  campaign_->setMinimumWidth(180);
  campaign_->setObjectName(QStringLiteral("dataCampaign"));
  newCampaign_ = new QPushButton(this);
  plan_ = new QPushButton(this);
  plan_->setObjectName(QStringLiteral("dataPlan"));
  ingest_ = new QPushButton(this);
  refresh_ = new QPushButton(this);
  catalogs_ = new QPushButton(this);
  topBar_ = new QWidget(this);
  auto *top = new QHBoxLayout(topBar_);
  top->setContentsMargins(0, 0, 0, 0);
  top->addWidget(root_, 1);
  top->addWidget(rootBrowse_);
  top->addSpacing(12);
  top->addWidget(campaign_);
  top->addWidget(newCampaign_);
  top->addWidget(plan_);
  top->addWidget(ingest_);
  top->addWidget(refresh_);
  top->addWidget(catalogs_);

  // --- what a case wants (hidden until the Cases page asks) --------------
  wantedBar_ = new QFrame(this);
  wantedBar_->setObjectName(QStringLiteral("wantedBar"));
  wantedText_ = new QLabel(wantedBar_);
  wantedText_->setObjectName(QStringLiteral("wantedText"));
  wantedStation_ = new QComboBox(wantedBar_);
  wantedStation_->setObjectName(QStringLiteral("wantedStation"));
  wantedPlan_ = new QPushButton(wantedBar_);
  wantedPlan_->setObjectName(QStringLiteral("wantedPlan"));
  wantedFdsn_ = new QPushButton(wantedBar_);
  wantedFdsn_->setObjectName(QStringLiteral("wantedFdsn"));
  wantedClose_ = new QPushButton(wantedBar_);
  auto *wantedLayout = new QHBoxLayout(wantedBar_);
  wantedLayout->setContentsMargins(0, 0, 0, 0);
  wantedLayout->addWidget(wantedText_, 1);
  wantedLayout->addWidget(wantedStation_);
  wantedLayout->addWidget(wantedPlan_);
  wantedLayout->addWidget(wantedFdsn_);
  wantedLayout->addWidget(wantedClose_);
  wantedBar_->hide();
  // --- the guided way: what a case needs, in one step ----------------------
  guide_ = new CaseDownloads(ctx_, this);
  advanced_ = new QPushButton(this);
  advanced_->setObjectName(QStringLiteral("dataAdvanced"));
  advanced_->setCheckable(true);
  advanced_->setFlat(true);
  why_ = new QLabel(this);
  why_->setObjectName(QStringLiteral("dataWhy"));
  why_->setWordWrap(true);
  showJobs_ = new QPushButton(this);
  showJobs_->setObjectName(QStringLiteral("showJobs"));

  // --- left: run controls and status -----------------------------------
  address_ = new QLineEdit(this);
  slots_ = new QSpinBox(this);
  slots_->setRange(1, 64);
  interval_ = new QSpinBox(this);
  interval_->setRange(10, 3600);
  interval_->setValue(60);
  interval_->setSuffix(tr(" s"));
  exitWhenDone_ = new QCheckBox(this);
  dryRun_ = new QCheckBox(this);
  once_ = new QCheckBox(this);
  mailbox_ = new QPushButton(this);
  start_ = new QPushButton(this);
  start_->setObjectName(QStringLiteral("dataFetch"));
  stop_ = new QPushButton(this);
  runNote_ = new QLabel(this);
  runNote_->setWordWrap(true);
  auto *runBox = new QGroupBox(this);
  runBox->setObjectName(QStringLiteral("runBox"));
  auto *form = new QFormLayout(runBox);
  form->addRow(tr("Address"), address_);
  form->addRow(tr("Slots"), slots_);
  form->addRow(tr("Interval"), interval_);
  form->addRow(QString(), exitWhenDone_);
  form->addRow(QString(), once_);
  form->addRow(QString(), dryRun_);
  auto *runButtons = new QHBoxLayout;
  runButtons->addWidget(mailbox_);
  runButtons->addStretch(1);
  runButtons->addWidget(start_);
  runButtons->addWidget(stop_);
  form->addRow(runButtons);
  form->addRow(runNote_);
  auto *jobsRow = new QHBoxLayout;
  jobsRow->addWidget(showJobs_);
  jobsRow->addStretch(1);
  form->addRow(jobsRow);
  status_ = new QPlainTextEdit(this);
  status_->setReadOnly(true);
  status_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  auto *left = new QWidget(this);
  left_ = left;
  auto *leftLayout = new QVBoxLayout(left);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->addWidget(runBox);
  leftLayout->addWidget(status_, 1);

  // --- right: chunks, coverage, fdsn -----------------------------------
  ledger_ = new LedgerModel(this);
  filter_ = new QSortFilterProxyModel(this);
  filter_->setSourceModel(ledger_);
  filter_->setFilterKeyColumn(LedgerModel::State);
  stateFilter_ = new QComboBox(this);
  table_ = new QTableView(this);
  table_->setModel(filter_);
  table_->setSortingEnabled(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(LedgerModel::Note,
                                                   QHeaderView::Stretch);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  reset_ = new QPushButton(this);
  auto *chunkBar = new QHBoxLayout;
  chunkBar->addWidget(stateFilter_);
  chunkBar->addStretch(1);
  chunkBar->addWidget(reset_);
  auto *chunks = new QWidget(this);
  auto *chunksLayout = new QVBoxLayout(chunks);
  chunksLayout->setContentsMargins(0, 0, 0, 0);
  chunksLayout->addLayout(chunkBar);
  chunksLayout->addWidget(table_, 1);
  coverage_ = new CoveragePane(ctx_.jobs, this);
  fdsn_ = new FdsnPane(ctx_.jobs, this);
  tabs_ = new QTabWidget(this);
  tabs_->addTab(chunks, QString());
  tabs_->addTab(coverage_, QString());
  tabs_->addTab(fdsn_, QString());
  stationMap_ = new StationPickMap(ctx_.theme, this);
  stationMap_->setObjectName(QStringLiteral("stationMap"));
  try {
    stationMap_->setStations(tdvms::loadStations());
  } catch (const std::exception &) {
    // No list: the map still shows what is chosen by name in its summary.
  }
  tabs_->addTab(stationMap_, QString());

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(left);
  split->addWidget(tabs_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 3);
  ui::persistSplitter(split, ctx_.settings, QStringLiteral("tdvms/split"));
  auto *advancedRow = new QHBoxLayout;
  advancedRow->addWidget(advanced_);
  advancedRow->addStretch(1);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(guide_);
  layout->addLayout(advancedRow);
  layout->addWidget(wantedBar_);
  layout->addWidget(topBar_);
  layout->addWidget(why_);
  layout->addWidget(split, 1);
  address_->installEventFilter(this);
  try {
    guide_->setPortalStations(tdvms::loadStations());
  } catch (const std::exception &) {
    // No list: every station is then asked for at the FDSN service unless
    // the user says otherwise.
  }

  // --- behaviour ----------------------------------------------------------
  connect(rootBrowse_, &QPushButton::clicked, this, &TdvmsWidget::chooseRoot);
  connect(root_, &QLineEdit::editingFinished, this,
          [this] { setRoot(root_->text()); });
  connect(campaign_, &QComboBox::currentIndexChanged, this,
          &TdvmsWidget::selectCampaign);
  connect(newCampaign_, &QPushButton::clicked, this, &TdvmsWidget::newCampaign);
  connect(plan_, &QPushButton::clicked, this, &TdvmsWidget::plan);
  connect(wantedPlan_, &QPushButton::clicked, this, &TdvmsWidget::plan);
  connect(wantedFdsn_, &QPushButton::clicked, this, [this] {
    applyWanted();
    tabs_->setCurrentWidget(fdsn_);
  });
  connect(wantedStation_, &QComboBox::currentIndexChanged, this,
          &TdvmsWidget::applyWanted);
  connect(wantedStation_, &QComboBox::currentIndexChanged, this,
          &TdvmsWidget::updateStationMap);
  connect(wantedClose_, &QPushButton::clicked, this, [this] {
    wantedStations_.clear();
    wantedBar_->hide();
    stationMap_->clearCase();
    updateStationMap();
  });
  connect(guide_, &CaseDownloads::dismissed, this, [this] {
    wantedStations_.clear();
    {
      const QSignalBlocker block(wantedStation_);
      wantedStation_->clear();
    }
    // The forms let go of the case too.
    fdsn_->prefill(QString(), QString(), QDate(), QDate());
    stationMap_->clearCase();
    updateStationMap();
    updateMode();
  });
  connect(guide_, &CaseDownloads::modeChanged, this, &TdvmsWidget::updateMode);
  connect(guide_, &CaseDownloads::runningChanged, this, [this] {
    if (guide_->running())
      ticker_->start();
    else if (!running_)
      ticker_->stop();
    reloadLedger();
    fdsn_->reload();
    updateControls();
  });
  connect(guide_, &CaseDownloads::mailboxRequested, this,
          &TdvmsWidget::mailbox);
  connect(guide_, &CaseDownloads::downloadChanged, this,
          &TdvmsWidget::showDownload);
  connect(advanced_, &QPushButton::toggled, this, &TdvmsWidget::updateMode);
  connect(fdsn_, &FdsnPane::defaultsChanged, this, [this] {
    guide_->setFdsnDefaults(fdsn_->service(), fdsn_->channels());
  });
  connect(stationMap_, &StationPickMap::toggled, this,
          &TdvmsWidget::toggleStation);
  connect(fdsn_, &FdsnPane::stationChanged, this,
          &TdvmsWidget::updateStationMap);
  connect(showJobs_, &QPushButton::clicked, this, [this] {
    if (!ctx_.nav)
      return;
    QVariantMap args;
    if (running_)
      args.insert(QStringLiteral("job"), ctx_.jobs->rowOf(running_));
    ctx_.nav->show(kNavJobsPanel, args);
  });
  connect(fdsn_, &FdsnPane::fetchStarted, this,
          &TdvmsWidget::registerDownloadFolder);
  if (ctx_.nav)
    connect(ctx_.nav, &Navigator::requested, this,
            [this](const QString &id, const QVariantMap &args) {
              if (id == QLatin1String("tdvms") &&
                  args.contains(QStringLiteral("stations")))
                setWanted(args);
            });
  connect(ingest_, &QPushButton::clicked, this, &TdvmsWidget::ingest);
  connect(refresh_, &QPushButton::clicked, this,
          &TdvmsWidget::refreshCampaigns);
  connect(catalogs_, &QPushButton::clicked, this,
          &TdvmsWidget::refreshCatalogs);
  connect(mailbox_, &QPushButton::clicked, this, &TdvmsWidget::mailbox);
  connect(start_, &QPushButton::clicked, this, &TdvmsWidget::startRun);
  connect(stop_, &QPushButton::clicked, this, &TdvmsWidget::stopRun);
  connect(reset_, &QPushButton::clicked, this, &TdvmsWidget::resetSelected);
  connect(table_, &QTableView::customContextMenuRequested, this,
          [this](const QPoint &p) {
            QMenu menu(this);
            menu.addAction(tr("Reset to pending"), this,
                           &TdvmsWidget::resetSelected);
            menu.exec(table_->viewport()->mapToGlobal(p));
          });
  connect(stateFilter_, &QComboBox::currentIndexChanged, this, [this](int i) {
    filter_->setFilterFixedString(
        i <= 0 ? QString() : stateFilter_->itemData(i).toString());
  });
  ticker_ = new QTimer(this);
  ticker_->setInterval(3000);
  connect(ticker_, &QTimer::timeout, this, &TdvmsWidget::reloadLedger);

  retranslate();
  const QString saved =
      ctx_.settings->value(QStringLiteral("tdvms/root")).toString();
  setRoot(saved.isEmpty() ? QDir::homePath() : saved);
  setAddress(baseAddress());
  guide_->setFdsnDefaults(fdsn_->service(), fdsn_->channels());
  {
    QPointer<TdvmsWidget> self(this);
    guide_->setNotify([self](const QString &t, const QString &b, bool u) {
      QMetaObject::invokeMethod(
          self.data(),
          [self, t, b, u] {
            if (self)
              self->notifyDesktop(t, b, u);
          },
          Qt::QueuedConnection);
    });
  }
  updateMode();
  int n = kDefaultSlots;
  try {
    n = slotCount();
  } catch (const std::exception &) {
  }
  slots_->setValue(n);
  updateControls();
}

// --- campaigns ------------------------------------------------------------

void TdvmsWidget::chooseRoot() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Folder with the campaigns"), root_->text());
  if (!dir.isEmpty())
    setRoot(dir);
}

void TdvmsWidget::setRoot(const QString &root, bool remember) {
  root_->setText(root);
  if (remember)
    ctx_.settings->setValue(QStringLiteral("tdvms/root"), root);
  refreshCampaigns();
}

// The study's own download in the expert view: its folder becomes the root
// shown (not the one remembered) and it is the campaign selected.
void TdvmsWidget::showDownload(const Campaign &download) {
  if (download.ledgerPath.isEmpty())
    return;
  const QString root = studyDownloadsRoot(ctx_.study->study().dir);
  if (QDir::cleanPath(root_->text()) != QDir::cleanPath(root))
    setRoot(root, false);
  else
    refreshCampaigns();
  const int i = campaign_->findData(download.ledgerPath);
  if (i >= 0 && i != campaign_->currentIndex())
    campaign_->setCurrentIndex(i);
  reloadLedger();
  fdsn_->reload();
  updateControls();
}

QString TdvmsWidget::address() const {
  return address_->hasFocus() ? address_->text().trimmed() : addressValue_;
}

void TdvmsWidget::setAddress(const QString &address) {
  addressValue_ = address.trimmed();
  address_->setText(address_->hasFocus() ? addressValue_
                                         : maskAddress(addressValue_));
}

// The address field shows a mask (this page ends up in screenshots) and
// the address itself only while it is being edited.
bool TdvmsWidget::eventFilter(QObject *o, QEvent *e) {
  if (o == address_) {
    if (e->type() == QEvent::FocusIn)
      address_->setText(addressValue_);
    else if (e->type() == QEvent::FocusOut) {
      addressValue_ = address_->text().trimmed();
      address_->setText(maskAddress(addressValue_));
    }
  }
  return QWidget::eventFilter(o, e);
}

// With a case in hand (or an unfinished download of the study) the page is
// the guided panel and the map; the expert view is one click away.
void TdvmsWidget::updateMode() {
  const bool guided = guide_->guiding();
  const bool expert = !guided || advanced_->isChecked();
  advanced_->setVisible(guided);
  topBar_->setVisible(expert);
  left_->setVisible(expert);
  why_->setVisible(expert && !why_->text().isEmpty());
  for (int i = 0; i < tabs_->count(); ++i)
    tabs_->setTabVisible(i, expert || tabs_->widget(i) == stationMap_);
  if (!expert)
    tabs_->setCurrentWidget(stationMap_);
  wantedBar_->setVisible(!guided && !wantedStations_.isEmpty());
  advanced_->setText(advanced_->isChecked() ? tr("▾ All downloads (advanced)")
                                            : tr("▸ All downloads (advanced)"));
}

void TdvmsWidget::refreshCampaigns() {
  const QString keep = campaign_->currentData().toString();
  campaigns_ = discoverCampaigns(root_->text());
  campaign_->blockSignals(true);
  campaign_->clear();
  for (const Campaign &c : campaigns_)
    campaign_->addItem(c.name, c.ledgerPath);
  const int idx = campaign_->findData(keep);
  campaign_->setCurrentIndex(idx >= 0 ? idx : (campaigns_.isEmpty() ? -1 : 0));
  campaign_->blockSignals(false);
  selectCampaign(campaign_->currentIndex());
}

Campaign TdvmsWidget::current() const {
  const int i = campaign_->currentIndex();
  return i >= 0 && i < campaigns_.size() ? campaigns_[i] : Campaign{};
}

void TdvmsWidget::selectCampaign(int index) {
  const Campaign c =
      index >= 0 && index < campaigns_.size() ? campaigns_[index] : Campaign{};
  ledger_->setLedgerPath(c.ledgerPath);
  coverage_->setCampaign(c.ledgerPath, c.raw);
  fdsn_->setCampaign(c);
  reloadLedger();
  updateControls();
}

void TdvmsWidget::reloadLedger() {
  const Campaign c = current();
  if (c.ledgerPath.isEmpty()) {
    status_->clear();
    return;
  }
  ledger_->reload();
  Ledger ledger(c.ledgerPath);
  if (ledger.rows().isEmpty()) {
    status_->setPlainText(
        tr("%1 has no chunks yet. Plan a station.").arg(c.ledgerPath));
    return;
  }
  QStringList lines;
  try {
    Pool pool(address(), slots_->value());
    lines = summarise(ledger, &pool);
  } catch (const std::exception &) {
    lines = summarise(ledger, nullptr);
  }
  const QJsonObject who = PoolLock::holder(address());
  if (!who.isEmpty())
    lines.prepend(tr("A run holds %1: pid %2, ledger %3")
                      .arg(maskAddress(address()),
                           who.value("pid").toVariant().toString(),
                           who.value("ledger").toString()) +
                  '\n');
  // The report names the slot addresses; on screen they are masked.
  status_->setPlainText(maskAddresses(lines.join('\n')));
}

void TdvmsWidget::newCampaign() {
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, tr("New campaign"),
                            tr("Name (lowercase letters, digits, - and _):"),
                            QLineEdit::Normal, QString(), &ok)
          .trimmed();
  if (!ok || name.isEmpty())
    return;
  const QString target = QFileDialog::getExistingDirectory(
      this, tr("Where to keep the downloads (cancel for the campaign folder)"),
      root_->text());
  try {
    const Campaign c = tdvms::newCampaign(root_->text(), name, target);
    // A campaign is found by its ledger file; a new one has none until the
    // first station is planned, so it would not show up. Start it empty.
    if (!QFileInfo::exists(c.ledgerPath)) {
      QFile ledger(c.ledgerPath);
      if (!ledger.open(QIODevice::WriteOnly))
        throw std::runtime_error(
            tr("cannot create %1").arg(c.ledgerPath).toStdString());
    }
    refreshCampaigns();
    campaign_->setCurrentIndex(campaign_->findData(c.ledgerPath));
  } catch (const std::exception &e) {
    QMessageBox::warning(this, tr("New campaign"), QString::fromUtf8(e.what()));
  }
}

void TdvmsWidget::setWanted(const QVariantMap &args) {
  wantedStations_ = args.value(QStringLiteral("stations")).toStringList();
  const auto day = [&](const char *key) {
    const QDateTime t = QDateTime::fromString(
        args.value(QLatin1String(key)).toString(), Qt::ISODate);
    return t.isValid() ? t.toUTC().date() : QDate();
  };
  wantedStart_ = day("start");
  wantedEnd_ = day("end");
  // Whole days are downloaded: include the day the span ends on.
  if (wantedEnd_.isValid())
    wantedEnd_ = wantedEnd_.addDays(1);
  wantedNetwork_ = args.value(QStringLiteral("network")).toString();
  wantedCase_ = args.value(QStringLiteral("case")).toString();
  {
    const QSignalBlocker block(wantedStation_);
    wantedStation_->clear();
    wantedStation_->addItems(wantedStations_);
  }
  // With a study open the guided panel takes the case; without one the bar
  // offers the two forms, as before.
  if (ctx_.study && ctx_.study->isOpen())
    guide_->setCase(wantedCase_, wantedStations_, wantedStart_, wantedEnd_,
                    wantedNetwork_);
  wantedBar_->setVisible(!wantedStations_.isEmpty() && !guide_->guiding());
  retranslate();
  applyWanted();
  // Where the case is, when the sender said so.
  if (args.contains(QStringLiteral("lat")) &&
      args.contains(QStringLiteral("lon")))
    stationMap_->setCase(wantedCase_, args.value(QStringLiteral("lat")).toDouble(),
                         args.value(QStringLiteral("lon")).toDouble(),
                         args.value(QStringLiteral("zone_km")).toDouble());
  else
    stationMap_->clearCase();
  updateStationMap();
}

// What the map shows as chosen: the stations wanted, and the one typed in
// the FDSN form.
void TdvmsWidget::updateStationMap() {
  QStringList chosen = wantedStations_;
  const QString typed = fdsn_->station();
  if (!typed.isEmpty() && !chosen.contains(typed, Qt::CaseInsensitive))
    chosen << typed;
  if (!planStation_.isEmpty() &&
      !chosen.contains(planStation_, Qt::CaseInsensitive))
    chosen << planStation_;
  stationMap_->setSelected(chosen, wantedStations_.isEmpty()
                                       ? typed
                                       : wantedStation_->currentText());
}

void TdvmsWidget::toggleStation(const QString &code) {
  const auto at = [this](const QString &c) {
    for (int i = 0; i < wantedStations_.size(); ++i)
      if (wantedStations_[i].compare(c, Qt::CaseInsensitive) == 0)
        return i;
    return -1;
  };
  const int i = at(code);
  const bool typedOnly =
      i < 0 && fdsn_->station().compare(code, Qt::CaseInsensitive) == 0;
  if (i >= 0)
    wantedStations_.removeAt(i);
  else if (!typedOnly)
    wantedStations_ << code;
  {
    const QSignalBlocker block(wantedStation_);
    wantedStation_->clear();
    wantedStation_->addItems(wantedStations_);
    if (i < 0 && !typedOnly)
      wantedStation_->setCurrentIndex(int(wantedStations_.size()) - 1);
  }
  // The guided list follows the map.
  if (guide_->mode() == CaseDownloads::Mode::Case)
    guide_->setStations(wantedStations_);
  wantedBar_->setVisible(!wantedStations_.isEmpty() && !guide_->guiding());
  retranslate();
  // The forms follow: on the station just chosen, or off the one taken out.
  if (wantedStations_.isEmpty() || typedOnly)
    fdsn_->prefill(wantedStations_.isEmpty() ? QString()
                                             : wantedStation_->currentText(),
                   QString(), QDate(), QDate());
  else
    applyWanted();
  updateStationMap();
}

void TdvmsWidget::applyWanted() {
  if (wantedStations_.isEmpty())
    return;
  fdsn_->prefill(wantedStation_->currentText(), wantedNetwork_, wantedStart_,
                 wantedEnd_);
}

bool TdvmsWidget::registerDownloadFolder(const QString &dir) {
  if (dir.isEmpty() || !ctx_.study || !ctx_.study->isOpen())
    return false;
  bool added = false;
  ctx_.study->update(
      [&](study::Study &s) { added = s.addWaveformRoot(dir); });
  if (added) {
    registered_ = dir;
    updateControls();
  }
  return added;
}

void TdvmsWidget::plan() {
  const Campaign c = current();
  if (c.ledgerPath.isEmpty())
    return;
  PlanDialog dlg(c.ledgerPath, this);
  // The station being planned shows on the map while the dialog is open.
  connect(&dlg, &PlanDialog::stationPicked, this, [this](const QString &code) {
    planStation_ = code;
    updateStationMap();
  });
  if (!wantedStations_.isEmpty())
    dlg.prefill(wantedStation_->currentText(), wantedStart_, wantedEnd_);
  const int answer = dlg.exec();
  planStation_.clear();
  updateStationMap();
  if (answer != QDialog::Accepted)
    return;
  const PlanRequest req = dlg.request();
  if (req.station.isEmpty())
    return;
  const QString ledgerPath = c.ledgerPath;
  QPointer<TdvmsWidget> self(this);
  ctx_.jobs->add(new TaskJob(
      tr("plan %1").arg(req.station), QStringLiteral("tdvms"),
      tr("%1 from %2 to %3 in %4-day windows")
          .arg(req.station, req.startIso, req.endIso)
          .arg(req.chunkDays),
      [req, ledgerPath, self](TaskContext &ctx) -> int {
        if (req.verify) {
          auto http = makeQtHttp();
          TdvmsPortal portal(http.get(), 30000);
          try {
            const QStringList codes = portal.stationCodes();
            if (!codes.contains(req.station)) {
              QStringList near;
              for (const QString &c : codes)
                if (c.startsWith(req.station.left(2)) && near.size() < 8)
                  near << c;
              ctx.log(
                  tr("the portal does not list '%1'. It lists %2 stations as "
                     "bare codes. "
                     "Closest by prefix: %3. Untick the check to plan it "
                     "anyway.")
                      .arg(req.station)
                      .arg(codes.size())
                      .arg(near.isEmpty() ? tr("(none)") : near.join(", ")));
              return 1;
            }
            const QString net = portal.stationNetwork(req.station);
            const QString dev = portal.deviceCode(req.station);
            if (net != QLatin1String("TU") || dev != QLatin1String("H"))
              ctx.log(tr("%1 is network %2, instrument %3%4")
                          .arg(req.station, net, dev,
                               kStrongMotion.contains(net)
                                   ? tr(" -- strong motion, an accelerometer "
                                        "rather than a broadband")
                                   : QString()));
          } catch (const std::exception &e) {
            ctx.log(tr("could not reach the station list (%1); planning %2 "
                       "unverified")
                        .arg(e.what(), req.station));
          }
        }
        Ledger ledger(ledgerPath);
        try {
          const int added = enumerateChunks(
              ledger, req.station, req.startIso, req.endIso, req.chunkDays,
              req.everyDays > 0 ? req.everyDays : -1);
          const int pending =
              ledger.inState({QStringLiteral("pending")}).size();
          ctx.log(tr("planned %1 new chunk(s) for %2; ledger holds %3 "
                     "chunk(s), %4 pending")
                      .arg(added)
                      .arg(req.station)
                      .arg(ledger.rows().size())
                      .arg(pending));
        } catch (const std::exception &e) {
          ctx.log(QString::fromUtf8(e.what()));
          return 1;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self] {
              if (self)
                self->reloadLedger();
            },
            Qt::QueuedConnection);
        return 0;
      }));
}

void TdvmsWidget::ingest() {
  const Campaign c = current();
  if (c.ledgerPath.isEmpty())
    return;
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Archive to file against its window"), c.raw,
      tr("Archives (*.zip *.part);;All files (*)"));
  if (file.isEmpty())
    return;
  Ledger ledger(c.ledgerPath);
  IngestResult r = ingestFromDisk(ledger, file, c.raw, false);
  if (!r.ok && r.message.contains(QLatin1String("matches no chunk; kept as"))) {
    if (QMessageBox::question(
            this, tr("Ingest"),
            tr("%1\nOverwrite the existing archive?").arg(r.message)) ==
        QMessageBox::Yes)
      r = ingestFromDisk(ledger, file, c.raw, true);
  }
  if (r.ok)
    QMessageBox::information(this, tr("Ingest"), r.message);
  else
    QMessageBox::warning(this, tr("Ingest"), r.message);
  reloadLedger();
}

void TdvmsWidget::resetSelected() {
  const Campaign c = current();
  const auto rows = table_->selectionModel()->selectedRows();
  if (c.ledgerPath.isEmpty() || rows.isEmpty())
    return;
  QStringList ids;
  for (const QModelIndex &idx : rows)
    ids << chunkId(ledger_->rowAt(filter_->mapToSource(idx).row()));
  if (QMessageBox::question(
          this, tr("Reset to pending"),
          tr("Put %n chunk(s) back to pending?\n%1", nullptr, ids.size())
              .arg(ids.join('\n'))) != QMessageBox::Yes)
    return;
  Ledger ledger(c.ledgerPath);
  for (const QString &cid : ids)
    ledger.release(cid, tr("reset by hand"));
  reloadLedger();
}

// --- running ----------------------------------------------------------------

void TdvmsWidget::startRun() {
  const Campaign c = current();
  if (c.ledgerPath.isEmpty() || running_)
    return;
  RunOptions opt;
  opt.address = address();
  opt.slotCount = slots_->value();
  opt.intervalSeconds = interval_->value();
  opt.once = once_->isChecked();
  opt.exitWhenDone = exitWhenDone_->isChecked();
  opt.supervisor.dryRun = dryRun_->isChecked();
  opt.mailbox = credentials();
  opt.notifyTargets = notifyTargets();
  if (opt.address.isEmpty()) {
    QMessageBox::warning(
        this, tr("Run"),
        tr("A base address is needed (the mailbox user, usually)."));
    return;
  }
  try {
    poolAddresses(opt.address, opt.slotCount);
  } catch (const std::exception &e) {
    QMessageBox::warning(this, tr("Run"), QString::fromUtf8(e.what()));
    return;
  }
  if (!opt.mailbox.complete() && !opt.supervisor.dryRun &&
      QMessageBox::question(
          this, tr("Run"),
          tr("No mailbox credentials are set, so links cannot be read and "
             "nothing will be downloaded. Submit anyway?")) != QMessageBox::Yes)
    return;
  // What this run downloads belongs to the open study.
  if (!opt.supervisor.dryRun)
    registerDownloadFolder(c.raw);
  QPointer<TdvmsWidget> self(this);
  Notify desktop = [self](const QString &t, const QString &b, bool u) {
    QMetaObject::invokeMethod(
        self.data(),
        [self, t, b, u] {
          if (self)
            self->notifyDesktop(t, b, u);
        },
        Qt::QueuedConnection);
  };
  running_ = ctx_.jobs->add(new TaskJob(tr("run %1").arg(c.name),
                                        QStringLiteral("tdvms"),
                                        tr("%1 slot(s) from %2 on %3")
                                            .arg(opt.slotCount)
                                            .arg(maskAddress(opt.address),
                                                 c.name),
                                        campaignWork(c, opt, desktop)));
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running) {
      running_ = nullptr;
      ticker_->stop();
      reloadLedger();
      updateControls();
    }
  });
  ticker_->start();
  updateControls();
}

void TdvmsWidget::stopRun() {
  if (running_)
    running_->cancel();
}

void TdvmsWidget::mailbox() {
  MailboxDialog dlg(ctx_.jobs, this);
  if (dlg.exec() == QDialog::Accepted) {
    if (address().isEmpty())
      setAddress(baseAddress());
    try {
      slots_->setValue(slotCount());
    } catch (const std::exception &) {
    }
    reloadLedger();
  }
}

void TdvmsWidget::refreshCatalogs() {
  ctx_.jobs->add(new TaskJob(
      tr("catalog refresh"), QStringLiteral("tdvms"),
      tr("stations from the portal, M5+ events from USGS"),
      [](TaskContext &ctx) -> int {
        try {
          auto http = makeQtHttp();
          const auto [stations, events] = tdvms::refreshCatalogs(*http);
          ctx.log(tr("%1: %2 stations; %3: %4 events")
                      .arg(stationsPath())
                      .arg(stations)
                      .arg(eventsPath())
                      .arg(events));
          return 0;
        } catch (const std::exception &e) {
          ctx.log(QString::fromUtf8(e.what()));
          return 1;
        }
      }));
}

void TdvmsWidget::notifyDesktop(const QString &title, const QString &body,
                                bool urgent) {
  if (!tray_ && QSystemTrayIcon::isSystemTrayAvailable()) {
    tray_ = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_DriveNetIcon),
                                this);
    tray_->show();
  }
  if (tray_)
    tray_->showMessage(title, body,
                       urgent ? QSystemTrayIcon::Warning
                              : QSystemTrayIcon::Information,
                       10000);
}

void TdvmsWidget::updateControls() {
  const bool have = !current().ledgerPath.isEmpty();
  for (QWidget *w : std::initializer_list<QWidget *>{plan_, ingest_, reset_})
    w->setEnabled(have);
  const bool guidedRun = guide_->portalRunning();
  start_->setEnabled(have && !running_ && !guidedRun);
  stop_->setEnabled(running_ != nullptr);
  for (QWidget *w : std::initializer_list<QWidget *>{
           address_, slots_, interval_, exitWhenDone_, once_, dryRun_})
    w->setEnabled(!running_);
  QString note =
      running_ ? tr("Running. Progress and output are in the Jobs panel.")
               : (credentials().complete()
                      ? tr("Mailbox: %1").arg(maskAddress(credentials().user))
                      : tr("No mailbox credentials set (Mailbox…)."));
  if (!registered_.isEmpty())
    note += QLatin1Char('\n') +
            tr("%1 is now one of the study's waveform folders.")
                .arg(registered_);
  runNote_->setText(note);
  showJobs_->setEnabled(ctx_.nav != nullptr);
  wantedPlan_->setEnabled(have);
  // Why buttons are off, said on the page and not left to be guessed.
  QStringList why;
  if (!have)
    why << tr("Plan station, Ingest archive, Reset and Fetch need a "
              "campaign: choose one in the list above, or make one with "
              "New campaign…. For a case of the open study, the panel at "
              "the top does all of this in one step.");
  else if (running_)
    why << tr("A run is in progress: Fetch and the run settings are off "
              "until it ends or you press Stop.");
  else if (guidedRun)
    why << tr("The study's download is asking AFAD with this mailbox "
              "address: Fetch is off until it ends.");
  if (!running_ && have)
    why << tr("Stop is off because nothing is running.");
  why_->setText(why.join(QLatin1Char(' ')));
  why_->setVisible(!why_->text().isEmpty() && topBar_->isVisibleTo(this));
  if (!wantedStations_.isEmpty() && !have && !guide_->guiding())
    wantedPlan_->setToolTip(ui::helpText(
        tr("Plan from AFAD needs a campaign: open or create one below.")));
}

void TdvmsWidget::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void TdvmsWidget::retranslate() {
  ui::setEmptyHint(table_,
                   tr("No campaign open.\nChoose a data folder, then New "
                      "campaign… to plan the stations and dates to download."));
  root_->setPlaceholderText(tr("folder holding the campaigns"));
  rootBrowse_->setText(QStringLiteral("…"));
  newCampaign_->setText(tr("New campaign…"));
  plan_->setText(tr("Plan station…"));
  ingest_->setText(tr("Ingest archive…"));
  showJobs_->setText(tr("Show in the Jobs panel"));
  showJobs_->setToolTip(ui::helpText(
      tr("Opens the Jobs panel at the bottom of the window, where every "
         "download shows its progress and its full log.")));
  if (!wantedStations_.isEmpty()) {
    const QString period =
        wantedStart_.isValid() && wantedEnd_.isValid()
            ? tr("%1 to %2")
                  .arg(wantedStart_.toString(Qt::ISODate),
                       wantedEnd_.addDays(-1).toString(Qt::ISODate))
            : QString();
    wantedText_->setText(
        period.isEmpty()
            ? tr("Stations chosen on the map: %n. Set the period in the "
                 "form you use.",
                 nullptr, int(wantedStations_.size()))
        : wantedCase_.isEmpty()
            ? tr("Waveforms wanted: %n station(s), %1", nullptr,
                 int(wantedStations_.size()))
                  .arg(period)
            : tr("Waveforms wanted for %1: %n station(s), %2", nullptr,
                 int(wantedStations_.size()))
                  .arg(wantedCase_, period));
  }
  if (!wantedStations_.isEmpty() && current().ledgerPath.isEmpty())
    wantedText_->setText(
        wantedText_->text() + QLatin1Char(' ') +
        tr("Plan from AFAD needs a campaign: open or create one below."));
  advanced_->setToolTip(ui::helpText(
      tr("Shows every download with its requests, the run settings, the "
         "coverage view and the FDSN form: the expert view of this page.")));
  wantedPlan_->setText(tr("Plan from AFAD…"));
  wantedFdsn_->setText(tr("Fill the FDSN form"));
  wantedClose_->setText(tr("Dismiss"));
  ui::setHelp(wantedStation_,
              tr("The station of the case to download next. The plan dialog "
                 "and the FDSN form start on it and on the case's span."));
  wantedPlan_->setToolTip(ui::helpText(
      tr("Opens the plan dialog on this station and the case's span, to "
         "request the archives from AFAD's portal.")));
  wantedFdsn_->setToolTip(ui::helpText(
      tr("Puts this station and the case's span into the FDSN tab, to pull "
         "the days from an FDSN service.")));
  advanced_->setText(advanced_->isChecked() ? tr("▾ All downloads (advanced)")
                                            : tr("▸ All downloads (advanced)"));
  refresh_->setText(tr("Refresh"));
  catalogs_->setText(tr("Refresh catalogs"));
  findChild<QGroupBox *>(QStringLiteral("runBox"))->setTitle(tr("Run"));
  exitWhenDone_->setText(tr("stop when every chunk has an outcome"));
  once_->setText(tr("one cycle only"));
  dryRun_->setText(tr("dry run (no mail flagged, no submissions)"));
  mailbox_->setText(tr("Mailbox…"));
  start_->setText(tr("Fetch"));
  ui::setPrimary(start_);
  status_->setPlaceholderText(tr("The run log appears here."));
  stop_->setText(tr("Stop"));
  reset_->setText(tr("Reset to pending"));
  tabs_->setTabText(0, tr("Chunks"));
  tabs_->setTabText(1, tr("Coverage"));
  tabs_->setTabText(2, tr("FDSN"));
  tabs_->setTabText(3, tr("Map"));
  tabs_->setTabToolTip(
      3, ui::helpText(tr("Where the stations are: every station of the "
                         "portal, and the ones chosen for download. Click a "
                         "station to choose it or take it out.")));
  const int keep = stateFilter_->currentIndex();
  stateFilter_->blockSignals(true);
  stateFilter_->clear();
  stateFilter_->addItem(tr("all states"), QString());
  for (const QString &s : kStateOrder)
    stateFilter_->addItem(s, s);
  stateFilter_->setCurrentIndex(std::max(0, keep));
  stateFilter_->blockSignals(false);

  ui::setHelp({root_, rootBrowse_},
              tr("The folder that holds your download campaigns. Each "
                 "campaign is a sub-folder under campaigns/ with its ledger "
                 "(the list of requests) and the downloaded archives."));
  ui::setHelp(campaign_, tr("The campaign shown and run: one set of "
                            "stations and time windows to download."));
  newCampaign_->setToolTip(ui::helpText(
      tr("Starts a campaign: a name and, if the archives should go to "
         "another disk, the download folder.")));
  plan_->setToolTip(ui::helpText(
      tr("Adds chunks to the campaign: a station and the time windows to "
         "request for it.")));
  ingest_->setToolTip(ui::helpText(
      tr("Files an archive you obtained by other means against the chunk "
         "whose window it matches.")));
  refresh_->setToolTip(
      ui::helpText(tr("Reads the campaign's ledger from disk again.")));
  catalogs_->setToolTip(ui::helpText(
      tr("Downloads the current station list from the portal and the M5+ "
         "events from USGS, used by the plan dialog and the coverage "
         "view.")));
  mailbox_->setToolTip(ui::helpText(
      tr("The IMAP login of the mailbox the portal writes to, the default "
         "number of slots and where to send notifications.")));
  ui::setHelp(address_,
              tr("The e-mail address requests are made with, usually the "
                 "mailbox user. The portal mails the download links to "
                 "plus-addresses of it (you+a1@…), one per slot."));
  ui::setHelp(slots_,
              tr("How many requests may be open at the portal at once. "
                 "Each slot is a plus-address of its own, so every reply "
                 "can be matched to its request. More slots download "
                 "faster, up to what the portal accepts per address."));
  ui::setHelp(interval_,
              tr("Seconds between two cycles. A cycle reads the inbox, "
                 "downloads the links that arrived, frees stale slots and "
                 "submits new requests. Default 60 s."));
  ui::setHelp(exitWhenDone_,
              tr("The run ends by itself when no chunk is pending or "
                 "submitted any more. Unticked, it keeps cycling until "
                 "you press Stop."));
  ui::setHelp(once_, tr("Does a single cycle (read the inbox, download, "
                        "free slots, submit) and stops."));
  ui::setHelp(dryRun_,
              tr("Reads the mailbox and works out what it would do, but "
                 "marks no mail as handled and submits no request. For "
                 "checking the setup."));
  start_->setToolTip(ui::helpText(
      tr("Starts the run as a job. It appears in the Jobs panel and the "
         "log below shows each cycle.")));
  stop_->setToolTip(
      ui::helpText(tr("Ends the run after the current cycle.")));
  ui::setHelp(stateFilter_,
              tr("Shows only the chunks in one state: pending (not yet "
                 "requested), submitted (waiting for the mail), fetched, "
                 "nodata, failed, retired or claimed."));
  reset_->setToolTip(ui::helpText(
      tr("Puts the selected chunks back to pending so they are requested "
         "again, for example after a failure.")));
  updateControls();
}

} // namespace skg
