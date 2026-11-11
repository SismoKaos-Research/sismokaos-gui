#include "modules/tdvms/casedownloads.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "study/runs.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "tdvms/disk.h"
#include "tdvms/fdsn.h"
#include "tdvms/ledger.h"
#include "tdvms/plan.h"
#include "tdvms/slots.h"

namespace skg {

using namespace tdvms;

namespace {

enum Column { Station, Have, From, ToGet, State, ColumnCount };

QDateTime dayStartUtc(const QDate &d) {
  return QDateTime(d, QTime(0, 0), QTimeZone::utc());
}

QString sizeText(qint64 bytes) {
  return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

int daysIn(const QList<QPair<QString, QString>> &windows) {
  int n = 0;
  for (const auto &[s, e] : windows) {
    const QDate from = QDate::fromString(s.left(10), Qt::ISODate);
    const QDate to = QDate::fromString(e.left(10), Qt::ISODate);
    if (from.isValid() && to.isValid())
      n += int(from.daysTo(to));
  }
  return n;
}

} // namespace

CaseDownloads::CaseDownloads(AppContext ctx, QWidget *parent)
    : QFrame(parent), ctx_(ctx) {
  // Drawn as a card, like the other panels of the study pages.
  setObjectName(QStringLiteral("card"));
  hooks_.http = [] { return makeQtHttp(); };
  hooks_.portalRun = [](const Campaign &c, const RunOptions &o,
                        const Notify &n) { return campaignWork(c, o, n); };
  hooks_.credentials = [] { return credentials(); };
  hooks_.address = [] { return baseAddress(); };
  hooks_.addressBusy = [](const QString &address) {
    return !address.isEmpty() && !PoolLock::holder(address).isEmpty();
  };
  fdsnService_ = kKoeri;
  fdsnChannels_ = QStringLiteral("BH?");

  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("guideTitle"));
  QFont f = title_->font();
  f.setBold(true);
  title_->setFont(f);
  lead_ = new QLabel(this);
  lead_->setObjectName(QStringLiteral("guideLead"));
  lead_->setWordWrap(true);

  body_ = new QWidget(this);
  table_ = new QTableWidget(body_);
  table_->setObjectName(QStringLiteral("guideStations"));
  table_->setColumnCount(ColumnCount);
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  table_->setFocusPolicy(Qt::NoFocus);
  summary_ = new QLabel(body_);
  summary_->setObjectName(QStringLiteral("guideSummary"));
  summary_->setWordWrap(true);
  mailNote_ = new QLabel(body_);
  mailNote_->setObjectName(QStringLiteral("guideMailbox"));
  mailNote_->setWordWrap(true);
  mailButton_ = new QPushButton(body_);
  mailButton_->setObjectName(QStringLiteral("guideMailboxSetup"));
  why_ = new QLabel(body_);
  why_->setObjectName(QStringLiteral("guideWhy"));
  why_->setWordWrap(true);
  done_ = new QLabel(body_);
  done_->setObjectName(QStringLiteral("guideDone"));
  done_->setWordWrap(true);
  go_ = new QPushButton(body_);
  go_->setObjectName(QStringLiteral("guideGo"));
  stop_ = new QPushButton(body_);
  stop_->setObjectName(QStringLiteral("guideStop"));
  retry_ = new QPushButton(body_);
  retry_->setObjectName(QStringLiteral("guideRetry"));

  cases_ = new QPushButton(this);
  cases_->setObjectName(QStringLiteral("guideCases"));
  recipes_ = new QPushButton(this);
  recipes_->setObjectName(QStringLiteral("guideRecipes"));
  dismiss_ = new QPushButton(this);
  dismiss_->setObjectName(QStringLiteral("guideDismiss"));

  auto *mailRow = new QHBoxLayout;
  mailRow->addWidget(mailNote_, 1);
  mailRow->addWidget(mailButton_);
  auto *bodyLayout = new QVBoxLayout(body_);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->addWidget(table_);
  bodyLayout->addWidget(summary_);
  bodyLayout->addLayout(mailRow);
  bodyLayout->addWidget(done_);
  bodyLayout->addWidget(why_);

  auto *buttons = new QHBoxLayout;
  buttons->addWidget(go_);
  buttons->addWidget(stop_);
  buttons->addWidget(retry_);
  buttons->addStretch(1);
  buttons->addWidget(cases_);
  buttons->addWidget(recipes_);
  buttons->addWidget(dismiss_);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->addWidget(title_);
  layout->addWidget(lead_);
  layout->addWidget(body_);
  layout->addLayout(buttons);

  ticker_ = new QTimer(this);
  ticker_->setInterval(2000);
  connect(ticker_, &QTimer::timeout, this, &CaseDownloads::refresh);

