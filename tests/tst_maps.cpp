// The shared map and its uses: the projection, what is under the pointer,
// labels that keep apart, what a set of stations covers, and the pages that
// carry a map (Cases, the add-case dialog, Compare, the coverage dialog and
// the Data page). With SKG_SHOTS set to a folder each use is also written
// there as a picture (SKG_SHOTS_THEME=dark for the dark scheme).
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>
#include <memory>

#include "analysis/geo.h"
#include "core/module.h"
#include "core/theme.h"
#include "jobs/jobmodel.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"
#include "modules/study/casespage.h"
#include "modules/study/comparepage.h"
#include "modules/study/coveragedialog.h"
#include "modules/tdvms/stationmap.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "study/store.h"
#include "studyui/coveragemap.h"
#include "studyui/geomap.h"
#include "tdvms/campaign.h"
#include "tdvms/catalog.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

template <class T> T *child(QWidget &w, const char *name) {
  return w.findChild<T *>(QLatin1String(name));
}

void shot(QWidget &w, const QString &name) {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  QCoreApplication::processEvents();
  w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

GeoMarker quake(const QString &id, double lat, double lon, double mag,
                const QString &label = {}) {
  GeoMarker m;
  m.kind = GeoMarker::Kind::Earthquake;
  m.id = id;
  m.label = label;
  m.lat = lat;
  m.lon = lon;
  m.magnitude = mag;
  return m;
}

int indexOf(const GeoMap &map, const QString &id) {
  const GeoContent &c = map.content();
  for (int i = 0; i < int(c.markers.size()); ++i)
    if (c.markers[i].id == id)
      return i;
  return -1;
}

void clickMarker(GeoMap &map, const QString &id) {
  const int i = indexOf(map, id);
  QVERIFY2(i >= 0, qPrintable(id));
  const QPoint at = map.markerPx(i).toPoint();
  QVERIFY2(map.rect().contains(at), qPrintable(id + " is outside the map"));
  QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, at);
}

StationRef stationAt(const QString &code, double lat, double lon,
                     const Case &c) {
  StationRef st;
  st.code = code;
  st.lat = lat;
  st.lon = lon;
  st.distanceKm = analysis::haversineKm(c.lat, c.lon, lat, lon);
  return st;
}

Case caseAt(const QString &id, const QString &name, const QString &time,
            double lat, double lon, double mag) {
  Case c;
  c.id = id;
  c.name = name;
  c.time = QDateTime::fromString(time, Qt::ISODate);
  c.lat = lat;
  c.lon = lon;
  c.magnitude = mag;
  c.depthKm = 10;
  c.zone.radiusKm = 100.0;
  return c;
}

} // namespace

class TstMaps : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();
  void projectionRoundTrip();
  void theMarkerUnderThePointer();
  void coLocatedCasesKeepApart();
  void whatStationsCover();
  void coverageMapCounts();
  void manyPointsStayQuick();
  void casesPageAndMapSelectEachOther();
  void addDialogPreview();
  void previewFlagsAStationOutsideTheZone();
  void faultZoneCasesShowTheirTraces();
  void compareShowsWhere();
  void coverageDialogOfTheStudy();
  void dataPageMapChoosesStations();

private:
  bool makeStudy();

  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  std::unique_ptr<Navigator> nav_;
  AppContext ctx_;
};

void TstMaps::init() {
  initResources();
  dir_ = std::make_unique<QTemporaryDir>();
  settings_ = std::make_unique<QSettings>(dir_->filePath("s.ini"),
                                          QSettings::IniFormat);
  settings_->setValue(QStringLiteral("tdvms/root"), dir_->filePath("dl"));
  theme_ = std::make_unique<Theme>(settings_.get());
  theme_->setMode(qEnvironmentVariable("SKG_SHOTS_THEME") ==
                          QLatin1String("dark")
                      ? Theme::Mode::Dark
                      : Theme::Mode::Light);
  theme_->apply();
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  nav_ = std::make_unique<Navigator>();
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get(), nav_.get()};
}

void TstMaps::cleanup() {
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 20000);
  store_.reset();
  jobs_.reset();
}

