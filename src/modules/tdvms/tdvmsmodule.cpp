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
  newCampaign_ = new QPushButton(this);
  plan_ = new QPushButton(this);
  ingest_ = new QPushButton(this);
  refresh_ = new QPushButton(this);
  catalogs_ = new QPushButton(this);
  auto *top = new QHBoxLayout;
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

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(left);
  split->addWidget(tabs_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 3);
  ui::persistSplitter(split, ctx_.settings, QStringLiteral("tdvms/split"));
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(wantedBar_);
  layout->addLayout(top);
  layout->addWidget(split, 1);

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
  connect(wantedClose_, &QPushButton::clicked, this, [this] {
    wantedStations_.clear();
    wantedBar_->hide();
  });
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
  address_->setText(baseAddress());
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

void TdvmsWidget::setRoot(const QString &root) {
  root_->setText(root);
  ctx_.settings->setValue(QStringLiteral("tdvms/root"), root);
  refreshCampaigns();
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
    Pool pool(address_->text().trimmed(), slots_->value());
    lines = summarise(ledger, &pool);
  } catch (const std::exception &) {
    lines = summarise(ledger, nullptr);
  }
  const QJsonObject who = PoolLock::holder(address_->text().trimmed());
  if (!who.isEmpty())
    lines.prepend(tr("A run holds %1: pid %2, ledger %3")
                      .arg(address_->text().trimmed(),
                           who.value("pid").toVariant().toString(),
                           who.value("ledger").toString()) +
                  '\n');
  status_->setPlainText(lines.join('\n'));
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
  wantedBar_->setVisible(!wantedStations_.isEmpty());
  retranslate();
  applyWanted();
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
  if (!wantedStations_.isEmpty())
    dlg.prefill(wantedStation_->currentText(), wantedStart_, wantedEnd_);
  if (dlg.exec() != QDialog::Accepted)
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
  opt.address = address_->text().trimmed();
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
                                            .arg(opt.address, c.name),
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
    if (address_->text().trimmed().isEmpty())
      address_->setText(baseAddress());
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
  start_->setEnabled(have && !running_);
  stop_->setEnabled(running_ != nullptr);
  for (QWidget *w : std::initializer_list<QWidget *>{
           address_, slots_, interval_, exitWhenDone_, once_, dryRun_})
    w->setEnabled(!running_);
  QString note =
      running_ ? tr("Running. Progress and output are in the Jobs panel.")
               : (credentials().complete()
                      ? tr("Mailbox: %1").arg(credentials().user)
                      : tr("No mailbox credentials set (Mailbox…)."));
  if (!registered_.isEmpty())
    note += QLatin1Char('\n') +
            tr("%1 is now one of the study's waveform folders.")
                .arg(registered_);
  runNote_->setText(note);
  showJobs_->setEnabled(ctx_.nav != nullptr);
  wantedPlan_->setEnabled(have);
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
        wantedCase_.isEmpty()
            ? tr("Waveforms wanted: %n station(s), %1", nullptr,
                 int(wantedStations_.size()))
                  .arg(period)
            : tr("Waveforms wanted for %1: %n station(s), %2", nullptr,
                 int(wantedStations_.size()))
                  .arg(wantedCase_, period));
  }
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
