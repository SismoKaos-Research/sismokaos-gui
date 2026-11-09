// The analysis figures: series preparation and the QPainter renderings
// (daily, combined, event-centred, the map, the grid), rendered offscreen
// into images and SVG and checked for the elements they must contain.
#include <QtTest>

#include <cmath>

#include "analysis/events.h"
#include "analysis/faults.h"
#include "analysis/grid.h"
#include "analysis/map.h"
#include "analysis/plot.h"
#include "analysis/series.h"
#include "fx/npy.h"

using namespace skg::analysis;

namespace {
const QString kData = QStringLiteral(SKG_TEST_DATA "/analysis/");
constexpr double kElazig = 1579888514.0; // 2020-01-24 17:55:14 UTC

// A three-day table of 200 s windows every 200 s with a signed metric.
skg::fx::NpyTable makeTable() {
  skg::fx::NpyTable t;
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Wolf_LyE"), QStringLiteral("<f8")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Z_RMS"), QStringLiteral("<f8")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Etiket"), QStringLiteral("<f8")});
  const double day0 = 1579824000.0; // 2020-01-24 00:00 UTC
  QStringList ids;
  std::vector<double> zaman, wolf, rms, label;
  int n = 0;
  for (int d = 0; d < 3; ++d)
    for (int w = 1; w <= 432; ++w) {
      const double end = day0 + d * 86400 + w * 200.0;
      ids << QStringLiteral("enz_w%1").arg(++n);
      zaman.push_back(end / 60.0);
      wolf.push_back(0.4 * std::sin(w / 20.0) +
                     (d == 1 && w > 300 ? 0.5 : 0.0));
      rms.push_back(100 + 10 * std::cos(w / 7.0));
      label.push_back(0);
    }
  t.rows = n;
  t.strings.append(ids);
  for (int i = 0; i < 4; ++i)
    t.strings.append(QStringList());
  t.numeric.append(std::vector<double>());
  t.numeric.append(zaman);
  t.numeric.append(wolf);
  t.numeric.append(rms);
  t.numeric.append(label);
  return t;
}

int countColor(const QImage &img, const QColor &c, int tol = 40) {
  int n = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x) {
      const QColor px = img.pixelColor(x, y);
      if (std::abs(px.red() - c.red()) < tol &&
          std::abs(px.green() - c.green()) < tol &&
          std::abs(px.blue() - c.blue()) < tol)
        ++n;
    }
  return n;
}
// With SKG_PLOT_DUMP=<dir> the figures are written there for a look.
void dump(const QImage &img, const char *name) {
  const QByteArray dir = qgetenv("SKG_PLOT_DUMP");
  if (!dir.isEmpty())
    img.save(QString::fromLocal8Bit(dir) + '/' + QLatin1String(name) + ".png");
}
int nonWhite(const QImage &img) {
  int n = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x)
      if (img.pixelColor(x, y) != QColor(Qt::white))
        ++n;
  return n;
}
} // namespace

class TstAnalysisPlot : public QObject {
  Q_OBJECT
private slots:
  void seriesAndDays() {
    const auto t = makeTable();
    QCOMPARE(plottableColumns(t), (QStringList{"Wolf_LyE", "Z_RMS"}));
    const MetricSeries s = metricSeries(t, "Wolf_LyE");
    QCOMPARE(int(s.epoch.size()), 3 * 432);
    const auto days = daysOf(s);
    QCOMPARE(int(days.size()), 3);
    QCOMPARE(days[0], QDate(2020, 1, 24));
    QCOMPARE(days[2], QDate(2020, 1, 26));
    const DaySlice d = daySlice(s, QDate(2020, 1, 25));
    QCOMPARE(int(d.minutes.size()), 432);
    QVERIFY(d.minutes.front() > 3.3 && d.minutes.front() < 3.4); // 200 s
    QCOMPARE(d.minutes.back(), 1440.0);
    const Areas a = areasOf(d);
    QVERIFY(a.positive > 0 && a.negative < 0);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             metricSeries(t, "Pencere_ID"));

