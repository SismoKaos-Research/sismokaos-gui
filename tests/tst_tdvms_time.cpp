// Zone-less times in ledgers, archive names and plans are UTC. They must
// read the same on a machine in any time zone, also for a time of day that
// the local clock skipped: in Turkey 2015-03-29 03:00 to 04:00 did not
// exist. This test runs with TZ=Europe/Istanbul (set in main, before
// anything reads the zone) on the systems that honour TZ; elsewhere it runs
// in whatever zone the machine has and checks the same values.
#include <QtTest>

#include <QCoreApplication>
#include <QTimeZone>

#include <cstdlib>
#include <ctime>

#include "tdvms/fetch.h"
#include "tdvms/ledger.h"
#include "tdvms/plan.h"

using namespace skg::tdvms;

namespace {
// Seconds since the epoch, worked out by hand so that nothing here goes
// through the code under test.
constexpr qint64 kMar29 = 1427587200;         // 2015-03-29T00:00:00Z
constexpr qint64 kGap = kMar29 + 3 * 3600 + 1800; // 03:30:00Z that day
constexpr qint64 kJulyNoon = 1435752000;      // 2015-07-01T12:00:00Z
} // namespace

class TstTdvmsTime : public QObject {
  Q_OBJECT
private slots:
  void theZoneOfThisRun() {
    const QByteArray zone = QTimeZone::systemTimeZoneId();
    qInfo("system time zone: %s", zone.constData());
#ifdef Q_OS_LINUX // where TZ decides the zone Qt sees
    QCOMPARE(zone, QByteArray("Europe/Istanbul"));
    // The premise: read as local time, 03:30 that night is not 03:30.
    const QDateTime local(QDate(2015, 3, 29), QTime(3, 30));
    QVERIFY(!local.isValid() || local.time() != QTime(3, 30) ||
            local.toSecsSinceEpoch() != kGap);
#endif
  }

  void aTimeInASkippedHourIsReadAsWritten() {
    const QDateTime t = naiveUtc(QStringLiteral("2015-03-29T03:30:00"));
    QVERIFY(t.isValid());
    QCOMPARE(t.toSecsSinceEpoch(), kGap);
    QCOMPARE(t.toUTC().time(), QTime(3, 30));
    QCOMPARE(t.toUTC().date(), QDate(2015, 3, 29));
    QCOMPARE(naiveUtc(QStringLiteral("2015-03-29 03:30:00")).toSecsSinceEpoch(),
             kGap);
    QCOMPARE(
        naiveUtc(QStringLiteral("2015-03-29T03:30:00.250")).toMSecsSinceEpoch(),
        kGap * 1000 + 250);
    QCOMPARE(naiveUtc(QStringLiteral("2015-03-29")).toSecsSinceEpoch(), kMar29);
    QCOMPARE(naiveUtc(QStringLiteral("29032015"), QStringLiteral("ddMMyyyy"),
                      QStringLiteral("033000"), QStringLiteral("HHmmss"))
                 .toSecsSinceEpoch(),
             kGap);
  }

  void anOrdinaryTimeToo() {
    QCOMPARE(naiveUtc(QStringLiteral("2015-07-01T12:00:00")).toSecsSinceEpoch(),
             kJulyNoon);
    QCOMPARE(naiveUtc(QStringLiteral("01072015"), QStringLiteral("ddMMyyyy"),
                      QStringLiteral("120000"), QStringLiteral("HHmmss"))
                 .toSecsSinceEpoch(),
             kJulyNoon);
  }

  void whatIsNotATimeIsInvalid() {
    QVERIFY(!naiveUtc(QString()).isValid());
    QVERIFY(!naiveUtc(QStringLiteral("yesterday")).isValid());
    QVERIFY(!naiveUtc(QStringLiteral("2015-13-01")).isValid());
    QVERIFY(!naiveUtc(QStringLiteral("2015-03-29T25:00:00")).isValid());
    QVERIFY(!naiveUtc(QStringLiteral("2015-03-29X03:30:00")).isValid());
  }

  void ledgerStamps() {
    // Without a zone: UTC. With one: the instant it names.
    QCOMPARE(parseStamp(QStringLiteral("2015-03-29T03:30:00")).toSecsSinceEpoch(),
             kGap);
    QCOMPARE(
        parseStamp(QStringLiteral("2015-03-29T03:30:00+00:00")).toSecsSinceEpoch(),
        kGap);
    QCOMPARE(parseStamp(QStringLiteral("2015-03-29T03:30:00Z")).toSecsSinceEpoch(),
             kGap);
    QCOMPARE(
        parseStamp(QStringLiteral("2015-03-29T06:30:00+03:00")).toSecsSinceEpoch(),
        kGap);
    QCOMPARE(
        parseStamp(QStringLiteral("2015-03-29T00:30:00-03:00")).toSecsSinceEpoch(),
        kGap);
    QCOMPARE(parseStamp(QStringLiteral("2015-07-01T12:00:00")).toSecsSinceEpoch(),
             kJulyNoon);
    QVERIFY(!parseStamp(QString()).isValid());
    QVERIFY(!parseStamp(QStringLiteral("nonsense")).isValid());
    // The age of a stamp is counted from that instant.
    const QDateTime ref =
        QDateTime::fromSecsSinceEpoch(kGap + 90, QTimeZone::utc());
    const auto age = ageSeconds(QStringLiteral("2015-03-29T03:30:00"), ref);
    QVERIFY(age.has_value());
    QCOMPARE(*age, 90.0);
  }

  void theWindowInAnArchiveName() {
    const MemberWindow w = windowFromMember(
        QStringLiteral("TU_ELBA_29032015_033000_19042015_000000_HH.mseed"));
    QVERIFY(w.valid);
    QCOMPARE(w.station, QStringLiteral("ELBA"));
    QCOMPARE(w.start.toSecsSinceEpoch(), kGap);
    QCOMPARE(w.start.toUTC().time(), QTime(3, 30));
    QCOMPARE(w.end.toSecsSinceEpoch(), kMar29 + 21 * 86400);
    const MemberWindow july = windowFromMember(
        QStringLiteral("TU_ELBA_01072015_120000_22072015_000000_HH.mseed"));
    QCOMPARE(july.start.toSecsSinceEpoch(), kJulyNoon);
  }

  void plannedWindowsKeepTheirTimeOfDay() {
    const auto w = planWindows(QStringLiteral("2015-03-29T03:30:00"),
                               QStringLiteral("2015-03-31T03:30:00"), 1);
    QCOMPARE(w.size(), 2);
    QCOMPARE(w[0].first, QStringLiteral("2015-03-29T03:30:00"));
    QCOMPARE(w[0].second, QStringLiteral("2015-03-30T03:30:00"));
    QCOMPARE(w[1].second, QStringLiteral("2015-03-31T03:30:00"));
    // Across the night the clocks changed, a day is still 24 hours.
    const auto days = planWindows(QStringLiteral("2015-03-28"),
                                  QStringLiteral("2015-03-31"), 1);
    QCOMPARE(days.size(), 3);
    QCOMPARE(days[1].first, QStringLiteral("2015-03-29T00:00:00"));
    QCOMPARE(days[2].first, QStringLiteral("2015-03-30T00:00:00"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             planWindows(QStringLiteral("soon"),
                                         QStringLiteral("2015-03-31"), 1));
  }
};

int main(int argc, char **argv) {
  qputenv("TZ", "Europe/Istanbul");
#ifndef Q_OS_WIN
  tzset();
#endif
  QCoreApplication app(argc, argv);
  TstTdvmsTime test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_tdvms_time.moc"