// Three earthquakes and a reference date that shares the place of the
// first, with stations around them.
bool TstMaps::makeStudy() {
  const QString folder = dir_->filePath("study");
  if (!store_->create(folder, QStringLiteral("Synthetic demo")))
    return false;
  if (!QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                   QDir(folder).filePath("catalog.csv")))
    return false;
  return store_->update([](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    Case a = caseAt(QStringLiteral("a"), QStringLiteral("Synthetic A"),
                    QStringLiteral("2019-04-02T14:00:00Z"), 38.12, 38.56, 6.4);
    a.stations << stationAt(QStringLiteral("STA1"), 38.40, 38.85, a)
               << stationAt(QStringLiteral("STA2"), 37.95, 37.80, a);
    Case b = caseAt(QStringLiteral("b"), QStringLiteral("Synthetic B"),
                    QStringLiteral("2019-12-30T22:10:00Z"), 39.47, 35.35, 6.0);
    b.stations << stationAt(QStringLiteral("STA3"), 39.60, 35.10, b);
    Case c = caseAt(QStringLiteral("c"), QStringLiteral("Synthetic C"),
                    QStringLiteral("2020-07-07T07:07:07Z"), 38.40, 29.10, 5.6);
    c.stations << stationAt(QStringLiteral("STA4"), 38.10, 28.80, c);
    Case r = caseAt(QStringLiteral("r"), QStringLiteral("Reference R1"),
                    QStringLiteral("2018-06-01T00:00:00Z"), 38.12, 38.56, 0);
    r.reference = true;
    r.magnitude = std::numeric_limits<double>::quiet_NaN();
    r.stations << stationAt(QStringLiteral("STA1"), 38.40, 38.85, r);
    s.cases << a << b << c << r;
  });
}

void TstMaps::projectionRoundTrip() {
  GeoMap map(theme_.get());
  map.resize(640, 420);
  map.setView(38.5, 36.0, 1200.0);
  QCOMPARE(map.widthKm(), 1200.0);
  // The centre is the centre; a point comes back from its pixel.
  QCOMPARE(map.toPx(38.5, 36.0), QPointF(320, 210));
  for (const QPointF &ll : {QPointF(36.2, 30.1), QPointF(41.0, 43.7),
                            QPointF(38.5, 36.0)}) {
    const QPointF back = map.toLatLon(map.toPx(ll.x(), ll.y()));
    QVERIFY(std::abs(back.x() - ll.x()) < 1e-9);
    QVERIFY(std::abs(back.y() - ll.y()) < 1e-9);
  }
  // North is up, east is right; a degree of latitude is about 110.6 km.
  QVERIFY(map.toPx(39.5, 36.0).y() < map.toPx(38.5, 36.0).y());
  QVERIFY(map.toPx(38.5, 37.0).x() > map.toPx(38.5, 36.0).x());
  const double pxPerKm = 640.0 / 1200.0;
  QVERIFY(std::abs((map.toPx(38.5, 36.0).y() - map.toPx(39.5, 36.0).y()) -
                   110.574 * pxPerKm) < 0.01);
  // ... and a degree of longitude shrinks with the cosine of the latitude.
  const double east = map.toPx(38.5, 37.0).x() - map.toPx(38.5, 36.0).x();
  QVERIFY(std::abs(east - 111.320 * std::cos(38.5 * M_PI / 180) * pxPerKm) <
          0.01);
  // Fitting brings the content into view.
  GeoContent c;
  c.markers << quake(QStringLiteral("x"), 40.9, 29.0, 6.0)
            << quake(QStringLiteral("y"), 37.0, 42.5, 6.0);
  map.setContent(c, true);
  for (int i = 0; i < 2; ++i)
    QVERIFY(QRectF(map.rect()).adjusted(20, 20, -20, -20).contains(
        map.markerPx(i)));
}