    QCOMPARE(int(centredDays(QDate(2020, 1, 25), days, 3).size()), 3);
    QCOMPARE(int(centredDays(QDate(2020, 1, 24), days, 5).size()),
             3); // clipped to what exists
    QCOMPARE(centredDays(QDate(2020, 1, 26), days, 3).front(),
             QDate(2020, 1, 25));
    QVERIFY(scapRange("Wolf_LyE").has_value());
    QCOMPARE(scapRange("Wolf_LyE")->max, 1.0);
    QVERIFY(!scapRange("Z_RMS"));
    const YRange r = autoRange(metricSeries(t, "Z_RMS"));
    QCOMPARE(r.min, 0.0);
    QVERIFY(r.max > 110);
    QCOMPARE(metricTitle("Samp_Ent", "en"), QStringLiteral("Sample Entropy"));
    QCOMPARE(metricTitle("Z_RMS", "tr"), QStringLiteral("Z_RMS"));
    QCOMPARE(monthName(1, "tr"), QStringLiteral("Ocak"));
  }

  void dailyFigure() {
    const auto t = makeTable();
    const MetricSeries s = metricSeries(t, "Wolf_LyE");
    const auto events =
        eventsForStation(loadEvents(kData + "events.csv"), 38.68, 39.26);
    PlotStyle style;
    style.station = "ELZG";
    style.channel = "Z";
    const QImage img =
        renderImage(dailySize(100), [&](QPainter &p, const QRectF &r) {
          drawDaily(p, r, s, QDate(2020, 1, 24), events, style);
        });
    dump(img, "daily");
    QCOMPARE(img.size(), QSize(827, 310));
    QVERIFY(nonWhite(img) > 5000);
    // Both fills are present (alpha-blended on white).
    QVERIFY(countColor(img, QColor(0xE8, 0x93, 0x93), 30) > 200);
    QVERIFY(countColor(img, QColor(0x82, 0xA3, 0xE8), 30) > 200);
    // The depth colour map runs from light yellow to indigo.
    QCOMPARE(depthColor(0).name(), QStringLiteral("#ffff99"));
    QCOMPARE(depthColor(80).name(), QStringLiteral("#4b0082"));
    QVERIFY(depthColor(10).red() == 255 && depthColor(10).green() < 0xD7 + 1);

    const QTemporaryDir dir;
    const QString svg = dir.filePath("daily.svg");
    QVERIFY(renderSvg(svg, dailySize(100), [&](QPainter &p, const QRectF &r) {
      drawDaily(p, r, s, QDate(2020, 1, 24), events, style);
    }));
    QFile f(svg);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray text = f.readAll();
    QVERIFY(text.contains("<svg"));
    QVERIFY(text.contains("ELZG"));
    QVERIFY(text.contains("M6.8")); // the main shock is labelled
  }

  void combinedAndCentredFigures() {
    const auto t = makeTable();
    const MetricSeries s = metricSeries(t, "Wolf_LyE");
    const auto at =
        eventsForStation(loadEvents(kData + "events.csv"), 38.68, 39.26);
    const auto main = mainEvent(at);
    QVERIFY(main.has_value());
    PlotStyle style;
    style.lang = "en";
    const auto days = daysOf(s);
    const QImage combined =
        renderImage(combinedSize(100), [&](QPainter &p, const QRectF &r) {
          drawCombined(p, r, s, days, at, main, style);
        });
    dump(combined, "combined");
    QCOMPARE(combined.size(), QSize(1100, 310));
    QVERIFY(nonWhite(combined) > 8000);
    // The main-event line is pure red.
    QVERIFY(countColor(combined, QColor(0xD9, 0, 0), 20) > 50);
    const auto window = centredDays(QDate(2020, 1, 24), days, 3);
    const QImage centred =
        renderImage(centredSize(100), [&](QPainter &p, const QRectF &r) {
          drawCentred(p, r, s, window, at, *main, style);
        });
    dump(centred, "centred");
    QCOMPARE(centred.size(), QSize(950, 310));
    QVERIFY(nonWhite(centred) > 8000);
  }

  void mapFigure() {
    const auto stations = loadStations(kData + "stations.csv");
    const auto events = loadEvents(kData + "events.csv");
    const FaultIndex faults = loadFaults(kData + "faults.shp");
    MapSpec spec;
    spec.station = *findStation(stations, "ELZG");
    spec.events = eventsForStation(events, spec.station.lat, spec.station.lon);
    spec.mainEvent = mainEvent(spec.events);
    spec.faults = &faults;
    spec.title = "ELZG";
    const QRectF extent = mapExtent(spec);
    QVERIFY(extent.contains(QPointF(spec.station.lon, spec.station.lat)));
    QVERIFY(extent.width() > 10); // 780 km to Sakarya plus padding
    const QImage img =
        renderImage(QSize(800, 700),
                    [&](QPainter &p, const QRectF &r) { drawMap(p, r, spec); });
    dump(img, "map");
    QVERIFY(nonWhite(img) > 100000); // the sea-blue background fills the frame
    QVERIFY(countColor(img, QColor(0xB2, 0x10, 0x10), 30) > 20); // fault lines
    QVERIFY(countColor(img, QColor(0xFF, 0xD7, 0x00), 30) >
            20); // the main-event star
    spec.extentKm = 150;
    spec.showFaults = false;
    const QImage close =
        renderImage(QSize(400, 400),
                    [&](QPainter &p, const QRectF &r) { drawMap(p, r, spec); });
    QVERIFY(nonWhite(close) > 10000);
  }

  void gridComposition() {
    GridSpec g;
    g.rowLabels = {"ELZG", "MLZ"};
    g.colLabels = {"24/01", "25/01", "26/01"};
    g.title = "Wolf_LyE";
    int calls = 0;
    const QImage out = composeGrid(g, [&](int row, int col) {
      ++calls;
      if (row == 1 && col == 2)
        return QImage();
      QImage cell(400, 150, QImage::Format_ARGB32);
      cell.fill(row == 0 ? QColor(Qt::red) : QColor(Qt::blue));
      return cell;
    });
    dump(out, "grid");
    QCOMPARE(calls, 6);
    QCOMPARE(out.width(), 150 + 3 * (200 + 3));
    QCOMPARE(out.height(), 80 + 2 * (75 + 3));
    QVERIFY(countColor(out, QColor(Qt::red), 10) > 3 * 200 * 75 * 0.9);
    QVERIFY(countColor(out, QColor(Qt::blue), 10) > 2 * 200 * 75 * 0.9);
    QVERIFY(countColor(out, QColor(0xf2, 0xf2, 0xf2), 2) >
            200 * 75 * 0.8); // the empty cell
    QVERIFY(
        composeGrid(GridSpec{}, [](int, int) { return QImage(); }).isNull());
  }
};

QTEST_MAIN(TstAnalysisPlot)
#include "tst_analysis_plot.moc"
