// The regions port: the catalog kept as text, the self-organising map,
// Mean Shift and its bandwidth, the two regionalisations, the region files,
// the map layers and the map itself, and a whole run with the script's
// file names.
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <set>

#include "analysis/plot.h"
#include "analysis/regionmap.h"
#include "analysis/regionrun.h"
#include "analysis/regions.h"

using namespace skg::analysis;

namespace {
const QString kCatalog =
    QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv");

// Three tight groups of 40 points around the given centres.
std::vector<Point2> blobs() {
  const Point2 centers[3] = {{37.0, 36.0}, {38.0, 39.0}, {39.5, 41.0}};
  std::vector<Point2> x;
  for (int b = 0; b < 3; ++b)
    for (int i = 0; i < 40 + 20 * b; ++i) {
      // A fixed spiral: no random numbers in the fixture.
      const double a = 0.7 * i, r = 0.004 * i;
      x.push_back(
          {centers[b][0] + r * std::cos(a), centers[b][1] + r * std::sin(a)});
    }
  return x;
}

void writeText(const QString &path, const QByteArray &text) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(text);
}

QByteArray readAll(const QString &path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

class TstAnalysisRegions : public QObject {
  Q_OBJECT
private slots:
  void pointTableKeepsTheLines();
  void pointTableNeedsCoordinates();
  void somIsReproducible();
  void somUnitsFollowTheData();
  void bandwidthIsTheMeanNeighbourDistance();
  void meanShiftFindsTheGroups();
  void meanShiftMergesCloseCentres();
  void linearMapRegions();
  void hybridRegions();
  void regionFiles();
  void citiesAndLayers();
  void mapDraws();
  void wholeRun();
  void runRefusesNothingToDo();
};

void TstAnalysisRegions::pointTableKeepsTheLines() {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("c.csv"));
  writeText(path, "\xEF\xBB\xBFTarih,Enlem,Boylam,Not\r\n"
                  "2020-01-01,38.5,39.25,\"a, b\"\r\n"
                  "2020-01-02,,39.0,no latitude\r\n"
                  "\r\n"
                  "2020-01-03,\"37,75\",40.5,decimal comma\r\n");
  const PointTable t = loadPointTable(path);
  QCOMPARE(t.headerLine, QStringLiteral("Tarih,Enlem,Boylam,Not"));
  QCOMPARE(t.latCol, 1);
  QCOMPARE(t.lonCol, 2);
  QCOMPARE(t.size(), 2);
  QCOMPARE(t.rows.size(), 2);
  QCOMPARE(t.rows[0], QStringLiteral("2020-01-01,38.5,39.25,\"a, b\""));
  QCOMPARE(t.points[0][0], 38.5);
  QCOMPARE(t.points[0][1], 39.25);
  QCOMPARE(t.points[1][0], 37.75);

  const PointTable synthetic = loadPointTable(kCatalog);
  QCOMPARE(synthetic.size(), 2506);
  QCOMPARE(synthetic.header[synthetic.latCol], QStringLiteral("Latitude"));
  QCOMPARE(synthetic.header[synthetic.lonCol], QStringLiteral("Longitude"));
  QCOMPARE(synthetic.points[0][0], 38.5384);
  QCOMPARE(synthetic.points[0][1], 39.3279);
}

void TstAnalysisRegions::pointTableNeedsCoordinates() {
  QTemporaryDir dir;
  const QString path = dir.filePath(QStringLiteral("c.csv"));
  writeText(path, "time,magnitude\n2020-01-01,3.0\n");
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadPointTable(path));
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      loadPointTable(dir.filePath(QStringLiteral("missing.csv"))));
}