void TstMaps::theMarkerUnderThePointer() {
  GeoMap map(theme_.get());
  map.resize(600, 400);
  map.show();
  QVERIFY(QTest::qWaitForWindowExposed(&map));
  GeoContent c;
  c.markers << quake(QStringLiteral("big"), 38.0, 35.0, 7.0)
            << quake(QStringLiteral("far"), 39.0, 38.0, 5.0);
  GeoMarker dot;
  dot.kind = GeoMarker::Kind::Dot;
  dot.id = QStringLiteral("dot");
  dot.lat = 38.0;
  dot.lon = 35.0; // under the big one
  c.markers << dot;
  GeoMarker lone = dot;
  lone.id = QStringLiteral("lone");
  lone.lat = 37.2;
  lone.lon = 36.5;
  c.markers << lone;
  map.setContent(c, true);
  // A marker wins over the faint dot it covers; the dot alone is found too.
  QCOMPARE(map.markerAt(map.markerPx(0)), QStringLiteral("big"));
  QCOMPARE(map.markerAt(map.markerPx(1)), QStringLiteral("far"));
  QCOMPARE(map.markerAt(map.markerPx(3) + QPointF(3, 2)),
           QStringLiteral("lone"));
  QCOMPARE(map.markerAt(QPointF(5, 5)), QString());
  // A click reports the marker; a drag moves the map and reports nothing.
  QSignalSpy clicked(&map, &GeoMap::markerClicked);
  QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier,
                    map.markerPx(1).toPoint());
  QCOMPARE(clicked.size(), 1);
  QCOMPARE(clicked.at(0).at(0).toString(), QStringLiteral("far"));
  const QPointF before = map.toLatLon(QPointF(300, 200));
  const QPoint from = map.markerPx(1).toPoint();
  QTest::mousePress(&map, Qt::LeftButton, Qt::NoModifier, from);
  QTest::mouseMove(&map, from + QPoint(40, 25));
  QTest::mouseMove(&map, from + QPoint(80, 50));
  QTest::mouseRelease(&map, Qt::LeftButton, Qt::NoModifier,
                      from + QPoint(80, 50));
  QCOMPARE(clicked.size(), 1);
  const QPointF after = map.toLatLon(QPointF(300, 200));
  QVERIFY(after.y() < before.y()); // dragged east: the view went west
  QVERIFY(after.x() > before.x()); // dragged south: the view went north
}

void TstMaps::coLocatedCasesKeepApart() {
  // Two earthquakes and a reference date at one place, and a fourth case a
  // few kilometres off: the crowding of the mock picture.
  GeoMap map(theme_.get());
  map.resize(560, 400);
  GeoContent c;
  for (int i = 0; i < 3; ++i) {
    GeoMarker m = quake(QStringLiteral("q%1").arg(i), 38.12, 38.56, 6.4,
                        QStringLiteral("Earthquake %1 · M 6.4").arg(i));
    if (i == 2)
      m.kind = GeoMarker::Kind::Reference;
    m.strong = i == 0;
    c.markers << m;
  }
  c.markers << quake(QStringLiteral("near"), 38.15, 38.60, 6.0,
                     QStringLiteral("Earthquake near · M 6.0"));
  GeoMarker st;
  st.kind = GeoMarker::Kind::Station;
  st.label = QStringLiteral("STA1");
  st.lat = 38.13;
  st.lon = 38.58;
  c.markers << st;
  map.setContent(c, true);
  map.setView(38.5, 36.0, 1500.0); // far out: everything lands on a few pixels

  // Every label is drawn, inside the map, and none covers another.
  const QList<QRectF> labels = map.labelRects();
  QCOMPARE(labels.size(), 5);
  for (int i = 0; i < labels.size(); ++i) {
    QVERIFY(QRectF(map.rect()).contains(labels[i]));
    for (int j = i + 1; j < labels.size(); ++j)
      QVERIFY2(!labels[i].intersects(labels[j]),
               qPrintable(QStringLiteral("labels %1 and %2").arg(i).arg(j)));
  }
  // The case markers were moved apart, so each can be seen and clicked.
  for (int i = 0; i < 4; ++i)
    for (int j = i + 1; j < 4; ++j)
      QVERIFY(QLineF(map.markerPx(i), map.markerPx(j)).length() > 6.0);
  for (int i = 0; i < 4; ++i)
    QCOMPARE(map.markerAt(map.markerPx(i)), c.markers[i].id);
  map.show();
  QVERIFY(QTest::qWaitForWindowExposed(&map));
  shot(map, QStringLiteral("co_located"));
}

