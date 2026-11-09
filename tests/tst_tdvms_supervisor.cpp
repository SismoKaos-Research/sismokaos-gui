// The loop as a pure function of a ledger, an inbox and a portal. Every
// case is a failure the Python campaign produced; mirrors test_supervisor.py.
#include <QtTest>

#include "tdvms/supervisor.h"

using namespace skg::tdvms;

namespace {
Row chunk(const QString &station = "ELBA",
          const QString &start = "2025-09-09T00:00:00",
          const QString &state = "pending", const QJsonObject &extra = {}) {
  Row r = Ledger::makeRow(station, start, "2025-09-30T00:00:00");
  r.insert("state", state);
  for (auto it = extra.begin(); it != extra.end(); ++it)
    r.insert(it.key(), it.value());
  return r;
}

Ledger write(const QString &path, const QList<Row> &rows) {
  QFile f(path);
  f.open(QIODevice::WriteOnly | QIODevice::Truncate);
  for (const Row &r : rows)
    f.write(QJsonDocument(r).toJson(QJsonDocument::Compact) + "\n");
  f.close();
  return Ledger(path);
}

struct Call {
  QString station, date, email;
};

class FakePortal : public Portal {
public:
  QList<Outcome> answers;
  QList<Call> calls;
  Outcome submit(const QString &station, const QDateTime &start,
                 const QDateTime &, const QString &email) override {
    calls.append({station, start.toString("yyyy-MM-dd"), email});
    return answers.isEmpty() ? Outcome::accepted(0, "ok") : answers.takeFirst();
  }
};

class FakeMailbox : public Mailbox {
public:
  QList<MailEvent> events;
  QString boom;
  QList<QPair<QString, bool>> consumed;
  QList<MailEvent> read(const QSet<QString> &, bool) override {
    if (!boom.isEmpty())
      throw std::runtime_error(boom.toStdString());
    return events;
  }
  void consume(const QString &uid, bool ok) override {
    consumed.append({uid, ok});
  }
};

MailEvent noData(const QString &uid, const QString &to) {
  MailEvent e;
  e.kind = MailEvent::Kind::NoData;
  e.uid = uid;
  e.to = to;
  return e;
}
MailEvent foreign(const QString &uid, const QString &to) {
  MailEvent e;
  e.kind = MailEvent::Kind::Foreign;
  e.uid = uid;
  e.to = to;
  return e;
}
} // namespace

class TstTdvmsSupervisor : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  SupervisorConfig cfg_;
  int n_ = 0;
  QString fresh() {
    return tmp_.filePath(QStringLiteral("l%1.jsonl").arg(++n_));
  }

