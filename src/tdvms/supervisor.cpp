#include "tdvms/supervisor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <stdexcept>

#include "tdvms/disk.h"
#include "tdvms/imap.h"
#include <algorithm>
#include <utility>

namespace skg::tdvms {

// --- the real mailbox ------------------------------------------------------

ImapMailbox::ImapMailbox(QString host, QString user, QString password,
                         QString folder, QString search)
    : host_(std::move(host)), user_(std::move(user)),
      password_(std::move(password)), folder_(std::move(folder)),
      search_(std::move(search)) {}

ImapMailbox::~ImapMailbox() { close(); }

QList<MailEvent> ImapMailbox::read(const QSet<QString> &addresses,
                                   bool claimUnknown) {
  close();
  client_ = std::make_unique<ImapClient>();
  try {
    client_->connectToHost(host_);
    client_->login(user_, password_);
    client_->select(folder_);
    QList<MailEvent> events;
    for (const QString &uid : client_->uidSearch(search_)) {
      try {
        events << classifyMessage(uid,
                                  parseMessage(client_->uidFetchMessage(uid)),
                                  addresses, claimUnknown);
      } catch (const ImapError &) {
        MailEvent e;
        e.kind = MailEvent::Kind::Unreadable;
        e.uid = uid;
        events << e;
      }
    }
    return events;
  } catch (const ImapError &e) {
    throw std::runtime_error(e.what());
  }
}

void ImapMailbox::consume(const QString &uid, bool ok) {
  if (!client_)
    throw std::runtime_error("mailbox not open");
  try {
    client_->uidStore(uid, ok ? QStringLiteral("(\\Seen)")
                              : QStringLiteral("(\\Seen \\Flagged)"));
  } catch (const ImapError &e) {
    throw std::runtime_error(e.what());
  }
}

void ImapMailbox::close() {
  if (client_) {
    try {
      client_->logout();
    } catch (...) {
    }
    client_.reset();
  }
}

// --- the cycle -------------------------------------------------------------

bool CycleResult::quiet() const {
  return fetched.isEmpty() && nodata.isEmpty() && requeued.isEmpty() &&
         reclaimed.isEmpty() && retired.isEmpty() && submitted.isEmpty() &&
         busy.isEmpty() && rejected.isEmpty();
}

QString CycleResult::summary() const {
  QStringList bits;
  const QList<QPair<QString, const QStringList *>> parts{
      {"fetched", &fetched},   {"nodata", &nodata},
      {"requeued", &requeued}, {"reclaimed", &reclaimed},
      {"retired", &retired},   {"submitted", &submitted},
      {"busy", &busy},         {"rejected", &rejected}};
  for (const auto &[name, list] : parts)
    if (!list->isEmpty())
      bits << QStringLiteral("%1 %2").arg(name).arg(list->size());
  return bits.isEmpty() ? QStringLiteral("nothing to do") : bits.join(", ");
}

namespace {

struct Ctx {
  Ledger &ledger;
  Pool &pool;
  const SupervisorConfig &cfg;
  CycleResult &out;
  const Log &log;
  const std::function<bool()> &shouldStop;
  void say(const QString &m) const {
    if (log)
      log(m);
  }
  bool stopping() const { return shouldStop && shouldStop(); }
};

QDateTime naiveUtc(const QString &iso) {
  QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
  t.setTimeZone(QTimeZone::utc());
  return t;
}

void freeSlot(Pool &pool, const QString &email) {
  if (Slot *s = pool.get(email))
    s->holding.reset();
}

std::optional<Row> rowHeldBy(Ctx &c, const QString &email) {
  if (Slot *s = c.pool.get(email); s && s->holding)
    return s->holding;
  return c.ledger.heldBy(email);
}

void requeueSlot(Ctx &c, const QString &email, const QString &note) {
  const auto row = rowHeldBy(c, email);
  if (!row)
    return;
  const QString cid = chunkId(*row);
  const int attempts = intOf(*row, "attempts") + 1;
  c.ledger.release(cid, note);
  c.ledger.update(cid, {{"attempts", attempts}});
  freeSlot(c.pool, email);
  c.out.requeued << cid;
}

// --- 1. reap ---------------------------------------------------------------

bool handleNoData(Ctx &c, const MailEvent &ev) {
  const auto row = rowHeldBy(c, ev.to);
  if (!row) {
    c.say(QStringLiteral(
              "  no-data mail to %1, which holds nothing -- nothing recorded")
              .arg(ev.to));
    return true;
  }
  const QString cid = chunkId(*row);
  c.ledger.update(cid,
                  {{"state", "nodata"},
                   {"fetched_at", nowStamp()},
                   {"note", "portal: veri bulunmamaktadir (mail, no link)"}});
  freeSlot(c.pool, ev.to);
  c.out.nodata << cid;
  c.say(QStringLiteral("  [NODATA] %1 -- no waveform at source; slot %2 free")
            .arg(cid, ev.to));
  return true;
}

enum class IngestKind {
  Fetched,
  NoData,
  Requeued,
  Unmatched,
  Failed,
  DiskFull
};

IngestKind ingest(Ctx &c, Http &http, const QString &url, const QString &to) {
  const QString shortName = url.section('/', -1);
  const DownloadOutcome dl =
      download(http, url, c.cfg.outDir, c.cfg.downloadTimeoutMs, c.shouldStop);
  if (dl.diskFull) {
    c.say(QStringLiteral(
              "  [DISK] ran out of room downloading %1; the partial file is "
              "gone and the mail stays unread")
              .arg(shortName));
    return IngestKind::DiskFull;
  }
  if (dl.stopped)
    return IngestKind::Failed;
  if (dl.expired) {
    c.say(QStringLiteral("  [EXPIRED] %1").arg(dl.error));
    requeueSlot(c, to, QStringLiteral("link expired"));
    return IngestKind::Requeued;
  }
  if (!dl.ok()) {
    c.say(QStringLiteral("  [FAIL] %1").arg(dl.error));
    return IngestKind::Failed;
  }

  const Inspection insp = inspectArchive(dl.path);
  if (insp.problem != Inspection::Problem::None) {
    QFile::remove(dl.path);
    if (insp.problem == Inspection::Problem::EmptyArchive) {
      // Transient: the portal failed to build the archive; the window still
      // holds data. Requeue rather than retire.
      c.say(QStringLiteral("  [EMPTY] %1 -- requeuing the window")
                .arg(insp.detail));
      requeueSlot(c, to, QStringLiteral("empty archive, requeued"));
    } else {
      c.say(QStringLiteral("  [BAD] %1").arg(insp.detail));
      requeueSlot(c, to, insp.detail);
    }
    return IngestKind::Requeued;
  }

  const MemberWindow w = windowFromMember(insp.members.first());
  std::optional<Row> row;
  if (w.valid) {
    const QString want = w.start.toString(QStringLiteral("yyyy-MM-dd"));
    for (const Row &r : c.ledger.rows())
      if (str(r, "station") == w.station && str(r, "start").left(10) == want) {
        row = r;
        break;
      }
  }

  if (!insp.hasWaveform()) {
    const QString notice =
        QString::fromUtf8(readMember(dl.path, insp.members.first(), 200))
            .trimmed();
    QFile::remove(dl.path);
    if (!row)
      row = rowHeldBy(c, to); // the address names the window
    if (!row) {
      c.say(QStringLiteral(
          "  [NODATA] notice for a window this ledger does not hold"));
      return IngestKind::NoData;
    }
    const QString cid = chunkId(*row);
    c.ledger.update(cid, {{"state", "nodata"},
                          {"fetched_at", nowStamp()},
                          {"url", url},
                          {"bytes", double(insp.size)},
                          {"note", notice}});
    freeSlot(c.pool, to);
    c.out.nodata << cid;
    c.say(QStringLiteral("  [NODATA] %1 -- %2").arg(cid, notice.left(70)));
    return IngestKind::NoData;
  }

  if (!row) {
    const QString stray = QFileInfo(dl.path).dir().filePath(
        QStringLiteral("UNMATCHED_%1.zip")
            .arg(insp.members.first().section('/', -1)));
    QFile::remove(stray);
    QFile::rename(dl.path, stray);
    c.say(QStringLiteral("  [UNMATCHED] '%1' matches no chunk -- kept as %2")
              .arg(insp.members.first(), QFileInfo(stray).fileName()));
    return IngestKind::Unmatched;
  }

  const FiledArchive filed =
      fileArchive(dl.path, *row, c.cfg.outDir, insp.members.size(), insp.size);
  const QString cid = chunkId(*row);
  if (filed.kind == FiledArchive::Kind::Fetched) {
    c.ledger.update(
        cid, {{"state", "fetched"},
              {"fetched_at", nowStamp()},
              {"url", url},
              {"bytes", double(insp.size)},
              {"note", QStringLiteral("%1 file(s)").arg(insp.members.size())}});
    freeSlot(c.pool, to);
    c.out.fetched << cid;
    c.say(QStringLiteral("  [OK] %1 -- %2 -> %3")
              .arg(cid, filed.detail, filed.path));
    return IngestKind::Fetched;
  }
  c.say(QStringLiteral("  [WARN] %1").arg(filed.detail));
  return IngestKind::Failed;
}

// true/false: whether every link went cleanly; nullopt: the disk ran short
// (or we are stopping), so the message stays unread.
std::optional<bool> handleLinks(Ctx &c, Http *http, const MailEvent &ev) {
  bool clean = true;
  for (const QString &url : ev.urls) {
    bool banked = false;
    for (const Row &r : c.ledger.rows())
      if (str(r, "url") == url && str(r, "state") == QLatin1String("fetched")) {
        banked = true;
        break;
      }
    if (banked) {
      c.say(QStringLiteral("  [SKIP] already banked: %1")
                .arg(url.section('/', -1)));
      continue;
    }
    if (c.cfg.dryRun) {
      c.say(QStringLiteral("  DRY would fetch %1 for %2")
                .arg(url.section('/', -1), ev.to));
      continue;
    }
    if (c.stopping())
      return std::nullopt;
    c.out.paused = diskCheck(c.cfg.outDir, c.ledger.rows(), c.cfg.minFreeGb);
    if (!c.out.paused.isEmpty())
      return std::nullopt;
    if (!http) {
      clean = false;
      continue;
    }
    const IngestKind k = ingest(c, *http, url, ev.to);
    if (k == IngestKind::DiskFull) {
      c.out.paused =
          QStringLiteral("the disk under %1 filled during a download -- "
                         "paused until there is room")
              .arg(c.cfg.outDir);
      return std::nullopt;
    }
    if (k == IngestKind::Failed || k == IngestKind::Unmatched ||
        k == IngestKind::Requeued)
      clean = false;
  }
  return clean;
}

void reap(Ctx &c, Mailbox &mailbox, Http *http) {
  QList<MailEvent> events;
  try {
    const QStringList addresses = c.pool.addresses();
    events = mailbox.read(QSet<QString>(addresses.begin(), addresses.end()),
                          c.cfg.claimUnknown);
  } catch (const std::exception &e) {
    // A dropped IMAP connection is routine on a loop that runs for days;
    // failing the cycle over it would stop submissions too.
    c.out.notes
        << QStringLiteral("mailbox unreadable this pass (%1)").arg(e.what());
    c.out.mailError = QString::fromUtf8(e.what());
    c.say(QStringLiteral("  mailbox: %1 -- retrying next cycle").arg(e.what()));
    mailbox.close();
    return;
  }
  try {
    for (const MailEvent &ev : events) {
      if (c.stopping())
        break;
      if (ev.kind == MailEvent::Kind::Foreign) {
        c.out.foreign << ev.to;
        continue; // left unread, on purpose
      }
      if (ev.kind == MailEvent::Kind::Unreadable)
        continue;
      if (ev.kind == MailEvent::Kind::Link && !c.out.paused.isEmpty())
        continue; // no room to download it; left unread until there is
      std::optional<bool> ok = true;
      if (ev.kind == MailEvent::Kind::NoData)
        ok = handleNoData(c, ev);
      else if (ev.kind == MailEvent::Kind::Link)
        ok = handleLinks(c, http, ev);
      if (c.cfg.dryRun || !ok)
        continue;
      try {
        mailbox.consume(ev.uid, *ok);
      } catch (const std::exception &e) {
        // The connection died during a long download. The ledger write
        // already happened; the message is re-read next cycle, where the
        // already-banked check retires it without downloading again.
        c.out.notes << QStringLiteral("mail connection dropped mid-pass (%1)")
                           .arg(e.what());
        c.say(QStringLiteral(
                  "  mail connection dropped after the download (%1) -- the "
                  "ledger is already written; the message stays unread and is "
                  "skipped next cycle")
                  .arg(e.what()));
        break;
      }
    }
  } catch (const std::exception &e) {
    c.out.notes << QStringLiteral("reap failed (%1)").arg(e.what());
    c.say(
        QStringLiteral("  reap: %1 -- carrying on to the queue").arg(e.what()));
  }
  mailbox.close();
}

// --- 2. reclaim ------------------------------------------------------------

void reclaim(Ctx &c) {
  for (const Row &row : c.ledger.inState(kInFlight)) {
    const QString cid = chunkId(row);
    const bool claimed = str(row, "state") == QLatin1String("claimed");
    const char *stampKey = claimed ? "claimed_at" : "submitted_at";
    const int limit = claimed ? c.cfg.claimTimeout : c.cfg.submitTimeout;
    const QString why = claimed ? QStringLiteral("died mid-POST")
                                : QStringLiteral("link never arrived");
    const auto age = ageSeconds(str(row, stampKey));
    if (!age) {
      // No stamp: stamp it now so the NEXT cycle can time it out honestly.
      c.ledger.update(cid, {{QLatin1String(stampKey), nowStamp()}});
      c.say(QStringLiteral("  [STAMP] %1 had no %2; stamped now")
                .arg(cid, stampKey));
      continue;
    }
    if (*age < limit)
      continue;
    const int attempts = intOf(row, "attempts") + 1;
    if (attempts > c.cfg.maxAttempts) {
      c.ledger.update(
          cid, {{"state", "failed"},
                {"email", QJsonValue::Null},
                {"attempts", attempts},
                {"note", QStringLiteral("%1; gave up after %2 attempt(s)")
                             .arg(why)
                             .arg(attempts - 1)}});
      c.out.rejected << cid;
      c.say(QStringLiteral("  [GIVE UP] %1 -- %2, %3 attempts")
                .arg(cid, why)
                .arg(attempts - 1));
      continue;
    }
    c.ledger.release(cid, QStringLiteral("reclaimed after %1 min (%2)")
                              .arg(*age / 60, 0, 'f', 0)
                              .arg(why));
    c.ledger.update(cid, {{"attempts", attempts}});
    c.out.reclaimed << cid;
    c.say(QStringLiteral("  [RECLAIM] %1 -- %2 after %3 min; attempt %4")
              .arg(cid, why)
              .arg(*age / 60, 0, 'f', 0)
              .arg(attempts));
  }
}

// --- 3. retire -------------------------------------------------------------

void retireStation(Ctx &c, const QString &station, const QString &why) {
  QList<Row> doomed;
  for (const Row &r : c.ledger.rows())
    if (str(r, "station") == station &&
        str(r, "state") == QLatin1String("pending"))
      doomed << r;
  if (doomed.isEmpty())
    return;
  for (const Row &r : doomed) {
    c.ledger.update(
        chunkId(r),
        {{"state", "retired"},
         {"email", QJsonValue::Null},
         {"note",
          QStringLiteral("station retired after a permanent rejection: %1")
              .arg(why)}});
    c.out.retired << chunkId(r);
  }
  c.say(QStringLiteral("  [RETIRE] %1: %2\n           %3 pending chunk(s) "
                       "retired -- the next "
                       "window would fail the same way")
            .arg(station, why)
            .arg(doomed.size()));
}

void retire(Ctx &c) {
  for (const QString &station : c.ledger.stations()) {
    QList<Row> rows;
    for (const Row &r : c.ledger.rows())
      if (str(r, "station") == station)
        rows << r;
    bool anyFetched = false;
    int nodata = 0;
    QList<Row> dead;
    for (const Row &r : rows) {
      const QString s = str(r, "state");
      anyFetched = anyFetched || s == QLatin1String("fetched");
      nodata += s == QLatin1String("nodata");
      if (s == QLatin1String("pending"))
        dead << r;
    }
    if (anyFetched || nodata < c.cfg.nodataStreak || dead.isEmpty())
      continue;
    for (const Row &r : dead) {
      c.ledger.update(
          chunkId(r),
          {{"state", "retired"},
           {"email", QJsonValue::Null},
           {"note",
            QStringLiteral("station retired: %1 no-data answers, 0 fetched")
                .arg(nodata)}});
      c.out.retired << chunkId(r);
    }
    c.say(QStringLiteral(
              "  [RETIRE] %1: %2 no-data, 0 fetched -- %3 pending chunk(s) "
              "retired, slots released")
              .arg(station)
              .arg(nodata)
              .arg(dead.size()));
  }
}

// --- 4. submit -------------------------------------------------------------

std::optional<QStringList> nextStations(Ctx &c) {
  if (!c.cfg.stations.isEmpty())
    return c.cfg.stations;
  QMap<QString, int> pending, inflight;
  for (const Row &r : c.ledger.rows()) {
    const QString s = str(r, "state");
    if (s == QLatin1String("pending"))
      pending[str(r, "station")] += 1;
    else if (kInFlight.contains(s))
      inflight[str(r, "station")] += 1;
  }
  if (pending.isEmpty())
    return std::nullopt;
  int fewest = INT_MAX;
  for (auto it = pending.begin(); it != pending.end(); ++it)
    fewest = std::min(fewest, inflight.value(it.key()));
  QStringList out;
  for (auto it = pending.begin(); it != pending.end(); ++it)
    if (inflight.value(it.key()) == fewest)
      out << it.key();
  return out;
}

void record(Ctx &c, Slot &slot, const QString &cid, const Row &row,
            const Outcome &result) {
  if (result.kind == Outcome::Kind::Busy) {
    c.ledger.release(
        cid,
        QStringLiteral("portal busy; local state said this slot was free"));
    slot.markBusy(c.cfg.busyCooldown);
    slot.holding.reset();
    c.out.busy << slot.email;
    c.say(QStringLiteral(
              "  [BUSY] %1 -- the portal still holds its last request; cooling "
              "down %2 min")
              .arg(slot.email)
              .arg(c.cfg.busyCooldown / 60));
    return;
  }
  if (result.kind == Outcome::Kind::Rejected) {
    const int attempts = intOf(row, "attempts") + 1;
    if (result.permanent || attempts > c.cfg.maxAttempts) {
      c.ledger.update(cid, {{"state", "failed"},
                            {"email", QJsonValue::Null},
                            {"attempts", attempts},
                            {"note", result.detail}});
      c.out.rejected << cid;
      c.say(QStringLiteral("  [FAILED] %1 -- %2").arg(cid, result.detail));
      if (result.permanent)
        retireStation(c, str(row, "station"), result.detail);
    } else {
      c.ledger.release(cid, result.detail);
      c.ledger.update(cid, {{"attempts", attempts}});
      c.out.requeued << cid;
      c.say(QStringLiteral("  [RETRY] %1 -- %2 (attempt %3)")
                .arg(cid, result.detail)
                .arg(attempts));
    }
    slot.holding.reset();
    slot.lastResult = QStringLiteral("rejected");
    return;
  }
  const bool unconfirmed = result.kind == Outcome::Kind::AcceptedUnconfirmed;
  c.ledger.update(cid, {{"state", "submitted"},
                        {"submitted_at", nowStamp()},
                        {"note", unconfirmed ? QJsonValue(result.detail)
                                             : QJsonValue(QJsonValue::Null)}});
  slot.holding = c.ledger.byId(cid);
  slot.lastResult = QStringLiteral("accepted");
  c.out.submitted << cid;
  if (unconfirmed)
    c.say(QStringLiteral("  [UNCONFIRMED] %1 -- %2; kept as submitted")
              .arg(cid, result.detail));
  else
    c.say(QStringLiteral("  [SUBMITTED] %1 -> %2").arg(cid, slot.email));
}

void submit(Ctx &c, Portal &portal) {
  for (Slot *slot : c.pool.free()) {
    if (c.stopping())
      return;
    const auto allowed = nextStations(c);
    auto [row, blocker] = c.ledger.claim(slot->email, allowed);
    if (!row) {
      if (!blocker) {
        c.say(QStringLiteral("  nothing pending -- campaign complete"));
        return;
      }
      continue;
    }
    const QString cid = chunkId(*row);
    if (c.cfg.dryRun) {
      c.ledger.release(cid);
      c.say(
          QStringLiteral("  DRY would submit %1 -> %2").arg(cid, slot->email));
      continue;
    }
    c.say(QStringLiteral("  submitting %1 -> %2").arg(cid, slot->email));
    const Outcome result =
        portal.submit(str(*row, "station"), naiveUtc(str(*row, "start")),
                      naiveUtc(str(*row, "end")), slot->email);
    record(c, *slot, cid, *row, result);
  }
}

} // namespace

CycleResult runCycle(Ledger &ledger, Pool &pool, const SupervisorConfig &cfg,
                     Mailbox *mailbox, Portal *portal, Http *http,
                     const Log &log, const std::function<bool()> &shouldStop) {
  CycleResult out;
  Ctx c{ledger, pool, cfg, out, log, shouldStop};
  pool.refresh(ledger);
  out.paused = diskCheck(cfg.outDir, ledger.rows(), cfg.minFreeGb);
  if (mailbox) {
    reap(c, *mailbox, http);
    pool.refresh(ledger);
  }
  reclaim(c);
  if (cfg.retire)
    retire(c);
  pool.refresh(ledger);
  // A submission is a download promised for later; with no room for it, the
  // slot is better left free than filled with a link that cannot be kept.
  if (portal && out.paused.isEmpty() && !c.stopping())
    submit(c, *portal);
  return out;
}

} // namespace skg::tdvms