void TstMaps::whatStationsCover() {
  std::vector<analysis::Event> events;
  const auto add = [&events](double lat, double lon, double mag) {
    analysis::Event e;
    e.lat = lat;
    e.lon = lon;
    e.mag = mag;
    events.push_back(e);
  };
  add(38.0, 38.0, 6.0);  // at the first station
  add(38.0, 39.0, 5.5);  // about 88 km east of it
  add(38.0, 40.0, 5.5);  // about 176 km east: outside 150 km
  add(41.0, 30.0, 6.2);  // about 55 km from the second station
  add(38.05, 38.0, 4.0); // near, but below the magnitude asked for
  const QList<CoverageStation> stations = {
      {QStringLiteral("A"), 38.0, 38.0}, {QStringLiteral("B"), 40.5, 30.0}};
  const CoverageResult r = stationCoverage(events, stations, 150.0, 5.0);
  QCOMPARE(r.considered(), 4);
  QCOMPARE(r.covered, (std::vector<int>{0, 1, 3}));
  QCOMPARE(r.missed, (std::vector<int>{2}));
  // The radius is the rule: 100 km still holds the one 88 km away, 80 not.
  QCOMPARE(stationCoverage(events, stations, 100.0, 5.0).covered.size(),
           size_t(3));
  QCOMPARE(stationCoverage(events, stations, 80.0, 5.0).covered,
           (std::vector<int>{0, 3}));
  // No station covers nothing; a lower magnitude counts the small one.
  QCOMPARE(stationCoverage(events, {}, 150.0, 5.0).covered.size(), size_t(0));
  QCOMPARE(stationCoverage(events, stations, 150.0, 3.0).considered(), 5);
}

void TstMaps::coverageMapCounts() {
  CoverageMap w(theme_.get());
  w.resize(760, 620);
  std::vector<analysis::Event> events;
  for (int i = 0; i < 8; ++i) {
    analysis::Event e;
    e.lat = 38.0;
    e.lon = 37.0 + i * 0.5; // 0, 44, 88, ... km east of the station
    e.mag = 5.0 + 0.2 * i;
    e.place = QStringLiteral("Synthetic");
    events.push_back(e);
  }
  w.setEvents(events, QStringLiteral("a test list"));
  w.setStations({{QStringLiteral("STA1"), 38.0, 37.0}});
  w.setRadiusKm(100.0);
  w.setMinMagnitude(5.0);
  QCOMPARE(w.result().covered.size(), size_t(3)); // 0, 44 and 88 km
  QVERIFY(w.summary().startsWith(QStringLiteral("3 of 8 ")));
  QVERIFY(w.summary().contains(QStringLiteral("100 km")));
  QVERIFY(w.summary().contains(QStringLiteral("a test list")));
  auto *radius = child<QDoubleSpinBox>(w, "coverageRadius");
  QVERIFY(radius);
  radius->setValue(200.0);
  QCOMPARE(w.result().covered.size(), size_t(5));
  QVERIFY(w.summary().startsWith(QStringLiteral("5 of 8 ")));
  // The disc, the station and every earthquake are on the map; the ones
  // outside are rings, not told by colour alone.
  const GeoContent &c = w.map()->content();
  QCOMPARE(c.coverage.size(), 1);
  QCOMPARE(c.coverage[0].radiusKm, 200.0);
  int hollow = 0, filled = 0;
  for (const GeoMarker &m : c.markers)
    if (m.kind == GeoMarker::Kind::Earthquake)
      (m.hollow ? hollow : filled) += 1;
  QCOMPARE(filled, 5);
  QCOMPARE(hollow, 3);
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  shot(w, QStringLiteral("coverage_widget"));
}

void TstMaps::manyPointsStayQuick() {
  // The portal's 1,500 stations and a few thousand earthquakes: a repaint
  // after a pan has to stay well within a frame or two.
  GeoMap map(theme_.get());
  map.resize(900, 650);
  GeoContent c;
  for (int i = 0; i < 1500; ++i) {
    GeoMarker m;
    m.kind = GeoMarker::Kind::Dot;
    m.id = QStringLiteral("s%1").arg(i);
    m.lat = 36.0 + (i % 50) * 0.12;
    m.lon = 26.0 + (i / 50) * 0.6;
    c.markers << m;
  }
  for (int i = 0; i < 4000; ++i)
    c.markers << quake(QString(), 35.5 + (i % 80) * 0.09, 25.5 + (i / 80) * 0.4,
                       4.0 + (i % 30) * 0.1);
  map.setContent(c, true);
  map.show();
  QVERIFY(QTest::qWaitForWindowExposed(&map));
  QElapsedTimer t;
  t.start();
  const int frames = 10;
  for (int i = 0; i < frames; ++i) {
    map.setView(38.5 + 0.05 * i, 35.0 + 0.1 * i, 1400.0);
    map.grab(); // the basemap and every marker again
  }
  const double perFrame = double(t.elapsed()) / frames;
  qInfo("5,500 markers: %.1f ms per repaint after a pan", perFrame);
  QVERIFY2(perFrame < 250.0, "a repaint takes too long to pan with");
  // Finding what is under the pointer is cheap as well.
  t.restart();
  for (int i = 0; i < 200; ++i)
    map.markerAt(QPointF(50 + i * 4, 60 + i * 2));
  QVERIFY(t.elapsed() < 2000);
}

