#include "tdvms/runner.h"

#include <QDateTime>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include "tdvms/portal.h"
#include "tdvms/slots.h"

namespace skg::tdvms {

// --- Watch ------------------------------------------------------------------

Watch::Watch(Notify notify, QString campaignName, Ledger &ledger, Pool &pool,
             double stallHours, int mailFailures)
    : notify_(std::move(notify)), name_(std::move(campaignName)),
      ledger_(ledger), pool_(pool), stallHours_(stallHours),
      mailFailures_(mailFailures), done_(ledger.complete()) {}

void Watch::raise(const QString &key, const QString &title,
                  const QString &body) {
  if (active_.contains(key))
    return;
  active_.insert(key);
  if (notify_)
    notify_(name_ + ": " + title, body, true);
}

void Watch::clear(const QString &key, const QString &title,
                  const QString &body) {
  if (!active_.contains(key))
    return;
  active_.remove(key);
  if (!title.isEmpty() && notify_)
    notify_(name_ + ": " + title, body, false);
}

QString Watch::stalled(const QList<Row> &rows) const {
  QList<double> waiting, answered;
  for (const Row &r : rows) {
    const QString s = str(r, "state");
    if (s == QLatin1String("submitted")) {
      if (const auto a = ageSeconds(str(r, "submitted_at")))
        waiting << *a;
    } else if (s == QLatin1String("fetched") || s == QLatin1String("nodata")) {
      if (const auto a = ageSeconds(str(r, "fetched_at")))
        answered << *a;
    }
  }
  const double limit = stallHours_ * 3600;
  if (waiting.isEmpty())
    return {};
  const double oldest = *std::max_element(waiting.begin(), waiting.end());
  if (oldest < limit)
    return {};
  if (!answered.isEmpty() &&
      *std::min_element(answered.begin(), answered.end()) < limit)
    return {};
  return QStringLiteral("%1 request(s) waiting, the oldest for %2 h. Check the "
                        "mailbox and the portal.")
      .arg(waiting.size())
      .arg(oldest / 3600, 0, 'f', 1);
}

void Watch::after(const CycleResult &result) {
  if (!notify_)
    return;
  mailStreak_ = result.mailError.isEmpty() ? 0 : mailStreak_ + 1;
  if (mailStreak_ >= mailFailures_)
    raise(
        QStringLiteral("mail"), QStringLiteral("can't read the mailbox"),
        QStringLiteral("%1 cycles in a row: %2. Nothing is downloaded until it "
                       "can be read -- check the credentials.")
            .arg(mailStreak_)
            .arg(result.mailError));
  else if (result.mailError.isEmpty())
    clear(QStringLiteral("mail"), QStringLiteral("mailbox readable again"),
          QStringLiteral("Downloads have resumed."));

  if (!result.paused.isEmpty())
    raise(QStringLiteral("disk"), QStringLiteral("disk nearly full"),
          result.paused);
  else
    clear(QStringLiteral("disk"), QStringLiteral("disk space recovered"),
          QStringLiteral("Downloading and submitting again."));

  bool allBusy = pool_.size() > 0;
  for (const Slot &s : pool_.entries())
    allBusy = allBusy && s.remoteBusy();
  if (allBusy)
    raise(QStringLiteral("busy"), QStringLiteral("every slot is cooling down"),
          QStringLiteral(
              "The portal answered BUSY to all of them. They retry on "
              "their own; if this keeps happening, another tool may be "
              "using the same addresses."));
  else
    clear(QStringLiteral("busy"));

  const QList<Row> rows = ledger_.rows();
  const QString stall = stalled(rows);
  if (!stall.isEmpty())
    raise(QStringLiteral("stall"),
          QStringLiteral("nothing back for %1 h").arg(stallHours_), stall);
  else
    clear(QStringLiteral("stall"),
          QStringLiteral("results are arriving again"));

  if (!result.retired.isEmpty()) {
    QSet<QString> stations;
    for (const QString &cid : result.retired)
      stations.insert(cid.section(':', 0, 0));
    QStringList names = stations.values();
    names.sort();
    notify_(QStringLiteral("%1: stopped requesting %2")
                .arg(name_, names.join(", ")),
            QStringLiteral("%1 pending window(s) retired; see the log for why.")
                .arg(result.retired.size()),
            false);
  }
  if (!result.rejected.isEmpty())
    notify_(QStringLiteral("%1: gave up on %2 window(s)")
                .arg(name_)
                .arg(result.rejected.size()),
            result.rejected.mid(0, 5).join(", "), false);

  if (!done_ && !rows.isEmpty()) {
    bool open = false;
    for (const Row &r : rows)
      open = open || !kTerminal.contains(str(r, "state"));
    if (!open) {
      done_ = true;
      QMap<QString, int> by;
      double gb = 0;
      for (const Row &r : rows) {
        by[str(r, "state")] += 1;
        if (str(r, "state") == QLatin1String("fetched"))
          gb += bytesOf(r) / 1e9;
      }
      notify_(name_ + QStringLiteral(": complete"),
              QStringLiteral(
                  "%1 fetched (%2 GB), %3 no data, %4 failed, %5 retired.")
                  .arg(by.value("fetched"))
                  .arg(gb, 0, 'f', 1)
                  .arg(by.value("nodata"))
                  .arg(by.value("failed"))
                  .arg(by.value("retired")),
              false);
    }
  }
}

// --- notifier --------------------------------------------------------------

Notify makeNotifier(const QStringList &targets, Http *http,
                    const Notify &desktop, const Log &log) {
  if (targets.isEmpty())
    return {};
  return [=](const QString &title, const QString &body, bool urgent) {
    for (const QString &t : targets) {
      if (t == QLatin1String("desktop")) {
        if (desktop)
          desktop(title, body, urgent);
        continue;
      }
      if (!http)
        continue;
      const HttpResult r = http->postBody(
          QUrl(t), body.toUtf8(), 10000,
          {{QStringLiteral("Title"), title},
           {QStringLiteral("Priority"),
            urgent ? QStringLiteral("high") : QStringLiteral("default")},
           {QStringLiteral("Tags"), urgent ? QStringLiteral("warning")
                                           : QStringLiteral("seismograph")}});
      if (!r.ok() && log)
        log(QStringLiteral("  [NOTIFY] %1: %2")
                .arg(t, r.error.isEmpty()
                            ? QStringLiteral("HTTP %1").arg(r.status)
                            : r.error));
    }
  };
}

// --- the loop --------------------------------------------------------------

namespace {

QString stamp() {
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

// Sleeps `seconds` in small steps so a cancel is noticed quickly.
void pause(TaskContext &ctx, int seconds) {
  const qint64 until =
      QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000;
  while (QDateTime::currentMSecsSinceEpoch() < until && !ctx.cancelled())
    QThread::msleep(250);
}

struct RunPieces {
  std::unique_ptr<Http> http;
  std::unique_ptr<TdvmsPortal> portal;
  std::unique_ptr<ImapMailbox> mailbox;
  Notify notify;
};

RunPieces makePieces(const RunOptions &opt, const Notify &desktop,
                     const Log &log) {
  RunPieces p;
  p.http = makeQtHttp();
  p.portal = std::make_unique<TdvmsPortal>(p.http.get(), opt.portalTimeoutMs);
  if (opt.mailbox.complete())
    p.mailbox = std::make_unique<ImapMailbox>(
        opt.mailbox.host, opt.mailbox.user, opt.mailbox.password, opt.folder,
        opt.search);
  p.notify = makeNotifier(opt.notifyTargets, p.http.get(), desktop, log);
  return p;
}

// Cycles one campaign until stopped (or complete, with untilDone).
void loop(TaskContext &ctx, Ledger &ledger, Pool &pool,
          const SupervisorConfig &cfg, Mailbox *mailbox, Portal *portal,
          Http *http, int interval, bool once, bool untilDone, Watch *watch) {
  int quiet = 0;
  QString paused;
  while (!ctx.cancelled()) {
    const QString s = stamp();
    auto log = [&](const QString &m) {
      ctx.log(QStringLiteral("[%1] %2").arg(s, m));
    };
    const CycleResult result =
        runCycle(ledger, pool, cfg, mailbox, portal, http, log,
                 [&] { return ctx.cancelled(); });
    if (result.paused != paused) {
      log(QStringLiteral("[DISK] ") +
          (result.paused.isEmpty() ? QStringLiteral("room again -- resuming")
                                   : result.paused));
      paused = result.paused;
    }
    if (result.quiet()) {
      ++quiet;
      log(QStringLiteral("quiet (%1 in a row), %2/%3 slots free")
              .arg(quiet)
              .arg(pool.free().size())
              .arg(pool.size()));
    } else {
      quiet = 0;
      log(result.summary());
    }
    int terminal = 0;
    const QList<Row> rows = ledger.rows();
    for (const Row &r : rows)
      terminal += kTerminal.contains(str(r, "state"));
    ctx.progress(rows.isEmpty() ? 0 : double(terminal) / rows.size(),
                 QStringLiteral("%1/%2 done, %3/%4 slots free")
                     .arg(terminal)
                     .arg(rows.size())
                     .arg(pool.free().size())
                     .arg(pool.size()));
    if (watch)
      watch->after(result);
    if (once)
      return;
    if (untilDone && ledger.complete()) {
      log(QStringLiteral("every chunk has an outcome -- done"));
      return;
    }
    pause(ctx, interval);
  }
}

QJsonObject describe(const RunOptions &opt, const QString &outDir,
                     const QString &command) {
  return {{"slots", opt.slotCount}, {"out_dir", outDir}, {"command", command}};
}

} // namespace

TaskJob::Work campaignWork(const Campaign &campaign, const RunOptions &opt,
                           const Notify &desktop) {
  return [campaign, opt, desktop](TaskContext &ctx) -> int {
    Ledger ledger(campaign.ledgerPath);
    if (ledger.rows().isEmpty()) {
      ctx.log(QStringLiteral("[ERROR] %1 is empty. Plan a station first.")
                  .arg(ledger.path()));
      return 2;
    }
    const QString outDir =
        opt.supervisor.outDir.isEmpty() ? campaign.raw : opt.supervisor.outDir;
    Pool pool(opt.address, opt.slotCount);
    std::unique_ptr<PoolLock> lock;
    if (!opt.supervisor.dryRun) {
      try {
        lock = std::make_unique<PoolLock>(
            opt.address, ledger.path(),
            describe(opt, outDir, QStringLiteral("run")));
      } catch (const PoolHeld &e) {
        ctx.log(
            QStringLiteral(
                "[ERROR] %1 is already being worked (%2). Two runs on one "
                "address share its slots and its inbox. Stop that run first.")
                .arg(opt.address, e.what()));
        return 3;
      }
    }
    SupervisorConfig cfg = opt.supervisor;
    cfg.outDir = outDir;
    auto log = [&ctx](const QString &m) { ctx.log(m); };
    RunPieces pieces = makePieces(opt, desktop, log);
    std::unique_ptr<Watch> watch;
    if (!cfg.dryRun && pieces.notify)
      watch =
          std::make_unique<Watch>(pieces.notify, campaign.name, ledger, pool);
    if (cfg.dryRun)
      ctx.log(QStringLiteral(
          "dry run -- no mail flagged, no ledger writes, no submissions"));
    ctx.log(QStringLiteral("%1 slot(s) from %2; ledger %3; archives to %4%5")
                .arg(pool.size())
                .arg(opt.address, ledger.path(), outDir,
                     opt.notifyTargets.isEmpty()
                         ? QString()
                         : QStringLiteral("; notifying %1")
                               .arg(opt.notifyTargets.join(", "))));
    if (!pieces.mailbox)
      ctx.log(QStringLiteral(
          "no mailbox credentials: links will not be read until they are set"));
    loop(ctx, ledger, pool, cfg, pieces.mailbox.get(), pieces.portal.get(),
         pieces.http.get(), opt.intervalSeconds, opt.once, opt.exitWhenDone,
         watch.get());
    if (ctx.cancelled())
      ctx.log(QStringLiteral(
          "stopped. Nothing is lost -- the ledger is the state, and every "
          "in-flight chunk is reclaimed by the next run."));
    return 0;
  };
}

TaskJob::Work queueWork(const QList<Campaign> &campaigns, const RunOptions &opt,
                        const Notify &desktop) {
  return [campaigns, opt, desktop](TaskContext &ctx) -> int {
    if (campaigns.isEmpty())
      return 2;
    Pool pool(opt.address, opt.slotCount);
    std::unique_ptr<PoolLock> lock;
    try {
      lock = std::make_unique<PoolLock>(
          opt.address, campaigns.first().ledgerPath,
          describe(opt, campaigns.first().raw, QStringLiteral("queue")));
    } catch (const PoolHeld &e) {
      ctx.log(QStringLiteral("[ERROR] %1 is already being worked (%2).")
                  .arg(opt.address, e.what()));
      return 3;
    }
    auto log = [&ctx](const QString &m) { ctx.log(m); };
    RunPieces pieces = makePieces(opt, desktop, log);
    QStringList names;
    for (const Campaign &c : campaigns)
      names << c.name;
    ctx.log(QStringLiteral("queue: %1; %2 slot(s) from %3")
                .arg(names.join(" -> "))
                .arg(pool.size())
                .arg(opt.address));
    for (const Campaign &c : campaigns) {
      if (ctx.cancelled())
        break;
      Ledger ledger(c.ledgerPath);
      const QList<Row> rows = ledger.rows();
      if (rows.isEmpty()) {
        ctx.log(QStringLiteral("== %1: %2 is empty, skipping")
                    .arg(c.name, c.ledgerPath));
        continue;
      }
      if (ledger.complete()) {
        ctx.log(
            QStringLiteral("== %1: already complete, skipping").arg(c.name));
        continue;
      }
      lock->relabel(c.ledgerPath,
                    describe(opt, c.raw, QStringLiteral("queue")));
      int pending = 0, inflight = 0;
      for (const Row &r : rows) {
        pending += str(r, "state") == QLatin1String("pending");
        inflight += kInFlight.contains(str(r, "state"));
      }
      ctx.log(QStringLiteral("== %1: %2 pending, %3 in flight; archives to %4")
                  .arg(c.name)
                  .arg(pending)
                  .arg(inflight)
                  .arg(c.raw));
      SupervisorConfig cfg = opt.supervisor;
      cfg.outDir = c.raw;
      std::unique_ptr<Watch> watch;
      if (pieces.notify)
        watch = std::make_unique<Watch>(pieces.notify, c.name, ledger, pool);
      loop(ctx, ledger, pool, cfg, pieces.mailbox.get(), pieces.portal.get(),
           pieces.http.get(), opt.intervalSeconds, false, true, watch.get());
    }
    if (!ctx.cancelled()) {
      ctx.log(QStringLiteral("queue finished"));
      if (pieces.notify)
        pieces.notify(QStringLiteral("tdvms: queue finished"),
                      QStringLiteral("%1: every chunk has an outcome.")
                          .arg(names.join(", ")),
                      false);
    }
    return 0;
  };
}

} // namespace skg::tdvms
