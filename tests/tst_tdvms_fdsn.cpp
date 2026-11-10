// Whole days from an FDSN service: planning, adopting files on disk, the
// fetch loop with retries and no-data answers. Mirrors test_fdsn.py's intent.
#include <QtTest>

#include "tdvms/fdsn.h"
#include "tdvms/runner.h"

using namespace skg::tdvms;

namespace {
const QDateTime T0(QDate(2015, 7, 1), QTime(0, 0), QTimeZone::utc());

QByteArray record(const QDateTime &start, int nsamp, int rate,
                  const QByteArray &cha) {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001D ", 8);
  s.writeRawData("SVRC ", 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData("KO", 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0) << quint16(0);
  s << quint16(nsamp) << qint16(rate) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1) << quint8(9)
    << quint8(0);
  return r.leftJustified(512, '\0');
}

// `hours` hours of 1 Hz data from midnight on the two horizontal channels.
QByteArray dayData(double hours) {
  QByteArray out;
  const int records = int(hours * 3600 / 100);
  for (const QByteArray &cha : {QByteArray("BHZ"), QByteArray("BHN")})
    for (int i = 0; i < records; ++i)
      out += record(T0.addSecs(i * 100), 100, 1, cha);
  return out;
}

class FakeHttp : public Http {
public:
  QList<HttpResult> answers; // consumed in order; empty => 200 with dayData(2)
  QList<QUrl> urls;
  HttpResult get(const QUrl &url, int) override {
    urls << url;
    if (!answers.isEmpty())
      return answers.takeFirst();
    HttpResult r;
    r.status = 200;
    r.body = dayData(2);
    return r;
  }
  HttpResult postJson(const QUrl &, const QByteArray &, int,
                      const QMap<QString, QString> &) override {
    return {};
  }
  HttpResult postBody(const QUrl &, const QByteArray &, int,
                      const QMap<QString, QString> &) override {
    return {};
  }
  HttpResult getToDevice(const QUrl &, QIODevice *, int,
                         const std::function<bool()> &) override {
    return {};
  }
};
} // namespace

class TstTdvmsFdsn : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  QString path(const QString &n) const { return tmp_.filePath(n); }