void TstAnalysisRegions::somIsReproducible() {
  const std::vector<Point2> x = blobs();
  SomOptions o;
  o.cols = 4;
  const Som a = trainSom(x, o), b = trainSom(x, o);
  QCOMPARE(a.weights.size(), size_t(4));
  QVERIFY(a.weights == b.weights);
  o.seed = 7;
  const Som c = trainSom(x, o);
  QVERIFY(!(a.weights == c.weights));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, trainSom({}, o));
  o.cols = 0;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, trainSom(x, o));
}

void TstAnalysisRegions::somUnitsFollowTheData() {
  const std::vector<Point2> x = blobs();
  SomOptions o;
  o.rows = 6;
  o.cols = 6;
  o.sigma = 1.0;
  const Som som = trainSom(x, o);
  QCOMPARE(som.weights.size(), size_t(36));
  for (const Point2 &w : som.weights) {
    QVERIFY(w[0] > 36.5 && w[0] < 40.0);
    QVERIFY(w[1] > 35.5 && w[1] < 41.5);
  }
  // Every sample's winner is its nearest unit.
  for (const Point2 &p : x) {
    const int win = som.winner(p);
    const Point2 &w = som.weights[size_t(win)];
    const double d = std::hypot(w[0] - p[0], w[1] - p[1]);
    for (const Point2 &other : som.weights)
      QVERIFY(std::hypot(other[0] - p[0], other[1] - p[1]) >= d - 1e-12);
  }
  // Each group has a unit of its own nearby.
  for (const Point2 &c :
       {Point2{37.0, 36.0}, Point2{38.0, 39.0}, Point2{39.5, 41.0}}) {
    const Point2 &w = som.weights[size_t(som.winner(c))];
    QVERIFY(std::hypot(w[0] - c[0], w[1] - c[1]) < 0.5);
  }
}

void TstAnalysisRegions::bandwidthIsTheMeanNeighbourDistance() {
  // Four points one apart on a line: with two neighbours (itself and the
  // nearest) every distance is 1.
  const std::vector<Point2> line{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
  QCOMPARE(estimateBandwidth(line, 0.5), 1.0);
  // Three neighbours: the ends reach 2, the middle ones 1.
  QCOMPARE(estimateBandwidth(line, 0.75), 1.5);
  // A quantile below one point still uses the point itself.
  QCOMPARE(estimateBandwidth(line, 0.01), 0.0);
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, estimateBandwidth({}, 0.3));
}

void TstAnalysisRegions::meanShiftFindsTheGroups() {
  const std::vector<Point2> x = blobs();
  const MeanShiftResult r = meanShift(x, 0.8);
  QCOMPARE(r.centers.size(), size_t(3));
  // Densest first: the groups hold 80, 60 and 40 points.
  QVERIFY(std::hypot(r.centers[0][0] - 39.5, r.centers[0][1] - 41.0) < 0.1);
  QVERIFY(std::hypot(r.centers[1][0] - 38.0, r.centers[1][1] - 39.0) < 0.1);
  QVERIFY(std::hypot(r.centers[2][0] - 37.0, r.centers[2][1] - 36.0) < 0.1);
  QCOMPARE(r.labels.size(), x.size());
  QCOMPARE(int(std::count(r.labels.begin(), r.labels.end(), 0)), 80);
  QCOMPARE(int(std::count(r.labels.begin(), r.labels.end(), 1)), 60);
  QCOMPARE(int(std::count(r.labels.begin(), r.labels.end(), 2)), 40);
  QCOMPARE(r.labels.front(), 2);
  QCOMPARE(r.labels.back(), 0);
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, meanShift(x, 0.0));
}

void TstAnalysisRegions::meanShiftMergesCloseCentres() {
  // A bandwidth wider than the whole set leaves one region.
  const MeanShiftResult r = meanShift(blobs(), 20.0);
  QCOMPARE(r.centers.size(), size_t(1));
  for (int l : r.labels)
    QCOMPARE(l, 0);
}

