// The ledger, plan, campaign, slots, disk, settings and history of the tdvms
// port. The cases mirror tdvms-pull's own tests, each of which pins a
// failure the Python campaign actually produced.
#include <QtTest>

#include "tdvms/campaign.h"
#include "tdvms/disk.h"
#include "tdvms/history.h"
#include "tdvms/ledger.h"
#include "tdvms/plan.h"
#include "tdvms/report.h"
#include "tdvms/settings.h"
#include "tdvms/slots.h"

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
} // namespace

class TstTdvmsCore : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  QString path(const QString &name) const { return tmp_.filePath(name); }

private slots:
  void initTestCase() {
    QVERIFY(tmp_.isValid());
    qputenv("XDG_CONFIG_HOME", path("config").toUtf8());
    qputenv("XDG_STATE_HOME", path("state").toUtf8());
    qunsetenv("TDVMS_SLOTS");
    qunsetenv("TDVMS_NOTIFY");
    for (const QString &v : kCredentialVars) {
      qunsetenv(v.toUtf8());
      qunsetenv(("AFAD_" + v.mid(6)).toUtf8());
    }
  }

  // --- ledger -----------------------------------------------------------
  void chunkNamedByStationAndStart() {
    Ledger led = write(path("a.jsonl"), {chunk("ELBA"), chunk("MANT")});
    QCOMPARE(str(*led.byId("ELBA:2025-09-09T00:00:00"), "station"),
             QString("ELBA"));
    QCOMPARE(str(*led.byId("MANT:2025-09-09T00:00:00"), "station"),
             QString("MANT"));
    QVERIFY(!led.byId("NOPE:2025-09-09T00:00:00"));
  }

  void updateTouchesOneStation() {
    Ledger led =
        write(path("b.jsonl"), {chunk("ELBA"), chunk("MANT"), chunk("SEMS")});
    QVERIFY(led.update("ELBA:2025-09-09T00:00:00", {{"state", "fetched"}}));
    QMap<QString, QString> states;
    for (const Row &r : led.rows())
      states[str(r, "station")] = str(r, "state");
    QCOMPARE(states["ELBA"], QString("fetched"));
    QCOMPARE(states["MANT"], QString("pending"));
    QCOMPARE(states["SEMS"], QString("pending"));
  }

  void writeDoesNotClobberConcurrentOne() {
    Ledger led = write(path("c.jsonl"), {chunk("ELBA"), chunk("MANT")});
    const QList<Row> stale = led.rows();
    led.update("MANT:2025-09-09T00:00:00",
               {{"state", "submitted"}, {"email", "you+a2@x"}});
    QCOMPARE(str(stale[1], "state"), QString("pending"));
    led.update("ELBA:2025-09-09T00:00:00", {{"state", "fetched"}});
    QCOMPARE(str(*led.byId("MANT:2025-09-09T00:00:00"), "state"),
             QString("submitted"));
  }

  void claimTakesOldestWindow() {
    Ledger led = write(path("d.jsonl"), {chunk("SEMS", "2025-12-01T00:00:00"),
                                         chunk("ELBA", "2024-05-01T00:00:00")});
    auto [row, blocker] = led.claim("you+a1@x");
    QVERIFY(!blocker);
    QVERIFY(row);
    QCOMPARE(str(*row, "station"), QString("ELBA"));
    QCOMPARE(str(*row, "state"), QString("claimed"));
    QVERIFY(ageSeconds(str(*row, "claimed_at")).value() < 5);
    auto [second, blocker2] = led.claim("you+a1@x");
    QVERIFY(!second);
    QVERIFY(blocker2);
    QCOMPARE(str(*blocker2, "station"), QString("ELBA"));
  }

  void releaseClearsStamps() {
    Ledger led = write(path("e.jsonl"),
                       {chunk("ELBA", "2025-09-09T00:00:00", "fetched",
                              {{"submitted_at", "2025-09-09T00:00:00+00:00"},
                               {"fetched_at", "2025-09-09T02:00:00+00:00"},
                               {"url", "http://x/y.zip"},
                               {"bytes", 1},
                               {"email", "you+a1@x"}})});
    led.release("ELBA:2025-09-09T00:00:00");
    const Row r = *led.byId("ELBA:2025-09-09T00:00:00");
    QCOMPARE(str(r, "state"), QString("pending"));
    QVERIFY(r.value("submitted_at").isNull());
    QVERIFY(r.value("fetched_at").isNull());
    QVERIFY(r.value("email").isNull());
    QVERIFY(r.value("url").isNull());
    QVERIFY(r.value("bytes").isNull());
  }

  void stampsAndAges() {
    QVERIFY(!ageSeconds(QString()));
    QVERIFY(!ageSeconds("not a date"));
    const QString s = nowStamp();
    QVERIFY(s.endsWith("+00:00"));
    QVERIFY(ageSeconds(s).value() < 5);
    // The Python tool's stamps and naive ones both parse.
    QVERIFY(ageSeconds("2020-01-01T00:00:00+00:00").value() > 1e8);
    QVERIFY(ageSeconds("2020-01-01T00:00:00").value() > 1e8);
  }

  void completeAndStations() {
    Ledger empty(path("none.jsonl"));
    QVERIFY(!empty.complete());
    QVERIFY(empty.rows().isEmpty());
    Ledger led = write(path("f.jsonl"),
                       {chunk("ELBA", "2025-09-09T00:00:00", "fetched"),
                        chunk("MANT", "2025-09-09T00:00:00", "nodata"),
                        chunk("ELBA", "2025-10-01T00:00:00", "retired")});
    QVERIFY(led.complete());
    QCOMPARE(led.stations(), (QStringList{"ELBA", "MANT"}));
    QCOMPARE(led.inState({"fetched", "nodata"}).size(), 2);
  }

  // --- plan -------------------------------------------------------------
  void planWindowsCoverAndSample() {
    auto w = planWindows("2024-05-01", "2024-07-01", 21);
    QCOMPARE(w.size(), 3);
    QCOMPARE(w[0].first, QString("2024-05-01T00:00:00"));
    QCOMPARE(w[0].second, QString("2024-05-22T00:00:00"));
    QCOMPARE(w[2].second, QString("2024-07-01T00:00:00")); // clipped at end
    auto s = planWindows("2024-05-01", "2024-05-29", 1, 7);
    QCOMPARE(s.size(), 4);
    QCOMPARE(s[1].first, QString("2024-05-08T00:00:00"));
    QCOMPARE(s[1].second, QString("2024-05-09T00:00:00"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             planWindows("2024-05-01", "2024-06-01", 0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             planWindows("2024-05-01", "2024-06-01", 7, 3));
  }

  void planIsIdempotent() {
    Ledger led(path("plan.jsonl"));
    QCOMPARE(enumerateChunks(led, "ELBA", "2024-05-01", "2024-07-01", 21), 3);
    QCOMPARE(enumerateChunks(led, "ELBA", "2024-05-01", "2024-08-01", 21), 2);
    QCOMPARE(led.rows().size(), 5);
    QCOMPARE(normaliseStation("TU.kand "), QString("KAND"));
    QCOMPARE(normaliseStation("ELBA"), QString("ELBA"));
  }

  // --- campaign ---------------------------------------------------------
  void campaignLayouts() {
    QDir().mkpath(path("proj/campaigns/van/raw"));
    QFile(path("proj/campaigns/van/ledger.jsonl")).open(QIODevice::WriteOnly);
    QFile(path("proj/silivri_ledger.jsonl")).open(QIODevice::WriteOnly);
    QFile(path("proj/tdvms_ledger.jsonl")).open(QIODevice::WriteOnly);
    const auto found = discoverCampaigns(path("proj"));
    QStringList names;
    for (const Campaign &c : found)
      names << c.name;
    QCOMPARE(names, (QStringList{"van", "silivri", "tdvms"}));
    QCOMPARE(QFileInfo(found[0].raw).fileName(), QString("raw"));
    QCOMPARE(QFileInfo(found[1].raw).fileName(), QString("silivri_raw"));
    QCOMPARE(QFileInfo(found[2].raw).fileName(), QString("afad_raw"));
    QCOMPARE(findCampaign("van", path("proj")).ledgerPath, found[0].ledgerPath);
    QVERIFY_THROWS_EXCEPTION(std::out_of_range,
                             findCampaign("nope", path("proj")));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             newCampaign(path("proj"), "Bad Name"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             newCampaign(path("proj"), "van"));
    const Campaign fresh = newCampaign(path("proj"), "elazig-2020");
    QVERIFY(QFileInfo(fresh.raw).isDir());
    QCOMPARE(fresh.name, QString("elazig-2020"));
    // raw.path pointer when the raw folder lives elsewhere
    const Campaign far = newCampaign(path("proj"), "far", path("elsewhere"));
    QVERIFY(QFileInfo(far.raw).isDir());
  }

  // --- slots ------------------------------------------------------------
  void poolIsAddressAndCount() {
    QCOMPARE(poolAddresses("you@gmail.com", 3),
             (QStringList{"you+a1@gmail.com", "you+a2@gmail.com",
                          "you+a3@gmail.com"}));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             poolAddresses("you+a1@gmail.com", 3));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, poolAddresses("you", 3));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             poolAddresses("you@gmail.com", 0));
  }

  void slotsKnowTheirChunks() {
    Ledger led = write(path("s.jsonl"),
                       {chunk("ELBA", "2025-09-09T00:00:00", "submitted",
                              {{"email", "You+A2@Gmail.com"}})});
    Pool p("you@gmail.com", 3);
    p.refresh(led);
    QVERIFY(p.get("you+a2@gmail.com")->holding);
    QVERIFY(p.get("YOU+A2@GMAIL.COM")->holding);
    QVERIFY(p.get("you+a1@gmail.com")->free());
    QCOMPARE(p.free().size(), 2);
    Ledger done =
        write(path("s2.jsonl"), {chunk("ELBA", "2025-09-09T00:00:00", "nodata",
                                       {{"email", "you+a1@gmail.com"}})});
    Pool p2("you@gmail.com", 2);
    QCOMPARE(p2.refresh(done).free().size(), 2);
  }

  void busySlotIsNotFree() {
    Pool p("you@gmail.com", 1);
    Slot *s = p.get("you+a1@gmail.com");
    QVERIFY(s->free());
    s->markBusy(1800);
    QVERIFY(!s->free() && s->remoteBusy());
    QVERIFY(p.free().isEmpty());
    s->markBusy(0);
    QVERIFY(s->free());
  }

  void poolLockRefusesSecondHolder() {
    QJsonObject info{{"command", "run"}};
    auto first = std::make_unique<PoolLock>(
        "you@gmail.com", path("balikesir_ledger.jsonl"), info, path("locks"));
    bool threw = false;
    try {
      PoolLock second("You@Gmail.com", path("silivri_ledger.jsonl"), info,
                      path("locks"));
    } catch (const PoolHeld &e) {
      threw = true;
      QVERIFY(QString::fromUtf8(e.what()).contains("balikesir_ledger.jsonl"));
    }
    QVERIFY(threw);
    const QJsonObject who = PoolLock::holder("you@gmail.com", path("locks"));
    QCOMPARE(who.value("command").toString(), QString("run"));
    QCOMPARE(PoolLock::holders(path("locks")).size(), 1);
    PoolLock other("other@gmail.com", path("b.jsonl"), info, path("locks"));
    first.reset();
    QVERIFY(PoolLock::holder("you@gmail.com", path("locks")).isEmpty());
    PoolLock again("you@gmail.com", path("silivri_ledger.jsonl"), info,
                   path("locks"));
  }

  // --- disk -------------------------------------------------------------
  void diskReserve() {
    QList<Row> rows{
        chunk("ELBA", "2025-09-09T00:00:00", "fetched", {{"bytes", 900e6}})};
    QCOMPARE(reserveBytes(rows, std::nullopt), qint64(2e9)); // floor wins
    rows[0].insert("bytes", 3e9);
    QCOMPARE(reserveBytes(rows, std::nullopt), qint64(6e9));
    QCOMPARE(reserveBytes(rows, 1.5), qint64(1.5e9));
    QVERIFY(diskCheck(tmp_.path(), rows, 0).isEmpty());    // off
    QVERIFY(!diskCheck(tmp_.path(), rows, 1e9).isEmpty()); // 1e9 GB: never
    QVERIFY(freeBytes(path("does/not/exist/yet")) > 0);
  }

  // --- settings ---------------------------------------------------------
  void envFilesRoundTrip() {
    const QString p = path("mailbox.env");
    Credentials c{"imap.gmail.com", "you@gmail.com", "pa'ss word\"x"};
    QVERIFY(saveCredentials(c, p));
    const auto back = readEnvFile(p, kCredentialVars);
    QCOMPARE(back.value("TDVMS_IMAP_PASS"), c.password);
    QCOMPARE(back.value("TDVMS_IMAP_USER"), c.user);
    // Python shlex.quote output and export lines read too.
    QFile f(path("py.env"));
    f.open(QIODevice::WriteOnly);
    f.write("# tdvms mailbox credentials -- written by tdvms-tui.\n"
            "export TDVMS_IMAP_HOST=imap.example.org\n"
            "TDVMS_IMAP_USER='a b@x.org'\n"
            "TDVMS_IMAP_PASS='it'\"'\"'s'\n");
    f.close();
    const auto py = readEnvFile(path("py.env"), kCredentialVars);
    QCOMPARE(py.value("TDVMS_IMAP_HOST"), QString("imap.example.org"));
    QCOMPARE(py.value("TDVMS_IMAP_USER"), QString("a b@x.org"));
    QCOMPARE(py.value("TDVMS_IMAP_PASS"), QString("it's"));
    QVERIFY(QFile(p).permissions() & QFileDevice::ReadOwner);
#ifndef Q_OS_WIN // Windows reports everyone-readable for any readable file
    QVERIFY(!(QFile(p).permissions() & QFileDevice::ReadGroup));
#endif
  }

  void credentialsEnvironmentWins() {
    QProcessEnvironment env;
    env.insert("AFAD_IMAP_HOST", "legacy.host");
    env.insert("TDVMS_IMAP_USER", "env@x");
    const auto got = resolveCredentials(env, path("mailbox.env"));
    QCOMPARE(got["TDVMS_IMAP_HOST"].value, QString("legacy.host"));
    QCOMPARE(got["TDVMS_IMAP_USER"].value, QString("env@x"));
    QCOMPARE(got["TDVMS_IMAP_PASS"].value, QString("pa'ss word\"x")); // file
    QCOMPARE(got["TDVMS_IMAP_PASS"].source, path("mailbox.env"));
    QCOMPARE(baseAddress(QString(), env, path("mailbox.env")),
             QString("env@x"));
    QCOMPARE(baseAddress("cli@x", env, path("mailbox.env")), QString("cli@x"));
  }

  void slotsAndNotifySettings() {
    QProcessEnvironment env;
    QCOMPARE(slotCount(0, env, path("settings.env")), kDefaultSlots);
    QCOMPARE(slotCount(3, env, path("settings.env")), 3);
    QVERIFY(saveSettings(
        {{kSlotsVar, "15"}, {kNotifyVar, "https://ntfy.sh/t, desktop"}},
        path("settings.env")));
    QCOMPARE(slotCount(0, env, path("settings.env")), 15);
    QCOMPARE(notifyTargets({}, env, path("settings.env")),
             (QStringList{"https://ntfy.sh/t", "desktop"}));
    env.insert(kSlotsVar, "zero");
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             slotCount(0, env, path("settings.env")));
    // saving one key keeps the other
    QVERIFY(saveSettings({{kSlotsVar, "4"}}, path("settings.env")));
    QCOMPARE(readEnvFile(path("settings.env"), {kNotifyVar}).value(kNotifyVar),
             QString("https://ntfy.sh/t, desktop"));
  }

  // --- history and report ----------------------------------------------
  void historyWarnsAboutDeadStations() {
    QList<Row> rows;
    for (int m = 1; m <= 3; ++m)
      rows << chunk(
          "BLKS",
          QStringLiteral("2025-%1-01T00:00:00").arg(m, 2, 10, QChar('0')),
          "nodata");
    rows << chunk("ELBA", "2025-01-01T00:00:00", "fetched");
    write(path("hist_ledger.jsonl"), rows);
    const auto hist = gatherHistory(discoverCampaigns(tmp_.path()));
    QCOMPARE(hist["BLKS"].kind(), QString("dead"));
    QVERIFY(hist["BLKS"].warning().contains(
        "3 no-data answer(s) for 2025-01-01..2025-09-30"));
    QVERIFY(hist["BLKS"].warning().contains("hist"));
    QVERIFY(hist["ELBA"].kind().isEmpty());
  }

  void statusSummary() {
    Ledger led = write(
        path("status_ledger.jsonl"),
        {chunk("ELBA", "2025-09-09T00:00:00", "fetched",
               {{"bytes", 5e8},
                {"submitted_at", "2025-09-09T00:00:00+00:00"},
                {"fetched_at", "2025-09-09T01:30:00+00:00"}}),
         chunk("ELBA", "2025-10-01T00:00:00", "submitted",
               {{"email", "you+a1@gmail.com"}, {"submitted_at", nowStamp()}}),
         chunk("MANT", "2025-09-09T00:00:00", "failed",
               {{"note", "gave up"}})});
    Pool pool("you@gmail.com", 2);
    const QStringList lines = summarise(led, &pool);
    const QString text = lines.join('\n');
    QVERIFY(text.contains("3 chunk(s)"));
    QVERIFY(text.contains(
        "awaiting link: ELBA:2025-10-01T00:00:00 (you+a1@gmail.com)"));
    QVERIFY(text.contains("FAILED: MANT:2025-09-09T00:00:00 -- gave up"));
    QVERIFY(text.contains("median 90 min"));
    QVERIFY(text.contains("1 of 2 free"));
    const auto lap = turnaround(led.rows());
    QVERIFY(lap);
    QCOMPARE(lap->n, 1);
    const auto per = perStation(led.rows());
    QCOMPARE(per.size(), 2);
    QCOMPARE(per[0].fetchedBytes, qint64(5e8));
  }
};

QTEST_GUILESS_MAIN(TstTdvmsCore)
#include "tst_tdvms_core.moc"
#include <stdexcept>