void TstMaps::casesPageAndMapSelectEachOther() {
  QVERIFY(makeStudy());
  CasesPage page(ctx_);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *table = child<QTableWidget>(page, "caseTable");
  auto *stations = child<QTableWidget>(page, "stationTable");
  auto *map = child<GeoMap>(page, "caseGeoMap");
  QVERIFY(table && stations && map);
  QCOMPARE(table->rowCount(), 4);

  const auto strongCase = [map] {
    QStringList ids;
    for (const GeoMarker &m : map->content().markers)
      if (m.strong && m.id.startsWith(QLatin1String("case:")))
        ids << m.id.mid(5);
    return ids;
  };
  // The table selects on the map: the selected case is the one in colour,
  // with its zone filled and its stations named.
  table->selectRow(1);
  QCOMPARE(strongCase(), QStringList{QStringLiteral("b")});
  int named = 0;
  for (const GeoMarker &m : map->content().markers)
    if (m.kind == GeoMarker::Kind::Station && !m.label.isEmpty()) {
      QCOMPARE(m.label, QStringLiteral("STA3"));
      ++named;
    }
  QCOMPARE(named, 1);
  int filled = 0;
  for (const GeoZone &z : map->content().zones)
    filled += z.filled && !z.dotted ? 1 : 0;
  QCOMPARE(filled, 1);
  // Every case is on the map, the others as context.
  int cases = 0;
  for (const GeoMarker &m : map->content().markers)
    cases += m.id.startsWith(QLatin1String("case:")) ? 1 : 0;
  QCOMPARE(cases, 4);

  // The map selects in the table.
  clickMarker(*map, QStringLiteral("case:c"));
  QCOMPARE(table->currentRow(), 2);
  QCOMPARE(strongCase(), QStringList{QStringLiteral("c")});
  // The reference date shares the place of case a; both can be clicked.
  clickMarker(*map, QStringLiteral("case:r"));
  QCOMPARE(table->currentRow(), 3);
  clickMarker(*map, QStringLiteral("case:a"));
  QCOMPARE(table->currentRow(), 0);
  // A station on the map selects its row in the stations table.
  clickMarker(*map, QStringLiteral("station:STA2"));
  QVERIFY(stations->currentRow() >= 0);
  QCOMPARE(stations->item(stations->currentRow(), 1)->text(),
           QStringLiteral("STA2"));
  shot(page, QStringLiteral("cases"));
  // "This case" zooms to the selected case; "All cases" back out.
  auto *one = child<QPushButton>(page, "mapFitCase");
  auto *all = child<QPushButton>(page, "mapFitAll");
  QVERIFY(one && all);
  const double wide = map->widthKm();
  one->click();
  QVERIFY(map->widthKm() < wide / 2);
  shot(page, QStringLiteral("cases_this_case"));
  all->click();
  QVERIFY(std::abs(map->widthKm() - wide) < wide * 0.05);
}