private slots:
  void initTestCase() { QVERIFY(tmp_.isValid()); }

  void pathsAndNames() {
    const Campaign flat = campaignForLedger(path("sivrice_ledger.jsonl"));
    QCOMPARE(QFileInfo(fdsnStatePath(flat)).fileName(),
             QString("sivrice_fdsn.jsonl"));
    QCOMPARE(QFileInfo(fdsnDefaultOut(flat)).fileName(),
             QString("sivrice_fdsn_raw"));
    QDir().mkpath(path("campaigns/van"));
    const Campaign folder =
        campaignForLedger(path("campaigns/van/ledger.jsonl"));
    QCOMPARE(QFileInfo(fdsnStatePath(folder)).fileName(),
             QString("fdsn.jsonl"));
    QCOMPARE(fdsnFileName("KO", "SVRC", QDate(2015, 7, 1), "BH?"),
             QString("KO_SVRC_01072015_000000_02072015_000000_BH.mseed"));
    QCOMPARE(daysBetween(QDate(2015, 7, 1), QDate(2015, 7, 4)).size(), 3);
  }

  void dayHoursPerChannel() {
    const auto h = dayHours(dayData(2), QDate(2015, 7, 1));
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.value("BHZ"), 2.0);
    QCOMPARE(h.value("BHN"), 2.0);
  }

  void planAdoptAndWork() {
    Ledger led(path("x_fdsn.jsonl"));
    const auto days = daysBetween(QDate(2015, 7, 1), QDate(2015, 7, 5));
    QCOMPARE(fdsnPlan(led, "SVRC", days, "KO", "BH?", kKoeri), 4);
    QCOMPARE(fdsnPlan(led, "SVRC", days, "KO", "BH?", kKoeri), 0); // idempotent
    QCOMPARE(str(led.rows().first(), "network"), QString("KO"));

    // One day is already on disk: adopted, not downloaded.
    const QString out = path("x_fdsn_raw");
    QDir().mkpath(out + "/SVRC");
    QFile f(out + "/SVRC/" +
            fdsnFileName("KO", "SVRC", QDate(2015, 7, 1), "BH?"));
    f.open(QIODevice::WriteOnly);
    f.write(dayData(1));
    f.close();
    QCOMPARE(fdsnAdopt(led, out), 1);
    const Row adopted = *led.byId("SVRC:2015-07-01T00:00:00");
    QCOMPARE(str(adopted, "state"), QString("fetched"));
    QCOMPARE(str(adopted, "note"), kAdoptedNote);
    QCOMPARE(adopted.value("hours").toObject().value("BHZ").toDouble(), 1.0);

    // A stale claim from a dead pull goes back to pending.
    led.update("SVRC:2015-07-03T00:00:00",
               {{"state", "claimed"}, {"claimed_at", nowStamp()}});
    QCOMPARE(fdsnReclaim(led), 1);

    FakeHttp http;
    HttpResult nodata;
    nodata.status = 404;
    HttpResult failing;
    failing.status = 0;
    failing.error = "connection reset";
    HttpResult ok;
    ok.status = 200;
    ok.body = dayData(2);
    // day 2: ok; day 3: 404 (no data); day 4: fails once then ok
    http.answers << ok << nodata << failing << ok;
    FdsnOptions opt;
    opt.retries = 2;
    opt.pauseMs = 1;
    QStringList log;
    const auto counts =
        fdsnWork(led, out, http, opt, [&](const QString &m) { log << m; });
    QCOMPARE(counts.value("fetched"), 2);
    QCOMPARE(counts.value("nodata"), 1);
    QCOMPARE(http.urls.size(), 4);
    QVERIFY(http.urls.first().toString().contains("fdsnws/dataselect/1/query"));
    QVERIFY(http.urls.first().query().contains("nodata=404"));
    QVERIFY(QFileInfo::exists(
        out + "/SVRC/" + fdsnFileName("KO", "SVRC", QDate(2015, 7, 2), "BH?")));
    QCOMPARE(str(*led.byId("SVRC:2015-07-03T00:00:00"), "state"),
             QString("nodata"));
    QCOMPARE(str(*led.byId("SVRC:2015-07-04T00:00:00"), "state"),
             QString("fetched"));
    QCOMPARE(intOf(*led.byId("SVRC:2015-07-04T00:00:00"), "attempts"), 1);
    QVERIFY(log.join('\n').contains("try 1/2"));
    QVERIFY(led.complete());

    // Retry failed: nothing failed here, so zero.
    QCOMPARE(fdsnRetryFailed(led, "SVRC"), 0);
  }

  void retriesExhaustedIsFailed() {
    Ledger led(path("y_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1)}, "KO", "BH?", kKoeri);
    FakeHttp http;
    HttpResult failing;
    failing.error = "timeout";
    http.answers << failing << failing;
    FdsnOptions opt;
    opt.retries = 2;
    opt.pauseMs = 1;
    const auto counts = fdsnWork(led, path("y_raw"), http, opt);
    QCOMPARE(counts.value("failed"), 1);
    QVERIFY(
        str(*led.byId("SVRC:2015-07-01T00:00:00"), "note").contains("timeout"));
    QCOMPARE(fdsnRetryFailed(led, "SVRC"), 1);
    QCOMPARE(str(*led.byId("SVRC:2015-07-01T00:00:00"), "state"),
             QString("pending"));
  }

  // A 200 whose connection dropped is not a day of data: it is tried again,
  // and when it never completes the day is failed, with nothing on disk.
  void aCutShortAnswerIsNotKept() {
    Ledger led(path("cut_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1), QDate(2015, 7, 2)}, "KO", "BH?",
             kKoeri);
    FakeHttp http;
    HttpResult cut;
    cut.status = 200;
    cut.body = dayData(2).left(700); // a record and a bit
    cut.error = "Connection closed";
    HttpResult whole;
    whole.status = 200;
    whole.body = dayData(2);
    // day 1: cut, then whole on the second try; day 2: cut twice.
    http.answers << cut << whole << cut << cut;
    FdsnOptions opt;
    opt.retries = 2;
    opt.pauseMs = 1;
    const QString out = path("cut_raw");
    const auto counts = fdsnWork(led, out, http, opt);
    QCOMPARE(counts.value("fetched"), 1);
    QCOMPARE(counts.value("failed"), 1);
    const Row first = *led.byId("SVRC:2015-07-01T00:00:00");
    QCOMPARE(str(first, "state"), QString("fetched"));
    QCOMPARE(qint64(first.value("bytes").toDouble()), qint64(whole.body.size()));
    QCOMPARE(QFileInfo(str(first, "path")).size(), qint64(whole.body.size()));
    const Row second = *led.byId("SVRC:2015-07-02T00:00:00");
    QCOMPARE(str(second, "state"), QString("failed"));
    QVERIFY(str(second, "note").contains("Connection closed"));
    QVERIFY(!QFileInfo::exists(
        out + "/SVRC/" + fdsnFileName("KO", "SVRC", QDate(2015, 7, 2), "BH?")));
  }

  // A 200 with something that is not miniSEED (an error page): the day is
  // failed with the reason, no file is kept, and the days after it are
  // fetched. Before, the scan threw, the day stayed claimed, and every
  // later start threw again before fetching anything.
  void anAnswerThatIsNotMiniseedFailsItsDayOnly() {
    Ledger led(path("html_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1), QDate(2015, 7, 2)}, "KO", "BH?",
             kKoeri);
    FakeHttp http;
    HttpResult page;
    page.status = 200;
    page.body = QByteArray("<html><body>Service temporarily unavailable"
                           "</body></html>")
                    .leftJustified(600, ' ');
    http.answers << page; // day 1; day 2 gets the default good answer
    FdsnOptions opt;
    opt.retries = 1;
    opt.pauseMs = 1;
    const QString out = path("html_raw");
    QMap<QString, int> counts;
    QVERIFY_THROWS_NO_EXCEPTION(counts = fdsnWork(led, out, http, opt));
    QCOMPARE(counts.value("failed"), 1);
    QCOMPARE(counts.value("fetched"), 1);
    const Row bad = *led.byId("SVRC:2015-07-01T00:00:00");
    QCOMPARE(str(bad, "state"), QString("failed"));
    QVERIFY2(str(bad, "note").contains("not miniSEED"),
             qPrintable(str(bad, "note")));
    QVERIFY(!QFileInfo::exists(
        out + "/SVRC/" + fdsnFileName("KO", "SVRC", QDate(2015, 7, 1), "BH?")));
    QVERIFY(led.inState({"claimed"}).isEmpty());
    // A later start is not held up by it.
    QVERIFY_THROWS_NO_EXCEPTION(fdsnAdopt(led, out));
    QCOMPARE(fdsnRetryFailed(led, "SVRC"), 1);
    QVERIFY_THROWS_NO_EXCEPTION(counts = fdsnWork(led, out, http, opt));
    QCOMPARE(counts.value("fetched"), 1);
    QVERIFY(led.complete());
  }

  // "Fetched" means the file is on disk. Here the station's folder cannot
  // be made (a file is in its place), as a full disk would refuse the write.
  void aDayThatCannotBeSavedIsNotFetched() {
    Ledger led(path("full_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1)}, "KO", "BH?", kKoeri);
    const QString out = path("full_raw");
    QDir().mkpath(out);
    QFile block(out + "/SVRC");
    QVERIFY(block.open(QIODevice::WriteOnly));
    block.close();
    FakeHttp http;
    FdsnOptions opt;
    opt.retries = 1;
    opt.pauseMs = 1;
    const auto counts = fdsnWork(led, out, http, opt);
    QCOMPARE(counts.value("fetched"), 0);
    QCOMPARE(counts.value("failed"), 1);
    const Row r = *led.byId("SVRC:2015-07-01T00:00:00");
    QVERIFY2(str(r, "note").contains("cannot write") ||
                 str(r, "note").contains("could not save"),
             qPrintable(str(r, "note")));
    QVERIFY(str(r, "path").isEmpty());
  }

  // One unreadable file under a day's name does not stop the others from
  // being adopted, and is not deleted: it is set aside.
  void adoptSetsABadFileAsideAndGoesOn() {
    Ledger led(path("adopt_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1), QDate(2015, 7, 2)}, "KO", "BH?",
             kKoeri);
    const QString out = path("adopt_raw");
    QDir().mkpath(out + "/SVRC");
    const QString bad =
        out + "/SVRC/" + fdsnFileName("KO", "SVRC", QDate(2015, 7, 1), "BH?");
    const QString good =
        out + "/SVRC/" + fdsnFileName("KO", "SVRC", QDate(2015, 7, 2), "BH?");
    {
      QFile f(bad);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(QByteArray("this is not a waveform").leftJustified(2048, '.'));
      QFile g(good);
      QVERIFY(g.open(QIODevice::WriteOnly));
      g.write(dayData(1));
    }
    int adopted = -1;
    QVERIFY_THROWS_NO_EXCEPTION(adopted = fdsnAdopt(led, out));
    QCOMPARE(adopted, 1);
    QCOMPARE(str(*led.byId("SVRC:2015-07-02T00:00:00"), "state"),
             QString("fetched"));
    const Row r = *led.byId("SVRC:2015-07-01T00:00:00");
    QCOMPARE(str(r, "state"), QString("failed"));
    QVERIFY2(str(r, "note").contains("not readable miniSEED"),
             qPrintable(str(r, "note")));
    QVERIFY(!QFileInfo::exists(bad));
    QVERIFY(QFileInfo::exists(bad + ".unreadable"));
    // Asked again, the day is fetched fresh; the file set aside stays.
    QCOMPARE(fdsnRetryFailed(led, "SVRC"), 1);
    QCOMPARE(fdsnAdopt(led, out), 0);
    FakeHttp http;
    FdsnOptions opt;
    opt.retries = 1;
    opt.pauseMs = 1;
    QCOMPARE(fdsnWork(led, out, http, opt).value("fetched"), 1);
    QVERIFY(QFileInfo::exists(bad));
    QVERIFY(QFileInfo::exists(bad + ".unreadable"));
  }

  void stopRequestLeavesTheDayPending() {
    Ledger led(path("z_fdsn.jsonl"));
    fdsnPlan(led, "SVRC", {QDate(2015, 7, 1)}, "KO", "BH?", kKoeri);
    FakeHttp http;
    const auto counts =
        fdsnWork(led, path("z_raw"), http, {}, {}, [] { return true; });
    QVERIFY(counts.isEmpty());
    QCOMPARE(str(*led.byId("SVRC:2015-07-01T00:00:00"), "state"),
             QString("pending"));
  }

  // --- the watch --------------------------------------------------------
  void watchRaisesOnceAndClears() {
    Ledger led(path("w.jsonl"));
    led.edit([](QList<Row> &rows) {
      rows << Ledger::makeRow("ELBA", "2025-09-09T00:00:00",
                              "2025-09-30T00:00:00");
      return true;
    });
    Pool pool("you@gmail.com", 1);
    QStringList sent;
    Watch watch([&](const QString &t, const QString &, bool) { sent << t; },
                "camp", led, pool);
    CycleResult r;
    r.mailError = "boom";
    watch.after(r);
    watch.after(r);
    QVERIFY(sent.isEmpty()); // under the threshold of 3
    watch.after(r);
    QCOMPARE(sent, QStringList{"camp: can't read the mailbox"});
    watch.after(r);
    QCOMPARE(sent.size(), 1); // raised once
    CycleResult fine;
    watch.after(fine);
    QCOMPARE(sent.last(), QString("camp: mailbox readable again"));
    // completion is announced once
    led.update("ELBA:2025-09-09T00:00:00",
               {{"state", "fetched"}, {"bytes", 1e9}});
    watch.after(fine);
    watch.after(fine);
    QCOMPARE(sent.count("camp: complete"), 1);
  }

  void notifierPostsToUrlsAndHandsDesktopOn() {
    class PostHttp : public FakeHttp {
    public:
      QList<QPair<QUrl, QMap<QString, QString>>> posts;
      HttpResult postBody(const QUrl &u, const QByteArray &, int,
                          const QMap<QString, QString> &h) override {
        posts.append({u, h});
        HttpResult r;
        r.status = 200;
        return r;
      }
    } http;
    QStringList desktop;
    Notify n = makeNotifier(
        {"https://ntfy.sh/t", "desktop"}, &http,
        [&](const QString &t, const QString &, bool) { desktop << t; }, {});
    QVERIFY(n);
    n("title", "body", true);
    QCOMPARE(http.posts.size(), 1);
    QCOMPARE(http.posts.first().second.value("Priority"), QString("high"));
    QCOMPARE(desktop, QStringList{"title"});
    QVERIFY(!makeNotifier({}, &http, {}, {}));
  }
};

QTEST_GUILESS_MAIN(TstTdvmsFdsn)
#include "tst_tdvms_fdsn.moc"