void TstAnalysisRegions::linearMapRegions() {
  const PointTable t = loadPointTable(kCatalog);
  RegionOptions o;
  const Regions a = regionalise(t.points, o), b = regionalise(t.points, o);
  QVERIFY(a.regionOf == b.regionOf);
  QCOMPARE(a.regionOf.size(), t.points.size());
  QVERIFY(!a.regions.empty());
  QVERIFY(a.regions.size() <= size_t(5));
  int total = 0;
  for (const RegionSummary &s : a.regions) {
    QVERIFY(s.id >= 1 && s.id <= 5);
    QVERIFY(s.count > 0);
    QVERIFY(s.latMin <= s.latMax && s.lonMin <= s.lonMax);
    QCOMPARE(int(std::count(a.regionOf.begin(), a.regionOf.end(), s.id)),
             s.count);
    total += s.count;
  }
  QCOMPARE(total, t.size());
  QCOMPARE(a.bandwidth, 0.0);
  o.regions = 0;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, regionalise(t.points, o));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           regionalise({}, RegionOptions()));
}

void TstAnalysisRegions::hybridRegions() {
  // Three well separated groups come back as three regions, each whole.
  const std::vector<Point2> x = blobs();
  RegionOptions o;
  o.method = RegionMethod::SomMeanShift;
  o.gridRows = 4;
  o.gridCols = 4;
  o.quantile = 0.3;
  const Regions r = regionalise(x, o);
  QVERIFY(r.bandwidth > 0);
  QCOMPARE(r.regions.size(), size_t(3));
  std::set<int> first(r.regionOf.begin(), r.regionOf.begin() + 40),
      second(r.regionOf.begin() + 40, r.regionOf.begin() + 100),
      third(r.regionOf.begin() + 100, r.regionOf.end());
  QCOMPARE(first.size(), size_t(1));
  QCOMPARE(second.size(), size_t(1));
  QCOMPARE(third.size(), size_t(1));
  QVERIFY(*first.begin() != *second.begin());
  QVERIFY(*second.begin() != *third.begin());
  QVERIFY(*first.begin() != *third.begin());
  const Regions again = regionalise(x, o);
  QVERIFY(r.regionOf == again.regionOf);

  // The script's defaults on the synthetic catalog.
  const PointTable t = loadPointTable(kCatalog);
  RegionOptions d;
  d.method = RegionMethod::SomMeanShift;
  const Regions s = regionalise(t.points, d);
  QVERIFY(!s.regions.empty());
  int total = 0;
  for (const RegionSummary &q : s.regions)
    total += q.count;
  QCOMPARE(total, t.size());
}

void TstAnalysisRegions::regionFiles() {
  const PointTable t = loadPointTable(kCatalog);
  RegionOptions o;
  o.regions = 3;
  const Regions r = regionalise(t.points, o);
  QTemporaryDir dir;
  const QStringList paths =
      saveRegionCsvs(t, r, dir.filePath(QStringLiteral("out")));
  QCOMPARE(paths.size(), qsizetype(r.regions.size()));
  QStringList all;
  for (size_t i = 0; i < r.regions.size(); ++i) {
    QCOMPARE(QFileInfo(paths[qsizetype(i)]).fileName(),
             QStringLiteral("som_region_%1.csv").arg(r.regions[i].id));
    const QStringList lines = QString::fromUtf8(readAll(paths[qsizetype(i)]))
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.first(), t.headerLine);
    QCOMPARE(lines.size() - 1, qsizetype(r.regions[i].count));
    all << lines.mid(1);
  }
  // Together the files are the catalog, every line once.
  QStringList original = t.rows;
  original.sort();
  all.sort();
  QCOMPARE(all, original);
  QCOMPARE(methodPrefix(RegionMethod::SomMeanShift),
           QStringLiteral("sommeanshift"));
  Regions wrong = r;
  wrong.regionOf.pop_back();
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           saveRegionCsvs(t, wrong, dir.path()));
}