void TstMaps::addDialogPreview() {
  QVERIFY(makeStudy());
  AddFromCatalogDialog dialog(store_->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = child<QTableWidget>(dialog, "eventTable");
  auto *stations = child<QTableWidget>(dialog, "stationTable");
  auto *map = child<GeoMap>(dialog, "previewGeoMap");
  QVERIFY(events && stations && map);
  // Nothing chosen: the earthquakes of the list are on the map.
  events->clearSelection();
  QCoreApplication::processEvents();
  int listed = 0;
  for (const GeoMarker &m : map->content().markers)
    listed += m.id.startsWith(QLatin1String("event:")) ? 1 : 0;
  QCOMPARE(listed, events->rowCount());
  QVERIFY(listed > 1);
  shot(dialog, QStringLiteral("add_dialog_list"));
  // Limiting the list to earthquakes near a station draws what it covers.
  auto *codes = child<QLineEdit>(dialog, "nearStations");
  auto *km = child<QDoubleSpinBox>(dialog, "nearKm");
  QVERIFY(codes && km);
  events->selectRow(0);
  QVERIFY(stations->rowCount() > 0);
  const QString code = stations->item(0, 1)->text();
  codes->setText(code);
  km->setValue(80);
  QCOMPARE(map->content().coverage.size(), 1);
  QCOMPARE(map->content().coverage[0].radiusKm, 80.0);
  shot(dialog, QStringLiteral("add_dialog_near_stations"));
  codes->clear();
  QVERIFY(map->content().coverage.isEmpty());

  // A click on an earthquake of the map selects its row.
  clickMarker(*map, QStringLiteral("event:1"));
  QCOMPARE(events->currentRow(), 1);
  // Now the map is on that earthquake: epicentre, zone, candidate stations.
  QCOMPARE(map->content().zones.size(), 1);
  int strongStations = 0, candidates = 0;
  QString aCandidate;
  for (const GeoMarker &m : map->content().markers)
    if (m.kind == GeoMarker::Kind::Station) {
      if (m.strong)
        ++strongStations;
      else {
        ++candidates;
        aCandidate = m.id;
      }
    }
  int ticked = 0;
  for (int i = 0; i < stations->rowCount(); ++i)
    ticked += stations->item(i, 0)->checkState() == Qt::Checked ? 1 : 0;
  QCOMPARE(strongStations, ticked);
  QVERIFY(candidates > 0);
  // A click on a candidate ticks it, in the table as on the map.
  map->setView(map->toLatLon(map->markerPx(indexOf(*map, aCandidate))).x(),
               map->toLatLon(map->markerPx(indexOf(*map, aCandidate))).y(),
               120.0);
  clickMarker(*map, aCandidate);
  int tickedAfter = 0;
  for (int i = 0; i < stations->rowCount(); ++i)
    tickedAfter += stations->item(i, 0)->checkState() == Qt::Checked ? 1 : 0;
  QCOMPARE(tickedAfter, ticked + 1);
  shot(dialog, QStringLiteral("add_dialog_case"));
}

void TstMaps::previewFlagsAStationOutsideTheZone() {
  CasePreview preview;
  preview.resize(520, 600);
  Case c = caseAt(QStringLiteral("a"), QStringLiteral("A"),
                  QStringLiteral("2021-03-14T09:20:00Z"), 37.75, 37.50, 6.4);
  QList<StationRef> near;
  near << stationAt(QStringLiteral("STA1"), 37.95, 37.85, c)   // ~38 km
       << stationAt(QStringLiteral("STA5"), 37.00, 38.05, c)   // ~96 km
       << stationAt(QStringLiteral("STA6"), 38.85, 38.05, c);  // ~131 km
  QVERIFY(near[2].distanceKm > 100.0 && near[1].distanceKm < 100.0);
  preview.setCase(c, near, {QStringLiteral("STA1"), QStringLiteral("STA6")},
                  [](const StationRef &s) {
                    return s.code == QLatin1String("STA5");
                  });
  QVERIFY(preview.warning().contains(QStringLiteral("STA6")));
  QVERIFY(!preview.warning().contains(QStringLiteral("STA1")));
  QVERIFY(preview.warning().contains(QStringLiteral("100 km")));
  const GeoContent &content = preview.map()->content();
  QHash<QString, GeoMarker::State> state;
  for (const GeoMarker &m : content.markers)
    if (m.kind == GeoMarker::Kind::Station)
      state.insert(m.id, m.state);
  QCOMPARE(state.value(QStringLiteral("station:STA1")),
           GeoMarker::State::Used);
  QCOMPARE(state.value(QStringLiteral("station:STA5")),
           GeoMarker::State::HasData);
  QCOMPARE(state.value(QStringLiteral("station:STA6")),
           GeoMarker::State::Outside);
  // With the station unticked the warning goes.
  preview.setCase(c, near, {QStringLiteral("STA1")}, {});
  QVERIFY(preview.warning().isEmpty());
  preview.setCase(c, near, {QStringLiteral("STA1"), QStringLiteral("STA6")},
                  [](const StationRef &s) {
                    return s.code == QLatin1String("STA5");
                  });
  preview.show();
  QVERIFY(QTest::qWaitForWindowExposed(&preview));
  shot(preview, QStringLiteral("preview_outside_zone"));
}

void TstMaps::faultZoneCasesShowTheirTraces() {
  QVERIFY(makeStudy());
  QVERIFY(store_->update([](Study &s) {
    s.cases[0].zone.kind = Zone::Kind::Fault;
    s.cases[1].zone.kind = Zone::Kind::Fault;
    s.cases[1].zone.faultGeoJson = QStringLiteral("no-such-file.geojson");
  }));
  const Study &s = store_->study();
  // The selected case's zone is the shipped East Anatolian traces: drawn in
  // its colour, and no circle for it.
  CaseMapSpec spec;
  spec.selectedId = QStringLiteral("a");
  QStringList problems;
  GeoContent c = caseMapContent(s, *theme_, spec, &problems);
  QCOMPARE(c.traces.size(), 1);
  QVERIFY(c.traces[0].segments.size() > 10);
  QCOMPARE(c.traces[0].color, theme_->seriesColors()[0]);
  QCOMPARE(c.zones.size(), 2); // the circles of the two other circle cases
  QVERIFY(problems.isEmpty());
  // A fault file that cannot be read is named, not left out silently.
  spec.selectedId = QStringLiteral("b");
  c = caseMapContent(s, *theme_, spec, &problems);
  QVERIFY(c.traces.isEmpty());
  QCOMPARE(problems.size(), 1);
  CaseMap map(theme_.get());
  map.setStudy(s, spec);
  bool said = false;
  for (const QLabel *l : map.findChildren<QLabel *>())
    said = said || l->text().contains(QStringLiteral("could not be drawn"));
  QVERIFY(said);
}

void TstMaps::compareShowsWhere() {
  QVERIFY(makeStudy());
  ComparePage page(ctx_);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 20000);
  auto *map = child<GeoMap>(page, "whereMap");
  auto *toggle = child<QToolButton>(page, "whereToggle");
  QVERIFY(map && toggle);
  // Every case, in the colour of its curve (the order of the cases), named;
  // no stations.
  const QList<QColor> colors = theme_->seriesColors();
  const GeoContent &c = map->content();
  QCOMPARE(c.markers.size(), 4);
  const Study &s = store_->study();
  for (const GeoMarker &m : c.markers) {
    QVERIFY(m.id.startsWith(QLatin1String("case:")));
    int at = -1;
    for (int i = 0; i < int(s.cases.size()); ++i)
      if (s.cases[i].id == m.id.mid(5))
        at = i;
    QVERIFY(at >= 0);
    QCOMPARE(m.color, colors[at]);
    QCOMPARE(m.label, s.cases[at].name);
    QCOMPARE(m.kind, s.cases[at].reference ? GeoMarker::Kind::Reference
                                           : GeoMarker::Kind::Earthquake);
  }
  shot(page, QStringLiteral("compare_where"));
  // The panel folds away and remembers it.
  QVERIFY(map->isVisible());
  toggle->click();
  QVERIFY(!map->isVisible());
  QCOMPARE(settings_->value(QStringLiteral("compare/whereOpen")).toBool(),
           false);
  ComparePage again(ctx_);
  QVERIFY(child<GeoMap>(again, "whereMap")->isHidden());
  toggle->click();
  QVERIFY(map->isVisible());
}