  connect(go_, &QPushButton::clicked, this, &CaseDownloads::go);
  connect(stop_, &QPushButton::clicked, this, &CaseDownloads::stop);
  connect(retry_, &QPushButton::clicked, this, &CaseDownloads::retry);
  connect(mailButton_, &QPushButton::clicked, this,
          &CaseDownloads::mailboxRequested);
  connect(dismiss_, &QPushButton::clicked, this, &CaseDownloads::dismiss);
  connect(cases_, &QPushButton::clicked, this, [this] {
    if (!ctx_.nav)
      return;
    QVariantMap args;
    if (studyOpen())
      for (const study::Case &c : ctx_.study->study().cases)
        if (c.name == case_)
          args.insert(QStringLiteral("case"), c.id);
    ctx_.nav->show(QStringLiteral("cases"), args);
  });
  connect(recipes_, &QPushButton::clicked, this, [this] {
    if (ctx_.nav)
      ctx_.nav->show(QStringLiteral("runs"));
  });
  if (ctx_.study)
    connect(ctx_.study, &study::StudyStore::changed, this, [this] {
      // Another study: what was asked for belonged to the one before.
      if (!studyOpen() || studyDir() != property("studyDir").toString()) {
        case_.clear();
        stations_.clear();
        chosen_.clear();
        first_ = end_ = QDate();
        from_ = to_ = QDateTime();
        finished_.clear();
        setProperty("studyDir", studyOpen() ? studyDir() : QString());
      }
          refresh();
    });
  setProperty("studyDir", studyOpen() ? studyDir() : QString());
  retranslate();
  refresh();
}

void CaseDownloads::setHooks(const Hooks &hooks) {
  if (hooks.http)
    hooks_.http = hooks.http;
  if (hooks.portalRun)
    hooks_.portalRun = hooks.portalRun;
  if (hooks.credentials)
    hooks_.credentials = hooks.credentials;
  if (hooks.address)
    hooks_.address = hooks.address;
  if (hooks.addressBusy)
    hooks_.addressBusy = hooks.addressBusy;
  hooks_.fdsn = hooks.fdsn;
  refresh();
}

void CaseDownloads::setPortalStations(const QList<CatalogStation> &stations) {
  portal_ = stations;
  refresh();
}

void CaseDownloads::setFdsnDefaults(const QString &service,
                                    const QString &channels) {
  if (!service.trimmed().isEmpty())
    fdsnService_ = service.trimmed();
  if (!channels.trimmed().isEmpty())
    fdsnChannels_ = channels.trimmed();
}

bool CaseDownloads::studyOpen() const {
  return ctx_.study && ctx_.study->isOpen();
}

QString CaseDownloads::studyDir() const {
  return studyOpen() ? ctx_.study->study().dir : QString();
}

Campaign CaseDownloads::download() const {
  return studyOpen() ? studyDownload(studyDir(), false) : Campaign{};
}

bool CaseDownloads::mailboxReady() const {
  return hooks_.credentials().complete() &&
         !hooks_.address().trimmed().isEmpty();
}

bool CaseDownloads::portalRunning() const {
  return portalJob_ && portalJob_->state() == Job::State::Running;
}

bool CaseDownloads::running() const {
  return portalRunning() ||
         (fdsnJob_ && fdsnJob_->state() == Job::State::Running);
}

int CaseDownloads::spanDays() const {
  return first_.isValid() && end_.isValid() ? int(first_.daysTo(end_)) : 0;
}

void CaseDownloads::setCase(const QString &name, const QStringList &stations,
                            const QDateTime &from, const QDateTime &to,
                            const QString &network) {
  from_ = from.toUTC();
  to_ = to.toUTC();
  // Whole days are downloaded: from the day the span starts on to the day
  // it ends on, that one included.
  const QDate first = from_.isValid() ? from_.date() : QDate();
  const QDate dayAfterLast =
      to_.isValid()
          ? (to_.time() == QTime(0, 0) && to_ > from_ ? to_.date()
                                                      : to_.date().addDays(1))
          : QDate();
  case_ = name;
  stations_.clear();
  for (const QString &s : stations) {
    const QString code = normaliseStation(s);
    if (!code.isEmpty() && !stations_.contains(code))
      stations_ << code;
  }
  first_ = first;
  end_ = dayAfterLast;
  network_ = network.trimmed().toUpper();
  finished_.clear();
  refresh();
}

void CaseDownloads::setStations(const QStringList &stations) {
  QStringList next;
  for (const QString &s : stations) {
    const QString code = normaliseStation(s);
    if (!code.isEmpty() && !next.contains(code))
      next << code;
  }
  if (next == stations_)
    return;
  stations_ = next;
  refresh();
}

void CaseDownloads::dismiss() {
  case_.clear();
  stations_.clear();
  chosen_.clear();
  first_ = end_ = QDate();
  from_ = to_ = QDateTime();
  finished_.clear();
  refresh();
  emit dismissed();
}

// --- what is there, what is missing, what would be asked for -----------------

void CaseDownloads::compute() {
  const Mode before = mode_;
  rows_.clear();
  portalBytes_ = fdsnBytes_ = 0;
  freeBytes_ = -1;
  diskNote_.clear();
  if (!studyOpen()) {
    mode_ = Mode::Off;
    if (before != mode_)
      emit modeChanged();
    return;
  }
  const Campaign c = download();
  const QList<tdvms::Row> portalRows =
      c.ledgerPath.isEmpty() ? QList<tdvms::Row>() : Ledger(c.ledgerPath).rows();
  const QList<tdvms::Row> fdsnRows =
      c.ledgerPath.isEmpty() ? QList<tdvms::Row>()
                             : Ledger(fdsnStatePath(c)).rows();

  const bool haveCase = !stations_.isEmpty() && spanDays() > 0;
  QStringList codes = stations_;
  if (!haveCase) {
    // No case in hand: the stations whose requests are not all answered.
    codes.clear();
    for (const QString &s : stationsOf(portalRows) + stationsOf(fdsnRows))
      if (!codes.contains(s) && (progressOf(portalRows, s).open() > 0 ||
                                 progressOf(fdsnRows, s).open() > 0 ||
                                 progressOf(portalRows, s).failed > 0 ||
                                 progressOf(fdsnRows, s).failed > 0))
        codes << s;
  }
  mode_ = haveCase ? Mode::Case : (codes.isEmpty() ? Mode::Idle : Mode::Resume);

  const auto index = study::WaveIndex::shared(studyDir());
  const QString key = studyDir() + QLatin1Char('|') +
                      from_.toString(Qt::ISODate) + QLatin1Char('|') +
                      to_.toString(Qt::ISODate);
  if (indexSeen_ != index || coverKey_ != key) {
    indexSeen_ = index;
    coverKey_ = key;
    cover_.clear();
  }
  const QList<study::WaveStation> indexed = index->stations();
  const QList<QDate> days = haveCase ? daysBetween(first_, end_) : QList<QDate>();
  int portalDays = 0, fdsnDays = 0;
  for (const QString &code : codes) {
    Row row;
    row.code = code;
    row.known = defaultSource(code, portal_) == Source::Portal;
    row.network = portalNetwork(code, portal_);
    if (row.network.isEmpty())
      for (const study::WaveStation &w : indexed)
        if (w.code.compare(code, Qt::CaseInsensitive) == 0)
          row.network = w.network;
    const RequestProgress pp = progressOf(portalRows, code);
    const RequestProgress fp = progressOf(fdsnRows, code);
    if (chosen_.contains(code))
      row.source = chosen_.value(code);
    else if (!haveCase)
      row.source = pp.empty() && !fp.empty() ? Source::Fdsn : Source::Portal;
    else
      row.source = defaultSource(code, portal_);
    if (row.network.isEmpty())
      row.network = !network_.isEmpty() && row.source == Source::Fdsn
                        ? network_
                        : (row.source == Source::Fdsn ? QStringLiteral("KO")
                                                      : QString());
    if (haveCase) {
      if (!cover_.contains(code)) {
        // What the study has of the span: study::spanData, the function
        // the Cases page and the Runs cells count with, on the same span.
        Cover cv;
        const study::SpanData span = study::spanData(*index, code, from_, to_);
        cv.have = span.days;
        cv.spanDays = span.spanDays;
        cv.approximate = span.approximate;
        cv.atLeast = span.atLeast;
        cv.indexed = span.known;
        cv.thin = span.thin();
        cv.dataFirst = span.first;
        cv.dataLast = span.last;
        // Which whole days to ask for: the ones of the span with no data.
        if (cv.have == 0)
          cv.missing = days;
        else
          for (const QDate &d : days)
            if (index->daysCovered(code, dayStartUtc(d),
                                   dayStartUtc(d.addDays(1))) == 0)
              cv.missing << d;
        cover_.insert(code, cv);
      }
      const Cover &cv = cover_[code];
      row.have = cv.have;
      row.spanDays = cv.spanDays;
      row.approximate = cv.approximate;
      row.atLeast = cv.atLeast;
      row.indexed = cv.indexed;
      row.thin = cv.thin;
      row.dataFirst = cv.dataFirst;
      row.dataLast = cv.dataLast;
      row.missing = cv.missing;
      // What would be new: days nothing asks for yet. A request that
      // failed is not written a second time; Retry failed puts it back.
      const auto known = [&code](const QList<tdvms::Row> &rows) {
        QSet<QString> ids;
        for (const tdvms::Row &r : rows)
          if (str(r, "station").compare(code, Qt::CaseInsensitive) == 0)
            ids.insert(str(r, "start").left(10));
        return ids;
      };
      if (row.source == Source::Portal) {
        const QSet<QString> have = known(portalRows);
        for (const auto &w : windowsForDays(
                 first_, end_, daysNotRequested(row.missing, portalRows, code),
                 kChunkDays))
          if (!have.contains(w.first.left(10)))
            row.windows << w;
      } else if (row.source == Source::Fdsn) {
        const QSet<QString> have = known(fdsnRows);
        for (const QDate &d : daysNotRequested(row.missing, fdsnRows, code))
          if (!have.contains(d.toString(Qt::ISODate)))
            row.fdsnDays << d;
      }
    }
    row.progress = row.source == Source::Fdsn ? fp : pp;
    if (row.source == Source::Portal) {
      portalDays += daysIn(row.windows);
      for (const tdvms::Row &r : portalRows)
        if (str(r, "station").compare(code, Qt::CaseInsensitive) == 0 &&
            (str(r, "state") == QLatin1String("pending") ||
             kInFlight.contains(str(r, "state"))))
          portalDays += daysIn({{str(r, "start"), str(r, "end")}});
    } else if (row.source == Source::Fdsn) {
      fdsnDays += int(row.fdsnDays.size()) + fp.open();
    }
    rows_ << row;
  }
  portalBytes_ = estimateBytes(portalRows, portalDays, kPortalBytesPerDay);
  fdsnBytes_ = estimateBytes(fdsnRows, fdsnDays, kFdsnBytesPerDay);
  if (mode_ != Mode::Idle) {
    const QString where =
        c.ledgerPath.isEmpty() ? studyDir() : QFileInfo(c.ledgerPath).path();
    freeBytes_ = tdvms::freeBytes(where);
    diskNote_ = diskCheck(where, portalRows, std::nullopt);
  }
  if (before != mode_)
    emit modeChanged();
}

QString CaseDownloads::blockedReason() const {
  if (mode_ != Mode::Case && mode_ != Mode::Resume)
    return {};
  if (running())
    return tr("The download is running. Stop it to change what is asked for.");
  // A study this window does not hold (its folder cannot be written to, so
  // no lock could be taken) is looked at here, not downloaded into.
  if (!holdsStudy())
    return tr("This study's folder cannot be written to, so the "
              "application does not hold the study and nothing is "
              "downloaded into it. Open it from a place you can write to.");
  int toPlan = 0, open = 0, portalOpen = 0, fdsnWork = 0;
  for (const Row &r : rows_) {
    if (r.source == Source::None)
      continue;
    toPlan += int(r.windows.size()) + int(r.fdsnDays.size());
    open += r.progress.open();
    if (r.source == Source::Portal)
      portalOpen += int(r.windows.size()) + r.progress.open();
    else
      fdsnWork += int(r.fdsnDays.size()) + r.progress.open();
  }
  if (toPlan + open == 0) {
    int failed = 0;
    for (const Row &r : rows_)
      failed += r.progress.failed;
    if (failed > 0)
      return tr("Nothing is waiting. %n request(s) failed: press Retry "
                "failed to ask for them again.",
                nullptr, failed);
    return mode_ == Mode::Case
               ? tr("Nothing to download: the study has every day of the "
                    "span for these stations, or it has been asked for "
                    "already.")
               : tr("Nothing is waiting to be downloaded.");
  }
  const qint64 need = portalBytes_ + fdsnBytes_;
  if (freeBytes_ >= 0 && need > 0 && need + qint64(kFloorGb * 1e9) > freeBytes_)
    return tr("Not enough room on the disk of the study: about %1 are "
              "needed and %2 are free. Free some space, or set \"Do not "
              "download\" for some stations.")
        .arg(sizeText(need), sizeText(freeBytes_));
  if (fdsnWork == 0 && portalOpen > 0) {
    if (!mailboxReady())
      return tr("These stations are asked for at AFAD, which answers by "
                "e-mail: set up the mailbox first.");
    if (hooks_.addressBusy(hooks_.address()))
      return tr("Another download is using the mailbox address right now. "
                "It has to end before this one can ask AFAD.");
  }
  return {};
}

// --- the one click --------------------------------------------------------------

bool CaseDownloads::holdsStudy() const {
  return studyOpen() && ctx_.study->holdsLock();
}

CaseDownloads::Planned CaseDownloads::plan() {
  Planned out;
  if (!holdsStudy())
    return out;
  compute();
  bool any = false;
  for (const Row &r : rows_)
    any = any || !r.windows.isEmpty() || !r.fdsnDays.isEmpty();
  if (!any)
    return out;
  const Campaign c = studyDownload(studyDir(), true);
  Ledger portal(c.ledgerPath);
  Ledger fdsn(fdsnStatePath(c));
  for (const Row &r : rows_) {
    if (r.source == Source::Portal && !r.windows.isEmpty())
      out.portalRequests += addChunks(portal, r.code, r.windows);
    else if (r.source == Source::Fdsn && !r.fdsnDays.isEmpty())
      out.fdsnDays += fdsnPlan(fdsn, r.code, r.fdsnDays, r.network,
                               fdsnChannels_, fdsnService_);
  }
  emit downloadChanged(c);
  return out;
}

void CaseDownloads::nudgeStudy() {
  // The folders the transfers file under are the study's waveform folders.
  // Saving the study, even unchanged, is what makes the Cases page look at
  // them again and read the new files.
  const Campaign c = download();
  if (c.ledgerPath.isEmpty() || !holdsStudy())
    return;
  QStringList dirs;
  if (!Ledger(c.ledgerPath).rows().isEmpty())
    dirs << c.raw;
  if (!Ledger(fdsnStatePath(c)).rows().isEmpty())
    dirs << fdsnDefaultOut(c);
  for (const QString &d : dirs)
    QDir().mkpath(d);
  ctx_.study->update([&dirs](study::Study &s) {
    for (const QString &d : dirs)
      s.addWaveformRoot(d);
  });
}

void CaseDownloads::start() {
  if (!holdsStudy() || running())
    return;
  const Campaign c = download();
  if (c.ledgerPath.isEmpty())
    return;
  nudgeStudy();
  const QString studyName = ctx_.study->study().name;
  const auto watch = [this](Job *job) {
    connect(job, &Job::stateChanged, this, [this, job] {
      if (job->state() != Job::State::Running &&
          job->state() != Job::State::Queued)
        jobEnded();
    });
  };

  // AFAD: one run that works through every request still open.
  Ledger portal(c.ledgerPath);
  const bool portalOpen =
      !portal.inState(QStringList{QStringLiteral("pending")} + kInFlight)
           .isEmpty();
  if (portalOpen && mailboxReady() && !hooks_.addressBusy(hooks_.address())) {
    RunOptions opt;
    opt.address = hooks_.address().trimmed();
    try {
      opt.slotCount = slotCount();
    } catch (const std::exception &) {
      opt.slotCount = kDefaultSlots;
    }
    opt.exitWhenDone = true;
    opt.mailbox = hooks_.credentials();
    opt.notifyTargets = notifyTargets();
    portalJob_ = ctx_.jobs->add(new TaskJob(
        tr("waveforms from AFAD: %1").arg(studyName), QStringLiteral("tdvms"),
        tr("%1 request slot(s) from %2")
            .arg(opt.slotCount)
            .arg(maskAddress(opt.address)),
        hooks_.portalRun(c, opt, notify_)));
    watch(portalJob_);
  }

  // FDSN: every waiting day, oldest first.
  const QString statePath = fdsnStatePath(c), outDir = fdsnDefaultOut(c);
  if (!Ledger(statePath)
           .inState({QStringLiteral("pending"), QStringLiteral("claimed")})
           .isEmpty()) {
    const auto makeHttp = hooks_.http;
    const FdsnOptions options = hooks_.fdsn;
    fdsnJob_ = ctx_.jobs->add(new TaskJob(
        tr("waveforms from FDSN: %1").arg(studyName), QStringLiteral("tdvms"),
        tr("the waiting days, into the study's downloads folder"),
        [statePath, outDir, makeHttp, options](TaskContext &ctx) -> int {
          try {
            Ledger ledger(statePath);
            fdsnReclaim(ledger);
            const int adopted = fdsnAdopt(ledger, outDir);
            if (adopted > 0)
              ctx.log(CaseDownloads::tr("%n day(s) were on disk already",
                                        nullptr, adopted));
            auto http = makeHttp();
            const auto counts = fdsnWork(
                ledger, outDir, *http, options,
                [&](const QString &m) { ctx.log(m); },
                [&] { return ctx.cancelled(); },
                [&](int done, int total) {
                  if (total)
                    ctx.progress(double(done) / total,
                                 QStringLiteral("%1/%2").arg(done).arg(total));
                });
            QStringList bits;
            for (auto it = counts.begin(); it != counts.end(); ++it)
              bits << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
            ctx.log(CaseDownloads::tr("done: %1").arg(bits.join(", ")));
            return counts.value(QStringLiteral("failed")) > 0 ? 1 : 0;
          } catch (const std::exception &e) {
            ctx.log(QString::fromUtf8(e.what()));
            return 1;
          }
        }));
    watch(fdsnJob_);
  }
  finished_.clear();
  if (running())
    ticker_->start();
  emit runningChanged();
  refresh();
}

void CaseDownloads::go() {
  if (!blockedReason().isEmpty())
    return;
  try {
    plan();
  } catch (const std::exception &e) {
    finished_ = tr("The download could not be prepared: %1")
                    .arg(QString::fromUtf8(e.what()));
    refresh();
    return;
  }
  start();
}

void CaseDownloads::stop() {
  if (portalJob_ && portalJob_->state() == Job::State::Running)
    portalJob_->cancel();
  if (fdsnJob_ && fdsnJob_->state() == Job::State::Running)
    fdsnJob_->cancel();
}

void CaseDownloads::retry() {
  const Campaign c = download();
  if (c.ledgerPath.isEmpty() || running() || !holdsStudy())
    return;
  Ledger portal(c.ledgerPath);
  Ledger fdsn(fdsnStatePath(c));
  for (const Row &r : rows_) {
    retryFailed(portal, r.code);
    fdsnRetryFailed(fdsn, r.code);
  }
  emit downloadChanged(c);
  start();
}

void CaseDownloads::jobEnded() {
  if (running())
    return;
  ticker_->stop();
  compute();
  int failed = 0, open = 0;
  for (const Row &r : rows_) {
    failed += r.progress.failed;
    open += r.progress.open();
  }
  const bool cancelled =
      (portalJob_ && portalJob_->state() == Job::State::Cancelled) ||
      (fdsnJob_ && fdsnJob_->state() == Job::State::Cancelled);
  if (cancelled)
    finished_ = tr("Stopped. What was not fetched yet stays asked for: "
                   "press the button again to go on.");
  else if (open > 0)
    finished_ = tr("The download ended with %n request(s) still open; see "
                   "the Jobs panel for why, then press the button again.",
                   nullptr, open);
  else if (failed > 0)
    finished_ = tr("Finished, but %n request(s) failed. The table says "
                   "which and why; Retry failed asks for them again.",
                   nullptr, failed);
  else
    finished_ = tr("Finished. The study reads the new files now; the Cases "
                   "page shows them when it has.");
  // The Cases page scans the folders again when the study is saved.
  nudgeStudy();
  emit runningChanged();
  refresh();
}

// --- showing it -----------------------------------------------------------------

QString CaseDownloads::sourceHelp() const {
  return tr("Where this station's waveforms are asked for. The rule: a "
            "station that AFAD's portal lists with an instrument is asked "
            "for there, in requests of %1 days that AFAD answers by e-mail; "
            "any other station is asked for at an FDSN service (%2, "
            "channels %3; both can be changed in the FDSN form under All "
            "downloads), one file per day and no mailbox. Change it here if "
            "you know better, or leave a station out.")
      .arg(kChunkDays)
      .arg(fdsnService_, fdsnChannels_);
}

QString CaseDownloads::stateText(const Row &row) const {
  const RequestProgress &p = row.progress;
  const bool portal = row.source == Source::Portal;
  if (row.source == Source::None)
    return tr("left out");
  if (p.empty())
    return {};
  QStringList bits;
  if (p.open() > 0 || p.done() < p.total) {
    bits << (portal ? tr("%1 of %n request(s) answered", nullptr, p.total)
                          .arg(p.done())
                    : tr("%1 of %n day(s) done", nullptr, p.total)
                          .arg(p.done()));
    if (portal && !running() && !mailboxReady())
      bits << tr("waiting for the mailbox");
  } else {
    bits << (portal ? tr("%n request(s) fetched", nullptr, p.fetched)
                    : tr("%n day(s) fetched", nullptr, p.fetched));
  }
  if (p.noData > 0)
    bits << tr("%n without data at the source", nullptr, p.noData);
  if (p.failed > 0)
    bits << tr("%n failed (%1)", nullptr, p.failed)
                .arg(p.failures.isEmpty() ? tr("no reason given")
                                          : p.failures.first());
  return bits.join(QStringLiteral(" · "));
}

void CaseDownloads::fillTable() {
  const QSignalBlocker block(table_);
  table_->setRowCount(int(rows_.size()));
  for (int i = 0; i < int(rows_.size()); ++i) {
    const Row &r = rows_[i];
    auto *name = new QTableWidgetItem(
        r.network.isEmpty() ? r.code : r.network + QLatin1Char('.') + r.code);
    name->setData(Qt::UserRole, r.code);
    if (!r.known && r.source != Source::None)
      name->setToolTip(ui::helpText(
          tr("AFAD's portal does not list %1 with an instrument, so it is "
             "asked for at the FDSN service.")
              .arg(r.code)));
    table_->setItem(i, Station, name);

    QString have = QStringLiteral("—");
    QString haveTip;
    if (mode_ == Mode::Case) {
      const int of = r.spanDays;
      if (r.have == 0 && !r.atLeast && r.dataFirst.isValid()) {
        // Data, but none of it in the span: not the same as no data.
        have = tr("⚠ none in the span (it has %1 to %2)")
                   .arg(r.dataFirst.toString(Qt::ISODate),
                        r.dataLast.toString(Qt::ISODate));
      } else {
        have = r.have == 0 ? tr("none of %n day(s)", nullptr, of)
               : r.atLeast ? tr("at least %1 of %n day(s)", nullptr, of)
                                 .arg(r.have)
               : r.approximate
                     ? tr("about %1 of %n day(s)", nullptr, of).arg(r.have)
                     : tr("%1 of %n day(s)", nullptr, of).arg(r.have);
        if (r.thin) {
          have = tr("⚠ %1: too little to run on").arg(have);
          haveTip = tr("The station has data on only a small part of the "
                       "span; a run on it would rest on very little.");
        }
      }
      if (r.approximate || r.atLeast)
        haveTip += (haveTip.isEmpty() ? QString() : QStringLiteral(" ")) +
                   tr("Some of this station's files have only been looked at "
                      "quickly; the exact count follows when the study has "
                      "read them.");
    }
    auto *haveItem = new QTableWidgetItem(have);
    if (!haveTip.isEmpty())
      haveItem->setToolTip(ui::helpText(haveTip));
    table_->setItem(i, Have, haveItem);

    auto *from = qobject_cast<QComboBox *>(table_->cellWidget(i, From));
    if (!from) {
      from = new QComboBox(table_);
      from->addItem(QString(), int(Source::Portal));
      from->addItem(QString(), int(Source::Fdsn));
      from->addItem(QString(), int(Source::None));
      table_->setCellWidget(i, From, from);
      connect(from, &QComboBox::activated, this, [this, from](int index) {
        sourceChosen(from->property("code").toString(), index);
      });
    }
    {
      const QSignalBlocker quiet(from);
      from->setProperty("code", r.code);
      from->setObjectName(QStringLiteral("guideFrom_") + r.code);
      from->setItemText(0, tr("AFAD portal"));
      from->setItemText(1, tr("FDSN service"));
      from->setItemText(2, tr("Do not download"));
      from->setCurrentIndex(from->findData(int(r.source)));
      from->setEnabled(!running());
      ui::setHelp(from, sourceHelp());
    }

    QString toGet;
    if (r.source == Source::None)
      toGet = QStringLiteral("—");
    else if (mode_ != Mode::Case)
      toGet = r.source == Source::Portal
                  ? tr("%n request(s) open", nullptr, r.progress.open())
                  : tr("%n day(s) open", nullptr, r.progress.open());
    else if (r.missing.isEmpty())
      toGet = tr("nothing: all there");
    else if (r.windows.isEmpty() && r.fdsnDays.isEmpty() &&
             r.progress.failed == 0 && r.progress.open() == 0 &&
             r.progress.fetched > 0)
      // Downloaded; the count on the left follows when the study has read
      // the new files.
      toGet = tr("fetched; the study has not read the files yet");
    else if (r.windows.isEmpty() && r.fdsnDays.isEmpty() &&
             r.progress.failed > 0)
      toGet = tr("%n day(s) to get; what failed can be asked for again",
                 nullptr, int(r.missing.size()));
    else if (r.source == Source::Portal)
      toGet = r.windows.isEmpty()
                  ? tr("%n day(s) to get, asked for already", nullptr,
                       int(r.missing.size()))
                  : tr("%1 day(s) to get: %n new request(s)", nullptr,
                       int(r.windows.size()))
                        .arg(r.missing.size());
    else
      toGet = r.fdsnDays.isEmpty()
                  ? tr("%n day(s) to get, asked for already", nullptr,
                       int(r.missing.size()))
                  : tr("%1 day(s) to get: %n new day(s) to ask for", nullptr,
                       int(r.fdsnDays.size()))
                        .arg(r.missing.size());
    table_->setItem(i, ToGet, new QTableWidgetItem(toGet));

    auto *state = new QTableWidgetItem(stateText(r));
    if (!r.progress.failures.isEmpty())
      state->setToolTip(ui::helpText(
          tr("Failed: %1")
              .arg(r.progress.failures.mid(0, 12).join(QStringLiteral("; ")))));
    table_->setItem(i, State, state);
  }
  // As tall as its rows, up to a handful; more scroll.
  int height = table_->horizontalHeader()->sizeHint().height() + 6;
  for (int i = 0; i < table_->rowCount(); ++i)
    height += table_->rowHeight(i);
  table_->setFixedHeight(std::clamp(height, 70, 230));
  table_->resizeColumnToContents(Station);
  table_->resizeColumnToContents(Have);
  table_->resizeColumnToContents(ToGet);
  table_->setColumnWidth(From, 170);
}

void CaseDownloads::sourceChosen(const QString &code, int index) {
  auto *from = qobject_cast<QComboBox *>(sender());
  if (!from)
    for (int i = 0; i < table_->rowCount(); ++i)
      if (auto *box = qobject_cast<QComboBox *>(table_->cellWidget(i, From));
          box && box->property("code").toString() == code)
        from = box;
  if (!from || index < 0)
    return;
  chosen_.insert(code, Source(from->itemData(index).toInt()));
  refresh();
}

void CaseDownloads::updateTexts() {
  const bool guide = guiding();
  setVisible(mode_ != Mode::Off);
  body_->setVisible(guide);
  go_->setVisible(guide);
  stop_->setVisible(guide);
  dismiss_->setVisible(mode_ == Mode::Case);
  recipes_->setVisible(guide);
  if (!guide)
    retry_->setVisible(false);
  cases_->setEnabled(ctx_.nav != nullptr);
  recipes_->setEnabled(ctx_.nav != nullptr);

  if (mode_ == Mode::Idle) {
    title_->setText(tr("Waveforms for a case"));
    lead_->setText(tr("To download what a case needs, select the case on "
                      "the Cases page and press \"Next: get waveforms\". "
                      "This page then lists its stations, what the study "
                      "already has and what is missing."));
    cases_->setText(tr("Go to Cases"));
    return;
  }
  if (!guide)
    return;
  const QString period =
      spanDays() > 0 ? tr("%1 to %2")
                           .arg(first_.toString(Qt::ISODate),
                                end_.addDays(-1).toString(Qt::ISODate))
                     : QString();
  if (mode_ == Mode::Case) {
    title_->setText(case_.isEmpty()
                        ? tr("Get waveforms")
                        : tr("Get waveforms for %1").arg(case_));
    lead_->setText(tr("%n station(s), %1. The table says what the study's "
                      "waveform folders hold for these days and what is "
                      "missing. One button asks for all of it and files it "
                      "in the study.",
                      nullptr, int(rows_.size()))
                       .arg(period));
  } else {
    title_->setText(tr("An unfinished download of this study"));
    lead_->setText(tr("Requests made earlier for this study are still "
                      "open. Resume to go on where it stopped; nothing is "
                      "asked for twice."));
  }
  table_->setHorizontalHeaderLabels(
      {tr("Station"), tr("In the study"), tr("Get it from"),
       tr("Still to get"), tr("Download")});

  // What the button would do, and what it costs.
  int newRequests = 0, newDays = 0, openRequests = 0, openDays = 0, failed = 0;
  int portalStations = 0;
  for (const Row &r : rows_) {
    failed += r.progress.failed;
    if (r.source == Source::Portal) {
      ++portalStations;
      newRequests += int(r.windows.size());
      openRequests += r.progress.open();
    } else if (r.source == Source::Fdsn) {
      newDays += int(r.fdsnDays.size());
      openDays += r.progress.open();
    }
  }
  QStringList parts;
  if (newRequests + openRequests > 0)
    parts << tr("%n request(s) to AFAD (about %1)", nullptr,
                newRequests + openRequests)
                 .arg(sizeText(portalBytes_));
  if (newDays + openDays > 0)
    parts << tr("%n day(s) from the FDSN service (about %1)", nullptr,
                newDays + openDays)
                 .arg(sizeText(fdsnBytes_));
  QString summary = parts.isEmpty()
                        ? QString()
                        : tr("To download: %1.").arg(parts.join(tr("; ")));
  if (freeBytes_ >= 0 && !parts.isEmpty())
    summary += QLatin1Char(' ') +
               tr("Free on the study's disk: %1.").arg(sizeText(freeBytes_));
  if (!diskNote_.isEmpty())
    summary += QLatin1Char(' ') + diskNote_;
  // The folder is named by what it is, not by its path: this page ends up
  // in screenshots, and the path is in the tooltip.
  if (!parts.isEmpty())
    summary += QLatin1Char(' ') +
               tr("The files go into the study's folder, under \"%1\".")
                   .arg(QLatin1String(kStudyDownloadsDir));
  summary_->setText(summary);
  summary_->setVisible(!summary.isEmpty());
  summary_->setToolTip(ui::helpText(
      tr("Sizes are rough: what this download has fetched per day so far, "
         "or, before anything was fetched, %1 per station and day from "
         "AFAD and %2 from an FDSN service. The folder is %3.")
          .arg(sizeText(kPortalBytesPerDay), sizeText(kFdsnBytesPerDay),
               QDir::toNativeSeparators(studyDownloadsRoot(studyDir())))));

  // The mailbox: only AFAD needs it.
  const bool needsMail = portalStations > 0;
  const bool ready = mailboxReady();
  mailNote_->setVisible(needsMail);
  mailButton_->setVisible(needsMail);
  if (needsMail) {
    mailNote_->setText(
        ready ? tr("Mailbox for AFAD's links: %1.")
                    .arg(maskAddress(hooks_.address()))
              : tr("AFAD's portal does not hand out files directly: it "
                   "e-mails a link for each request. The application reads "
                   "those e-mails from a mailbox you set up once. Until "
                   "then the %n station(s) asked for at AFAD wait; stations "
                   "from the FDSN service are fetched right away.",
                   nullptr, portalStations));
    mailButton_->setText(ready ? tr("Change the mailbox…")
                               : tr("Set up the mailbox…"));
    ui::setPrimary(mailButton_, !ready && newDays + openDays == 0);
  }

  const QString why = blockedReason();
  const bool isRunning = running();
  go_->setText(newRequests + newDays == 0 && openRequests + openDays > 0
                   ? tr("Resume the download")
                   : tr("Download the missing data"));
  go_->setEnabled(why.isEmpty() && !isRunning);
  ui::setPrimary(go_, go_->isEnabled());
  stop_->setText(tr("Stop"));
  stop_->setEnabled(isRunning);
  retry_->setText(tr("Retry failed"));
  retry_->setVisible(failed > 0);
  retry_->setEnabled(!isRunning && holdsStudy());
  why_->setText(isRunning ? tr("Downloading. The table follows the "
                               "requests; the full log is in the Jobs "
                               "panel.")
                          : why);
  done_->setText(finished_);
  done_->setVisible(!finished_.isEmpty() && !isRunning);
  // How the download ended says it already; no second line under it.
  why_->setVisible(!why_->text().isEmpty() && !done_->isVisibleTo(this));
  cases_->setText(tr("Back to Cases"));
  recipes_->setText(tr("Next: recipes →"));
  dismiss_->setText(tr("Dismiss"));

  go_->setToolTip(ui::helpText(
      tr("Writes the requests for every station in the table, files what "
         "arrives under the study's downloads folder, adds that folder to "
         "the study's waveform folders and starts the transfers as jobs.")));
  stop_->setToolTip(ui::helpText(
      tr("Stops the transfers. What was not fetched stays asked for and is "
         "taken up when you press the button again.")));
  retry_->setToolTip(ui::helpText(
      tr("Puts the failed requests of these stations back in line and "
         "starts the download again.")));
  mailButton_->setToolTip(ui::helpText(
      tr("The login of the mailbox AFAD's e-mails arrive in (IMAP server, "
         "user, password). It is kept on this computer only.")));
  cases_->setToolTip(ui::helpText(
      tr("Opens the Cases page on this case, where the Waveforms column "
         "shows what the study has once the new files are read.")));
  recipes_->setToolTip(ui::helpText(
      tr("Opens the Runs page, the next step: choosing what to compute on "
         "the cases.")));
  dismiss_->setToolTip(ui::helpText(
      tr("Closes this panel. Requests already made stay; an unfinished "
         "download is offered again.")));
}

void CaseDownloads::rebuild() {
  fillTable();
  updateTexts();
}

void CaseDownloads::refresh() {
  compute();
  rebuild();
}

void CaseDownloads::retranslate() { updateTexts(); }

void CaseDownloads::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    fillTable();
    retranslate();
  }
  QFrame::changeEvent(e);
}

} // namespace skg