void TstAnalysisRegions::citiesAndLayers() {
  QCOMPARE(eafzCities().size(), size_t(16));
  QCOMPARE(eafzCities()[10].name, QString::fromUtf8("Elazığ"));
  QVERIFY(eafzCities()[0].labelLeft);  // Adana: "right" in the script
  QVERIFY(!eafzCities()[2].labelLeft); // Osmaniye: "left"

  QTemporaryDir dir;
  const QString withHeader = dir.filePath(QStringLiteral("a.csv"));
  writeText(withHeader, "name,lat,lon,align\nVan,38.49,43.38,right\n"
                        ",1,2,\nBitlis,38.40,42.11,left\n");
  const std::vector<City> a = loadCities(withHeader);
  QCOMPARE(a.size(), size_t(2));
  QCOMPARE(a[0].name, QStringLiteral("Van"));
  QCOMPARE(a[0].lat, 38.49);
  QCOMPARE(a[0].lon, 43.38);
  QVERIFY(a[0].labelLeft);
  QVERIFY(!a[1].labelLeft);
  const QString bare = dir.filePath(QStringLiteral("b.csv"));
  writeText(bare, "38.49,43.38,Van\n");
  const std::vector<City> b = loadCities(bare);
  QCOMPARE(b.size(), size_t(1));
  QCOMPARE(b[0].name, QStringLiteral("Van"));
  const QString bad = dir.filePath(QStringLiteral("c.csv"));
  writeText(bad, "city,population\nVan,1\n");
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadCities(bad));

  // The shipped outline: Turkey and neighbours inside the clip box.
  const ShapeLayer &countries = shippedCountries();
  QVERIFY(countries.shapes.size() >= 10);
  QCOMPARE(countries.shapes.size(), countries.records.size());
  QStringList names;
  for (const QVariantMap &r : countries.records)
    names << r.value(QStringLiteral("name")).toString();
  QVERIFY(names.contains(QStringLiteral("Turkey")));
  QVERIFY(names.contains(QStringLiteral("Syria")));
  QVERIFY(countries.bbox.left() >= 24.0 - 1e-9);
  QVERIFY(countries.bbox.right() <= 48.0 + 1e-9);
  QVERIFY(countries.bbox.top() >= 32.0 - 1e-9);
  QVERIFY(countries.bbox.bottom() <= 44.0 + 1e-9);
  for (const Shape &s : countries.shapes)
    QCOMPARE(s.kind, Shape::Kind::Polygon);

  const QString geojson = dir.filePath(QStringLiteral("p.geojson"));
  writeText(geojson,
            R"({"type":"FeatureCollection","features":[
 {"type":"Feature","properties":{"name":"A"},"geometry":{"type":"Polygon",
  "coordinates":[[[0,0],[2,0],[2,1],[0,0]]]}},
 {"type":"Feature","properties":{"name":"L"},"geometry":{"type":"LineString",
  "coordinates":[[0,0],[1,1]]}}]})");
  const ShapeLayer one = readGeoJsonPolygons(geojson);
  QCOMPARE(one.shapes.size(), size_t(1));
  QCOMPARE(one.shapes[0].parts[0].size(), size_t(4));
  QCOMPARE(one.bbox, QRectF(QPointF(0, 0), QPointF(2, 1)));
  writeText(geojson, "{\"type\":\"FeatureCollection\",\"features\":[]}");
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, readGeoJsonPolygons(geojson));

  QCOMPARE(regionColor(0, 5), QColor(0x00, 0x72, 0xB2));
  QCOMPARE(regionColor(8, 8), QColor(0x00, 0x72, 0xB2));
  QCOMPARE(regionColor(0, 9), QColor(0x1f, 0x77, 0xb4));
  QCOMPARE(regionMapSize(100), QSize(1200, 1000));
}

