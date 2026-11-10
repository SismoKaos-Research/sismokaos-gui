// Studies: the study file, cases from catalog events, nearest stations and
// the store.
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>

#include "study/store.h"
#include "study/study.h"

using namespace skg;
using namespace skg::study;

namespace {

Study sample(const QString &dir) {
  Study s;
  s.dir = dir;
  s.name = QStringLiteral("Örnek \"çalışma\"");
  s.catalog = QStringLiteral("catalog.csv");
  s.catalogUtcOffsetHours = 3.0;
  s.waveRoots << WaveRoot{QStringLiteral("w1"), QStringLiteral("/data/mseed")};
  analysis::Event e;
  e.epoch = 1579888511; // 2020-01-24 17:55:11 UTC
  e.lat = 38.36;
  e.lon = 39.06;
  e.depthKm = 8.0;
  e.mag = 6.8;
  e.place = QStringLiteral("Sivrice (Elazığ)");
  Case c = caseFromEvent(e);
  c.id = s.freeCaseId(c.name);
  c.zone.kind = Zone::Kind::Fault;
  c.zone.strands = {QStringLiteral("main")};
  StationRef st;
  st.code = QStringLiteral("STA1");
  st.lat = 38.67;
  st.lon = 39.19;
  st.distanceKm = 36.25;
  st.azimuthDeg = 18.5;
  c.stations << st;
  s.cases << c;
  Case r = referenceCase(
      QStringLiteral("Quiet year"),
      QDateTime(QDate(2018, 6, 1), QTime(0, 0), QTimeZone::UTC), 38.0, 39.0);
  r.id = s.freeCaseId(r.name);
  r.spanDays = 180;
  s.cases << r;
  Recipe rec;
  rec.name = QStringLiteral("12 month window");
  rec.id = s.freeRecipeId(rec.name);
  rec.tool = QStringLiteral("indicators");
  rec.revision = 3;
  rec.fitted = {QStringLiteral("mc")};
  rec.settings = {
      {QStringLiteral("window_months"), 12},
      {QStringLiteral("mc"), 1.5},
      {QStringLiteral("bollinger"), true},
      {QStringLiteral("ma_windows"), QVariantList{7, 15, 23}},
      {QStringLiteral("note"), QStringLiteral("a \"quoted\"\nline")}};
  s.recipes << rec;
  return s;
}

} // namespace

class TstStudy : public QObject {
  Q_OBJECT
private slots:
  void slugs();
  void tomlRoundTrip();
  void badFilesSayWhy();
  void pathsAreRelativeInsideTheStudy();
  void nearestStationsAreSorted();
  void storeCreatesOpensEdits();
  void builtInStationsNearAPoint();
};

void TstStudy::slugs() {
  QCOMPARE(slug(QStringLiteral("Sivrice (Elazığ) 2020")),
           QStringLiteral("sivrice-elazig-2020"));
  QCOMPARE(slug(QStringLiteral("  ÇĞİÖŞÜ  ")), QStringLiteral("cgiosu"));
  QCOMPARE(slug(QStringLiteral("***")), QStringLiteral("item"));
  Study s;
  Case a;
  a.id = QStringLiteral("x");
  s.cases << a;
  QCOMPARE(s.freeCaseId(QStringLiteral("X")), QStringLiteral("x-2"));
}

void TstStudy::tomlRoundTrip() {
  QTemporaryDir dir;
  const Study a = sample(dir.path());
  const Study b = Study::fromToml(a.toToml(), dir.path());
  QCOMPARE(b.name, a.name);
  QCOMPARE(b.catalog, a.catalog);
  QCOMPARE(b.catalogUtcOffsetHours, 3.0);
  QCOMPARE(b.waveRoots.size(), 1);
  QCOMPARE(b.waveRoots[0].id, a.waveRoots[0].id);
  QCOMPARE(b.waveRoots[0].path, a.waveRoots[0].path);
  QCOMPARE(b.cases.size(), 2);
  const Case &c = b.cases[0];
  QCOMPARE(c.id, QStringLiteral("sivrice-elazig-2020"));
  QCOMPARE(c.name, a.cases[0].name);
  QVERIFY(!c.reference);
  QCOMPARE(c.time, a.cases[0].time);
  QCOMPARE(c.time.toString(Qt::ISODate),
           QStringLiteral("2020-01-24T17:55:11Z"));
  QCOMPARE(c.magnitude, 6.8);
  QCOMPARE(c.depthKm, 8.0);
  QCOMPARE(c.spanDays, 365);
  QVERIFY(c.zone.kind == Zone::Kind::Fault);
  QCOMPARE(c.zone.strands, QStringList{QStringLiteral("main")});
  QCOMPARE(c.stations.size(), 1);
  QCOMPARE(c.stations[0].code, QStringLiteral("STA1"));
  QCOMPARE(c.stations[0].distanceKm, 36.25);
  QCOMPARE(c.stations[0].azimuthDeg, 18.5);
  const Case &r = b.cases[1];
  QVERIFY(r.reference);
  QVERIFY(std::isnan(r.magnitude));
  QCOMPARE(r.spanDays, 180);
  QCOMPARE(r.spanStart().date(), QDate(2017, 12, 3));
  QCOMPARE(b.recipes.size(), 1);
  const Recipe &rec = b.recipes[0];
  QCOMPARE(rec.id, QStringLiteral("12-month-window"));
  QCOMPARE(rec.tool, QStringLiteral("indicators"));
  QCOMPARE(rec.revision, 3);
  QCOMPARE(rec.fitted, QStringList{QStringLiteral("mc")});
  QCOMPARE(rec.settings.value("window_months").toInt(), 12);
  QCOMPARE(rec.settings.value("mc").toDouble(), 1.5);
  QCOMPARE(rec.settings.value("bollinger").toBool(), true);
  QCOMPARE(rec.settings.value("ma_windows").toList().size(), 3);
  QCOMPARE(rec.settings.value("ma_windows").toList()[2].toInt(), 23);
  QCOMPARE(rec.settings.value("note").toString(),
           QStringLiteral("a \"quoted\"\nline"));
  // Writing what was read gives the same text.
  QCOMPARE(b.toToml(), a.toToml());
}