void TstMaps::coverageDialogOfTheStudy() {
  QVERIFY(makeStudy());
  // No waveform folder yet: the dialog starts on the stations of the cases.
  CoverageDialog dialog(ctx_);
  auto *which = child<QComboBox>(dialog, "coverageWhich");
  QVERIFY(which);
  QCOMPARE(which->currentIndex(), 1);
  QCOMPARE(dialog.coverage()->map()->content().coverage.size(), 4);
  QVERIFY(dialog.coverage()->summary().contains(
      QStringLiteral("the study's catalog")));
  QVERIFY(dialog.coverage()->result().considered() > 0);
  QVERIFY(!dialog.coverage()->result().covered.empty());
  // Every listed earthquake is one or the other.
  const std::vector<analysis::Event> events =
      coverageEvents(store_->study(), nullptr);
  int large = 0;
  for (const analysis::Event &e : events)
    large += e.mag + 1e-9 >= 5.0 ? 1 : 0;
  QCOMPARE(dialog.coverage()->result().considered(), large);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  shot(dialog, QStringLiteral("coverage_dialog"));
  // Without a catalog the bundled list of larger earthquakes is counted.
  QVERIFY(store_->update([](Study &s) { s.catalog.clear(); }));
  QString source;
  QVERIFY(coverageEvents(store_->study(), &source).size() > 100);
  QVERIFY(source.contains(QStringLiteral("USGS")));
}