void TstAnalysisRegions::mapDraws() {
  const PointTable t = loadPointTable(kCatalog);
  RegionOptions o;
  const Regions r = regionalise(t.points, o);
  RegionMapSpec spec;
  spec.lang = QStringLiteral("en");
  spec.title = QStringLiteral("Test area  |  Sub-regions\nSOM Model");
  spec.points = &t.points;
  spec.regions = &r;
  spec.countries = &shippedCountries();
  spec.cities = eafzCities();
  spec.marker = MapMarker{38.4, 39.1, QStringLiteral("Station")};
  const std::vector<FaultLine> faults{
      {{QPointF(38.0, 38.0), QPointF(40.5, 39.0)}, QStringLiteral("DYK")},
      {{QPointF(38.0, 38.6), QPointF(40.5, 39.4)}, QString()}};
  spec.faults = &faults;
  const QRectF extent = regionMapExtent(spec);
  QVERIFY(extent.contains(QPointF(39.3279, 38.5384)));
  const QImage img =
      renderImage(regionMapSize(100), [&](QPainter &p, const QRectF &rect) {
        drawRegionMap(p, rect, spec);
      });
  QCOMPARE(img.size(), QSize(1200, 1000));
  // Turkey's fill, the region colours and the surface-rupture red are there.
  std::set<QRgb> colours;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x)
      colours.insert(img.pixel(x, y));
  QVERIFY(colours.size() > 50);
  QVERIFY(colours.count(QColor(0xFF, 0xF8, 0xE7).rgb()));
  QVERIFY(colours.count(QColor(0xD3, 0x2F, 0x2F).rgb()));
  QCOMPARE(img.pixelColor(2, 2), QColor(Qt::white));

  // The plain map: no layers, grey events.
  RegionMapSpec plain;
  plain.points = &t.points;
  const QImage img2 =
      renderImage(regionMapSize(50), [&](QPainter &p, const QRectF &rect) {
        drawRegionMap(p, rect, plain);
      });
  QCOMPARE(img2.size(), QSize(600, 500));
  QVERIFY(img2.pixelColor(300, 250) != QColor(Qt::white));

  QTemporaryDir dir;
  const QString svg = dir.filePath(QStringLiteral("m.svg"));
  QVERIFY(
      renderSvg(svg, regionMapSize(100), [&](QPainter &p, const QRectF &rect) {
        drawRegionMap(p, rect, spec);
      }));
  const QByteArray text = readAll(svg);
  QVERIFY(text.contains("Elaz"));
  QVERIFY(text.contains("Cluster Center"));
  QVERIFY(text.contains("Region 1"));
  QVERIFY(text.contains("accepted national boundaries"));
}