void TstStudy::badFilesSayWhy() {
  QTemporaryDir dir;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, Study::load(dir.path()));
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      Study::fromToml(QStringLiteral("name = "), dir.path()));
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      Study::fromToml(QStringLiteral("schema = 2\n"), dir.path()));
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      Study::fromToml(QStringLiteral("[[case]]\nid = \"a\"\ntime = \"x\"\n"),
                      dir.path()));
  try {
    Study::fromToml(QStringLiteral("a = 1\nb = = 2\n"), dir.path());
    QFAIL("no exception");
  } catch (const std::runtime_error &e) {
    QVERIFY(QString::fromUtf8(e.what()).contains(QStringLiteral("line 2")));
  }
}

void TstStudy::pathsAreRelativeInsideTheStudy() {
  QTemporaryDir dir;
  Study s;
  s.dir = dir.path();
  QCOMPARE(s.stored(dir.filePath("in/catalog.csv")),
           QStringLiteral("in/catalog.csv"));
  QCOMPARE(s.stored(QStringLiteral("/elsewhere/c.csv")),
           QStringLiteral("/elsewhere/c.csv"));
  QCOMPARE(s.resolved(QStringLiteral("in/catalog.csv")),
           dir.filePath("in/catalog.csv"));
  QVERIFY(s.stationListPath().startsWith(QLatin1Char(':')));
  StationRef st;
  st.code = QStringLiteral("STA1");
  QVERIFY(s.dataDirFor(st).isEmpty());
  QVERIFY(s.addWaveformRoot(dir.filePath("mseed")));
  QCOMPARE(s.dataDirFor(st), dir.filePath("mseed/STA1"));
  st.dataDir = QStringLiteral("/other/STA1");
  QCOMPARE(s.dataDirFor(st), QStringLiteral("/other/STA1"));
}

void TstStudy::nearestStationsAreSorted() {
  std::vector<analysis::Station> all;
  const double lats[] = {38.5, 39.5, 38.05, 45.0};
  for (int i = 0; i < 4; ++i) {
    analysis::Station s;
    s.code = QStringLiteral("S%1").arg(i);
    s.lat = lats[i];
    s.lon = 39.0;
    all.push_back(s);
  }
  const QList<StationRef> near = nearestStations(all, 38.0, 39.0, 200.0);
  QCOMPARE(near.size(), 3);
  QCOMPARE(near[0].code, QStringLiteral("S2"));
  QCOMPARE(near[1].code, QStringLiteral("S0"));
  QCOMPARE(near[2].code, QStringLiteral("S1"));
  QVERIFY(std::abs(near[1].distanceKm - 55.6) < 0.5);
  QVERIFY(std::abs(near[1].azimuthDeg) < 0.5); // due north
  QCOMPARE(nearestStations(all, 38.0, 39.0, 200.0, 1).size(), 1);
}

void TstStudy::storeCreatesOpensEdits() {
  QTemporaryDir dir;
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  const QString folder = dir.filePath("my study");
  {
    StudyStore store(&settings);
    QSignalSpy spy(&store, &StudyStore::changed);
    QVERIFY(!store.isOpen());
    QString error;
    QVERIFY2(store.create(folder, QStringLiteral("Mine"), &error),
             qPrintable(error));
    QVERIFY(store.isOpen());
    QCOMPARE(spy.count(), 1);
    QVERIFY(!store.create(folder, QStringLiteral("Again"), &error));
    QVERIFY(store.update([](Study &s) {
      Case c = referenceCase(
          QStringLiteral("Ref"),
          QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC), 38, 39);
      c.id = s.freeCaseId(c.name);
      s.cases << c;
    }));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(store.study().cases.size(), 1);
    QCOMPARE(store.recent(), QStringList{QDir(folder).absolutePath()});
  }
  StudyStore again(&settings);
  again.reopenLast();
  QVERIFY(again.isOpen());
  QCOMPARE(again.study().name, QStringLiteral("Mine"));
  QCOMPARE(again.study().cases.size(), 1);
  QString error;
  QVERIFY(!again.open(dir.filePath("nowhere"), &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(again.isOpen()); // the open study is kept
  again.close();
  QVERIFY(!again.isOpen());
}

void TstStudy::builtInStationsNearAPoint() {
  // The AFAD list shipped with tdvms, read from the source tree.
  const auto all = analysis::loadStations(
      QStringLiteral(SKG_TEST_DATA "/../../src/tdvms/data/stations.csv"));
  QVERIFY(all.size() > 100);
  const QList<StationRef> near = nearestStations(all, 38.1228, 38.5626, 200.0);
  QCOMPARE(near.size(), kStationCandidates); // more than that are in reach
  QVERIFY(near[0].distanceKm < 60.0);
  for (int i = 1; i < near.size(); ++i)
    QVERIFY(near[i - 1].distanceKm <= near[i].distanceKm);
}

QTEST_GUILESS_MAIN(TstStudy)
#include "tst_study.moc"