void TstMaps::dataPageMapChoosesStations() {
  const tdvms::Campaign camp =
      tdvms::newCampaign(dir_->filePath("dl"), QStringLiteral("c1"), QString());
  QFile ledger(camp.ledgerPath);
  QVERIFY(ledger.open(QIODevice::WriteOnly));
  ledger.close();
  TdvmsWidget page(ctx_);
  page.resize(1360, 860);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *tabs = page.findChild<QTabWidget *>();
  auto *pick = child<StationPickMap>(page, "stationMap");
  auto *map = child<GeoMap>(page, "stationGeoMap");
  auto *station = child<QLineEdit>(page, "fdsnStation");
  auto *which = child<QComboBox>(page, "wantedStation");
  auto *bar = child<QWidget>(page, "wantedBar");
  QVERIFY(tabs && pick && map && station && which && bar);
  tabs->setCurrentWidget(pick);
  QCoreApplication::processEvents();
  // Every station of the portal is on the map, none chosen.
  const QList<tdvms::CatalogStation> all = tdvms::loadStations();
  QVERIFY(all.size() > 1000);
  QCOMPARE(map->content().markers.size(), all.size());
  QVERIFY(pick->selected().isEmpty());
  shot(*tabs, QStringLiteral("data_map_network")); // not the run box: it names a mailbox

  // A click on a station chooses it: the form is on it, the bar lists it.
  const QString first = all[all.size() / 2].code;
  map->setView(all[all.size() / 2].latitude, all[all.size() / 2].longitude,
               30.0);
  clickMarker(*map, QStringLiteral("station:") + first);
  QCOMPARE(pick->selected(), QStringList{first});
  QCOMPARE(station->text(), first);
  QVERIFY(!bar->isHidden());
  QCOMPARE(which->count(), 1);
  QCOMPARE(map->content().markers[indexOf(*map, QStringLiteral("station:") +
                                                    first)]
               .kind,
           GeoMarker::Kind::Station);
  // A second click takes it out again, and the form with it.
  clickMarker(*map, QStringLiteral("station:") + first);
  QVERIFY(pick->selected().isEmpty());
  QVERIFY(station->text().isEmpty());
  QVERIFY(bar->isHidden());

  // A station typed into the FDSN form shows as chosen too.
  station->setText(first);
  QCOMPARE(pick->selected(), QStringList{first});
  station->clear();

  // Opened for a case: its stations are chosen, its epicentre and zone are
  // drawn; a station the portal does not list is named, not dropped.
  const tdvms::CatalogStation &sa = all[10], &sb = all[11];
  nav_->show(QStringLiteral("tdvms"),
             {{QStringLiteral("stations"),
               QStringList{sa.code, sb.code, QStringLiteral("NOPE9")}},
              {QStringLiteral("start"), QStringLiteral("2019-01-01T14:00:00Z")},
              {QStringLiteral("end"), QStringLiteral("2019-04-02T14:00:00Z")},
              {QStringLiteral("case"), QStringLiteral("Case A")},
              {QStringLiteral("lat"), sa.latitude + 0.3},
              {QStringLiteral("lon"), sa.longitude + 0.3},
              {QStringLiteral("zone_km"), 100.0}});
  QCOMPARE(pick->selected().size(), 3);
  QCOMPARE(pick->unknown(), QStringList{QStringLiteral("NOPE9")});
  QVERIFY(child<QLabel>(page, "stationMapSummary")
              ->text()
              .contains(QStringLiteral("NOPE9")));
  QCOMPARE(map->content().zones.size(), 1);
  QCOMPARE(map->content().zones[0].radiusKm, 100.0);
  bool hasCase = false;
  for (const GeoMarker &m : map->content().markers)
    hasCase = hasCase || (m.kind == GeoMarker::Kind::Earthquake &&
                          m.label == QLatin1String("Case A"));
  QVERIFY(hasCase);
  // The chosen stations and the case are in view.
  QVERIFY(QRectF(map->rect()).contains(
      map->markerPx(indexOf(*map, QStringLiteral("station:") + sa.code))));
  shot(*tabs, QStringLiteral("data_map_case")); // not the run box: it names a mailbox
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  TstMaps t;
  return QTest::qExec(&t, argc, argv);
}
#include "tst_maps.moc"