void TstAnalysisRegions::wholeRun() {
  QTemporaryDir dir;
  RegionRunOptions o;
  o.catalogPath = kCatalog;
  o.outDir = dir.filePath(QStringLiteral("run"));
  o.areaName = QStringLiteral("Synthetic zone");
  o.lang = QStringLiteral("en");
  o.dpi = 40;
  o.svg = true;
  o.cities = eafzCities();
  o.countries = std::shared_ptr<const ShapeLayer>(&shippedCountries(),
                                                  [](const ShapeLayer *) {});
  o.countriesSource = QStringLiteral("shipped");
  QStringList log;
  RegionRunHooks hooks;
  hooks.log = [&](const QString &s) { log << s; };
  int last = -1, total = 0;
  hooks.progress = [&](int d, int t, const QString &) {
    QVERIFY(d > last);
    last = d;
    total = t;
  };
  const RegionRunResult r = runRegions(o, hooks);
  QVERIFY(!r.stopped);
  QCOMPARE(r.results.size(), size_t(2));
  QCOMPARE(r.results[0].method, RegionMethod::Som);
  QCOMPARE(r.results[1].method, RegionMethod::SomMeanShift);
  QCOMPARE(last, total - 1);
  const QDir out(r.outDir);
  for (const char *name :
       {"map_01_study_area_plain.png", "map_02_study_area_geographic.png",
        "map_03_som.png", "map_04_som_meanshift.png", "map_03_som.svg",
        "regions_summary.csv", "regions_run.json"})
    QVERIFY2(out.exists(QString::fromLatin1(name)), name);
  for (const RegionSummary &s : r.results[0].regions)
    QVERIFY(out.exists(QStringLiteral("som_region_%1.csv").arg(s.id)));
  for (const RegionSummary &s : r.results[1].regions)
    QVERIFY(out.exists(QStringLiteral("sommeanshift_region_%1.csv").arg(s.id)));
  QCOMPARE(QImage(out.filePath(QStringLiteral("map_03_som.png"))).size(),
           QSize(480, 400));
  for (const QString &f : r.files)
    QVERIFY2(QFileInfo::exists(f), qPrintable(f));

  const QJsonObject rec =
      QJsonDocument::fromJson(
          readAll(out.filePath(QStringLiteral("regions_run.json"))))
          .object();
  QCOMPARE(rec.value(QStringLiteral("input"))
               .toObject()
               .value(QStringLiteral("events"))
               .toInt(),
           2506);
  QCOMPARE(rec.value(QStringLiteral("input"))
               .toObject()
               .value(QStringLiteral("catalog_sha256"))
               .toString()
               .size(),
           64);
  QCOMPARE(rec.value(QStringLiteral("parameters"))
               .toObject()
               .value(QStringLiteral("seed"))
               .toString(),
           QStringLiteral("42"));
  QCOMPARE(rec.value(QStringLiteral("results")).toArray().size(), 2);
  const QStringList summary =
      QString::fromUtf8(
          readAll(out.filePath(QStringLiteral("regions_summary.csv"))))
          .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QCOMPARE(summary.size() - 1, qsizetype(r.results[0].regions.size() +
                                         r.results[1].regions.size()));
  QVERIFY(
      log.join(QLatin1Char('\n')).contains(QStringLiteral("Mean Shift ->")));

  // The same options give the same regions, byte for byte.
  RegionRunOptions again = o;
  again.outDir = dir.filePath(QStringLiteral("again"));
  again.maps = false;
  again.somMeanShift = false;
  const RegionRunResult r2 = runRegions(again);
  QCOMPARE(r2.results.size(), size_t(1));
  QVERIFY(!QDir(r2.outDir).exists(QStringLiteral("map_03_som.png")));
  for (const RegionSummary &s : r2.results[0].regions) {
    const QString name = QStringLiteral("som_region_%1.csv").arg(s.id);
    QCOMPARE(readAll(QDir(r2.outDir).filePath(name)),
             readAll(out.filePath(name)));
  }

  // A stop request before the first method still leaves a record.
  RegionRunOptions stopped = o;
  stopped.outDir = dir.filePath(QStringLiteral("stopped"));
  int asked = 0;
  RegionRunHooks stopHooks;
  stopHooks.shouldStop = [&] { return ++asked > 2; };
  const RegionRunResult r3 = runRegions(stopped, stopHooks);
  QVERIFY(r3.stopped);
  QVERIFY(r3.results.empty());
  QVERIFY(QDir(r3.outDir).exists(QStringLiteral("regions_run.json")));
}

void TstAnalysisRegions::runRefusesNothingToDo() {
  QTemporaryDir dir;
  RegionRunOptions o;
  o.catalogPath = kCatalog;
  o.outDir = dir.filePath(QStringLiteral("x"));
  o.som = o.somMeanShift = false;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, runRegions(o));
  o.som = true;
  o.outDir.clear();
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, runRegions(o));
  o.outDir = dir.filePath(QStringLiteral("x"));
  o.catalogPath = dir.filePath(QStringLiteral("none.csv"));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, runRegions(o));
  RegionRunHooks hooks;
  hooks.shouldStop = [] { return true; };
  o.catalogPath = kCatalog;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, runRegions(o, hooks));
}

QTEST_MAIN(TstAnalysisRegions)
#include "tst_analysis_regions.moc"