private slots:
  void initTestCase() {
    QVERIFY(tmp_.isValid());
    cfg_.outDir = tmp_.filePath("raw");
    cfg_.minFreeGb = 0; // the disk guard is not under test here
  }

  void noDataFreesItsSlotAndItIsRefilled() {
    Ledger led = write(
        fresh(),
        {chunk("ELBA", "2025-09-09T00:00:00", "submitted",
               {{"email", "you+a1@gmail.com"}, {"submitted_at", nowStamp()}}),
         chunk("ELBA", "2025-10-01T00:00:00")});
    Pool pool("you@gmail.com", 1);
    FakeMailbox box;
    box.events << noData("1", "you+a1@gmail.com");
    FakePortal portal;
    const CycleResult out = runCycle(led, pool, cfg_, &box, &portal, nullptr);
    QCOMPARE(out.nodata, QStringList{"ELBA:2025-09-09T00:00:00"});
    QCOMPARE(out.submitted, QStringList{"ELBA:2025-10-01T00:00:00"});
    QCOMPARE(portal.calls.first().email, QString("you+a1@gmail.com"));
    QCOMPARE(box.consumed, (QList<QPair<QString, bool>>{{"1", true}}));
  }

  void claimThatDiedMidPostIsReclaimed() {
    Ledger led =
        write(fresh(), {chunk("SLV", "2025-09-09T00:00:00", "claimed",
                              {{"email", "you+a1@gmail.com"},
                               {"claimed_at", "2020-01-01T00:00:00+00:00"}})});
    Pool pool("you@gmail.com", 1);
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, nullptr, nullptr);
    QCOMPARE(out.reclaimed, QStringList{"SLV:2025-09-09T00:00:00"});
    const Row row = *led.byId("SLV:2025-09-09T00:00:00");
    QCOMPARE(str(row, "state"), QString("pending"));
    QVERIFY(row.value("email").isNull());
    QCOMPARE(intOf(row, "attempts"), 1);
  }

  void submissionInsideItsWindowIsLeftAlone() {
    Ledger led = write(
        fresh(),
        {chunk("ELBA", "2025-09-09T00:00:00", "submitted",
               {{"email", "you+a1@gmail.com"}, {"submitted_at", nowStamp()}})});
    Pool pool("you@gmail.com", 1);
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, nullptr, nullptr);
    QVERIFY(out.reclaimed.isEmpty());
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "state"),
             QString("submitted"));
  }

  void unstampedRowIsStampedNotReaped() {
    Ledger led =
        write(fresh(), {chunk("ELBA", "2025-09-09T00:00:00", "submitted",
                              {{"email", "you+a1@gmail.com"}})});
    Pool pool("you@gmail.com", 1);
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, nullptr, nullptr);
    QVERIFY(out.reclaimed.isEmpty());
    QVERIFY(
        !str(*led.byId("ELBA:2025-09-09T00:00:00"), "submitted_at").isEmpty());
  }

  void chunkThatKeepsFailingIsRetired() {
    Ledger led =
        write(fresh(), {chunk("ELBA", "2025-09-09T00:00:00", "claimed",
                              {{"email", "you+a1@gmail.com"},
                               {"claimed_at", "2020-01-01T00:00:00+00:00"},
                               {"attempts", 3}})});
    Pool pool("you@gmail.com", 1);
    SupervisorConfig cfg = cfg_;
    cfg.maxAttempts = 3;
    const CycleResult out = runCycle(led, pool, cfg, nullptr, nullptr, nullptr);
    QCOMPARE(out.rejected, QStringList{"ELBA:2025-09-09T00:00:00"});
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "state"),
             QString("failed"));
  }

  void portalBusyBacksOutAndCoolsTheSlot() {
    Ledger led = write(fresh(), {chunk("ELBA"), chunk("MANT")});
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    portal.answers << Outcome::busy("still processing")
                   << Outcome::accepted(0, "ok");
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QCOMPARE(out.busy, QStringList{"you+a1@gmail.com"});
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "state"),
             QString("pending"));
    QVERIFY(pool.get("you+a1@gmail.com")->remoteBusy());
    QVERIFY(pool.free().isEmpty());
    runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QCOMPARE(portal.calls.size(),
             1); // the second cycle did not submit into the cooldown
  }

  void unconfirmedSubmissionKeepsItsClaim() {
    Ledger led = write(fresh(), {chunk("ELBA")});
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    portal.answers << Outcome::unconfirmed("RemoteDisconnected");
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    const Row row = *led.byId("ELBA:2025-09-09T00:00:00");
    QCOMPARE(str(row, "state"), QString("submitted"));
    QCOMPARE(str(row, "email"), QString("you+a1@gmail.com"));
    QCOMPARE(out.submitted, QStringList{"ELBA:2025-09-09T00:00:00"});
    QVERIFY(str(row, "note").contains("RemoteDisconnected"));
  }

  void unlistedStationIsNotRetried() {
    Ledger led =
        write(fresh(), {chunk("BAKC"), chunk("BAKC", "2025-10-01T00:00:00")});
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    portal.answers << Outcome::rejected("BAKC: not in the TDVMS station list",
                                        true);
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QCOMPARE(out.rejected, QStringList{"BAKC:2025-09-09T00:00:00"});
    QCOMPARE(str(*led.byId("BAKC:2025-09-09T00:00:00"), "state"),
             QString("failed"));
    // one permanent rejection retires the station's other pending windows
    QCOMPARE(out.retired, QStringList{"BAKC:2025-10-01T00:00:00"});
  }

  void stationThatNeverReturnsDataStopsTakingSlots() {
    QList<Row> rows;
    for (int m = 1; m <= 8; ++m)
      rows << chunk(
          "BLKS",
          QStringLiteral("2025-%1-01T00:00:00").arg(m, 2, 10, QChar('0')),
          "nodata");
    rows << chunk("BLKS", "2025-10-01T00:00:00")
         << chunk("BLKS", "2025-11-01T00:00:00");
    Ledger led = write(fresh(), rows);
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QCOMPARE(out.retired.size(), 2);
    QVERIFY(out.submitted.isEmpty());
    for (const Row &r : led.rows())
      if (str(r, "start").startsWith("2025-1"))
        QCOMPARE(str(r, "state"), QString("retired"));
  }

  void gapInAGoodSpanIsNotRetired() {
    QList<Row> rows;
    for (int m = 1; m <= 9; ++m)
      rows << chunk(
          "ELBA",
          QStringLiteral("2025-%1-01T00:00:00").arg(m, 2, 10, QChar('0')),
          "nodata");
    rows << chunk("ELBA", "2024-05-01T00:00:00", "fetched")
         << chunk("ELBA", "2025-12-01T00:00:00");
    Ledger led = write(fresh(), rows);
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QVERIFY(out.retired.isEmpty());
    QCOMPARE(out.submitted, QStringList{"ELBA:2025-12-01T00:00:00"});
  }

  void retirementCanBeTurnedOff() {
    QList<Row> rows;
    for (int m = 1; m <= 8; ++m)
      rows << chunk(
          "BLKS",
          QStringLiteral("2025-%1-01T00:00:00").arg(m, 2, 10, QChar('0')),
          "nodata");
    rows << chunk("BLKS", "2025-10-01T00:00:00");
    Ledger led = write(fresh(), rows);
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    SupervisorConfig cfg = cfg_;
    cfg.retire = false;
    const CycleResult out = runCycle(led, pool, cfg, nullptr, &portal, nullptr);
    QVERIFY(out.retired.isEmpty());
    QCOMPARE(out.submitted, QStringList{"BLKS:2025-10-01T00:00:00"});
  }

  void foreignLinkIsLeftUnread() {
    Ledger led = write(
        fresh(),
        {chunk("ELBA", "2025-09-09T00:00:00", "submitted",
               {{"email", "you+a1@gmail.com"}, {"submitted_at", nowStamp()}})});
    Pool pool("you@gmail.com", 1);
    FakeMailbox box;
    box.events << foreign("7", "someone+z9@gmail.com");
    FakePortal portal;
    const CycleResult out = runCycle(led, pool, cfg_, &box, &portal, nullptr);
    QCOMPARE(out.foreign, QStringList{"someone+z9@gmail.com"});
    QVERIFY(box.consumed.isEmpty());
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "state"),
             QString("submitted"));
  }

  void deadMailboxDoesNotStopSubmissions() {
    Ledger led = write(fresh(), {chunk("ELBA")});
    Pool pool("you@gmail.com", 1);
    FakeMailbox box;
    box.boom = "connection reset";
    FakePortal portal;
    const CycleResult out = runCycle(led, pool, cfg_, &box, &portal, nullptr);
    QVERIFY(!out.notes.isEmpty() &&
            out.notes.first().contains("connection reset"));
    QCOMPARE(out.mailError, QString("connection reset"));
    QCOMPARE(out.submitted, QStringList{"ELBA:2025-09-09T00:00:00"});
  }

  void oneStationCannotStarveTheOthers() {
    QList<Row> rows;
    for (int m = 1; m <= 6; ++m)
      rows << chunk(
          "MANT",
          QStringLiteral("2024-%1-01T00:00:00").arg(m, 2, 10, QChar('0')));
    rows << chunk("ELBA", "2025-06-01T00:00:00");
    Ledger led = write(fresh(), rows);
    Pool pool("you@gmail.com", 2);
    FakePortal portal;
    runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QSet<QString> stations;
    for (const Call &c : portal.calls)
      stations.insert(c.station);
    QCOMPARE(stations, (QSet<QString>{"MANT", "ELBA"}));
  }

  void dryRunChangesNothing() {
    const QString p = fresh();
    Ledger led = write(p, {chunk("ELBA")});
    QFile f(p);
    f.open(QIODevice::ReadOnly);
    const QByteArray before = f.readAll();
    f.close();
    Pool pool("you@gmail.com", 1);
    FakePortal portal;
    SupervisorConfig cfg = cfg_;
    cfg.dryRun = true;
    runCycle(led, pool, cfg, nullptr, &portal, nullptr);
    QVERIFY(portal.calls.isEmpty());
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "state"),
             QString("pending"));
    f.open(QIODevice::ReadOnly);
    QCOMPARE(f.readAll(), before);
  }

  void emptyCampaignIsQuiet() {
    Ledger led =
        write(fresh(), {chunk("ELBA", "2025-09-09T00:00:00", "fetched")});
    Pool pool("you@gmail.com", 2);
    FakePortal portal;
    const CycleResult out =
        runCycle(led, pool, cfg_, nullptr, &portal, nullptr);
    QVERIFY(out.submitted.isEmpty() && out.quiet());
    QCOMPARE(out.summary(), QString("nothing to do"));
    QVERIFY(led.complete());
  }

  void stopRequestEndsTheCycleEarly() {
    Ledger led = write(fresh(), {chunk("ELBA"), chunk("MANT")});
    Pool pool("you@gmail.com", 2);
    FakePortal portal;
    const CycleResult out = runCycle(led, pool, cfg_, nullptr, &portal, nullptr,
                                     {}, [] { return true; });
    QVERIFY(portal.calls.isEmpty());
    QVERIFY(out.quiet());
  }
};

QTEST_GUILESS_MAIN(TstTdvmsSupervisor)
#include "tst_tdvms_supervisor.moc"
