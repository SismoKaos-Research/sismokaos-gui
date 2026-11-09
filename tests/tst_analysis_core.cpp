// The analysis port, first slice: geometry, catalogs and their searches,
// the shapefile reader and fault distances, earthquake labelling of feature
// tables. Fixtures in tests/data/analysis (a two-fault shapefile written
// with Python's struct module, ISO-8859-9 attributes; a five-event AFAD
// catalog around the 2020 Elazığ earthquake; three stations).
#include <QtTest>

#include <cmath>

#include "analysis/events.h"
#include "analysis/faults.h"
#include "analysis/geo.h"
#include "analysis/label.h"
#include "analysis/shapefile.h"
#include "fx/npy.h"

using namespace skg::analysis;

namespace {
const QString kData = QStringLiteral(SKG_TEST_DATA "/analysis/");
constexpr double kElazig = 1579888514.0; // 2020-01-24 17:55:14 UTC
} // namespace

class TstAnalysisCore : public QObject {
  Q_OBJECT
private slots:
  void geometry() {
    QVERIFY(std::abs(haversineKm(38.68, 39.26, 38.3593, 39.0630) - 39.4) < 0.5);
    QVERIFY(std::abs(bearingDeg(0, 0, 1, 0) - 0.0) < 1e-9);
    QVERIFY(std::abs(bearingDeg(0, 0, 0, 1) - 90.0) < 1e-9);
    QCOMPARE(compassDirection(0, "tr"), QStringLiteral("Kuzey"));
    QCOMPARE(compassDirection(44, "tr"), QStringLiteral("Kuzey Doğu"));
    QCOMPARE(compassDirection(200, "en"), QStringLiteral("South"));
    QCOMPARE(compassDirection(330, "en"), QStringLiteral("North West"));
    QCOMPARE(compassDirection(337.5, "en"),
             QStringLiteral("North")); // SCAP rounds the boundary up
    QCOMPARE(compassDirection(std::nan(""), "tr"),
             QStringLiteral("Bilinmiyor"));
    QCOMPARE(distanceClass(100), 1);
    QCOMPARE(distanceClass(100.1), 2);
    QCOMPARE(distanceClass(500), 3);
    QCOMPARE(distanceClass(501), 4);

    const LocalProjection proj(38.0, 39.0);
    const QPointF km = proj.toKm(38.1, 39.1);
    QVERIFY(std::abs(km.y() - 11.12) < 0.05);
    QVERIFY(std::abs(km.x() - 11.12 * std::cos(38.0 * M_PI / 180)) < 0.05);
    const QPointF back = proj.toLatLon(km);
    QVERIFY(std::abs(back.x() - 38.1) < 1e-9 &&
            std::abs(back.y() - 39.1) < 1e-9);

    // A point 0.1 degrees north of a west-east line: the foot is straight
    // below.
    const std::vector<QPointF> line{{39.0, 38.0}, {40.0, 38.0}};
    const NearestPoint np = nearestOnPolyline(38.1, 39.5, line);
    QVERIFY(std::abs(np.km - 11.12) < 0.05);
    QVERIFY(std::abs(np.lat - 38.0) < 1e-6 && std::abs(np.lon - 39.5) < 1e-6);
    QCOMPARE(np.segment, 0);
    // Beyond the end the nearest point is the end vertex.
    const NearestPoint end = nearestOnPolyline(38.0, 40.5, line);
    QVERIFY(std::abs(end.lon - 40.0) < 1e-9);
  }

  void catalogs() {
    const auto events = loadEvents(kData + "events.csv");
    QCOMPARE(int(events.size()), 5);
    QCOMPARE(events[0].epoch, kElazig);
    QCOMPARE(events[0].mag, 6.8);
    QCOMPARE(events[0].place, QStringLiteral("Sivrice (Elazig)"));
    QCOMPARE(events[0].type, QStringLiteral("Mw"));
    QCOMPARE(events[0].depthKm, 8.06);
    // A catalog kept in Turkey time is shifted back to UTC.
    const auto local = loadEvents(kData + "events.csv", 3.0);
    QCOMPARE(local[0].epoch, kElazig - 3 * 3600);
    QCOMPARE(eventFolderName(events[0]),
             QStringLiteral("24012020_M6.8_Sivrice_Elazig"));

    const auto stations = loadStations(kData + "stations.csv");
    QCOMPARE(int(stations.size()), 3);
    const auto elzg = findStation(stations, "elzg");
    QVERIFY(elzg.has_value());
    QCOMPARE(elzg->network, QStringLiteral("TU"));
    QCOMPARE(elzg->province, QStringLiteral("Elazig"));
    QVERIFY(!findStation(stations, "NOPE"));

    EventQuery q;
    q.minMag = 4.0;
    QCOMPARE(int(selectEvents(events, q).size()), 3);
    q.placeContains = "elazig";
    QCOMPARE(int(selectEvents(events, q).size()), 2);
    EventQuery near;
    near.centerLat = 38.68;
    near.centerLon = 39.26;
    near.radiusKm = 100;
    QCOMPARE(int(selectEvents(events, near).size()), 3);
    EventQuery day;
    day.startEpoch = kElazig - 1;
    day.endEpoch = kElazig + 86400;
    QCOMPARE(int(selectEvents(events, day).size()),
             4); // the Sakarya event is on the 26th

    const auto nearby = stationsWithin(stations, 38.3593, 39.0630, 100);
    QCOMPARE(int(nearby.size()), 2);
    QCOMPARE(nearby[0].station.code, QStringLiteral("ELZG"));
    QVERIFY(nearby[0].km < nearby[1].km);

    // Bands from ELZG: the Istanbul M3.5 at ~1000 km is out, Sakarya M6.1 at
    // ~780 km is in (500-1000 km needs M6+), Puturge M3.2 at ~70 km is in.
    const auto at = eventsForStation(events, 38.68, 39.26);
    QCOMPARE(int(at.size()), 4);
    QCOMPARE(at[3].band, 3);
    QCOMPARE(at[2].band, 0);
    const auto main = mainEvent(at);
    QVERIFY(main.has_value());
    QCOMPARE(main->event.mag, 6.8);
    QVERIFY(std::abs(main->km - 39.4) < 0.5);
    QVERIFY(!mainEvent(at, 7.0));
  }

  void shapefileAndFaults() {
    const ShapeLayer layer = readShapefile(kData + "faults.shp");
    QCOMPARE(int(layer.shapes.size()), 2);
    QCOMPARE(layer.shapes[0].kind, Shape::Kind::PolyLine);
    QCOMPARE(int(layer.shapes[0].parts.size()), 1);
    QCOMPARE(int(layer.shapes[1].parts.size()), 2);
    QCOMPARE(int(layer.shapes[0].parts[0].size()), 3);
    QCOMPARE(layer.shapes[0].parts[0][0], QPointF(39.0, 38.5));
    QVERIFY(layer.fields.contains("FAYADI"));
    QCOMPARE(layer.records[0].value("FAYADI").toString(),
             QStringLiteral("Doğu Anadolu Fayı"));
    QCOMPARE(layer.records[0].value("SEGMENTADI").toString(),
             QStringLiteral("Pütürge Segmenti"));
    QCOMPARE(layer.records[0].value("FAYTIPI").toDouble(), 1.0);
    QCOMPARE(layer.records[0].value("UZUNLUK").toDouble(), 96.5);
    QCOMPARE(layer.records[1].value("ACIKLAMA").toString(),
             QStringLiteral("Şüpheli"));
    QVERIFY(layer.projection.contains("WGS_1984"));
    QCOMPARE(decodeLatin5(QByteArray("\xDD\xFE\xF0")), QStringLiteral("İşğ"));

    const FaultIndex faults(layer);
    QCOMPARE(int(faults.faults().size()), 3); // the second shape has two parts
    QCOMPARE(faults.faults()[0].label(),
             QStringLiteral("Doğu Anadolu Fayı (Pütürge Segmenti)"));
    QCOMPARE(Fault::typeName(1), QStringLiteral("DYK"));
    QCOMPARE(Fault::typeName(9), QStringLiteral("Kod:9"));
    // ELZG lies north-east of the DAF segment.
    const auto n = faults.nearest(38.68, 39.26);
    QVERIFY(n.has_value());
    QCOMPARE(n->fault->name, QStringLiteral("Doğu Anadolu Fayı"));
    QVERIFY(n->km > 3 && n->km < 7); // ~4.7 km by hand
    QVERIFY(n->azimuth > 300 ||
            n->azimuth < 60); // the station is north of the fault point
    // A point by the KAF's second part.
    const auto k = faults.nearest(39.16, 40.26);
    QCOMPARE(k->fault->name, QStringLiteral("Kuzey Anadolu Fayı"));
    QVERIFY(k->km < 5);
    QCOMPARE(int(faults.within(QRectF(QPointF(38.9, 38.4), QPointF(39.5, 38.8)))
                     .size()),
             1);
    QVERIFY(!FaultIndex().nearest(0, 0));
  }

  void labelling() {
    // An fx-style table: Zaman_Dk is the window end in epoch minutes.
    skg::fx::NpyTable t;
    t.fields.append(skg::fx::NpyField{QStringLiteral("Pencere_ID"),
                                      QStringLiteral("<U50")});
    t.fields.append(
        skg::fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")});
    t.fields.append(
        skg::fx::NpyField{QStringLiteral("Z_RMS"), QStringLiteral("<f8")});
    t.rows = 3;
    t.strings.append(QStringList{"enz_w01", "enz_w02", "enz_w03"});
    t.strings.append(QStringList());
    t.strings.append(QStringList());
    t.numeric.append(std::vector<double>());
    // windows ending 100 s after, 600 s after, and a day after the main shock
    t.numeric.append({(kElazig + 100) / 60.0, (kElazig + 600) / 60.0,
                      (kElazig + 86400) / 60.0});
    t.numeric.append({1.0, 2.0, 3.0});
    const auto events = loadEvents(kData + "events.csv");
    LabelOptions opt;
    opt.winSec = 200;
    opt.stationLat = 38.68;
    opt.stationLon = 39.26;
    const LabelSummary s = labelWindows(t, events, opt);
    QCOMPARE(s.windows, 3);
    QCOMPARE(s.withEvents, 2); // the M6.8 in the first, the M4.1 (18:04:00, 526
                               // s later) in the second
    QCOMPARE(t.fields.size(), 12);
    const int etiket = t.fields.size() - 1, sayi = etiket - 1, yon = etiket - 2,
              derece = etiket - 3, dist = etiket - 4, mag = etiket - 7,
              span = etiket - 8;
    QCOMPARE(t.fields[etiket].name, QStringLiteral("Etiket"));
    QCOMPARE(t.value(0, etiket).toDouble(), 1.0);
    QCOMPARE(t.value(1, etiket).toDouble(), 1.0);
    QCOMPARE(t.value(2, etiket).toDouble(), 0.0);
    QCOMPARE(t.value(0, sayi).toDouble(), 1.0);
    QCOMPARE(t.value(0, mag).toString(), QStringLiteral("6.8"));
    QCOMPARE(t.value(1, mag).toString(), QStringLiteral("4.1"));
    QCOMPARE(t.value(0, derece).toString(), QStringLiteral("1"));
    QVERIFY(t.value(0, dist).toString().startsWith("39."));
    QCOMPARE(t.value(0, yon).toString(), QStringLiteral("Güney Batı"));
    QCOMPARE(t.value(0, span).toString(),
             QStringLiteral("17:53:34 - 17:56:54"));
    // Labelling twice replaces the columns rather than adding more.
    labelWindows(t, events, opt);
    QCOMPARE(t.fields.size(), 12);

    // SCAP ids: hour in the id, minutes into the hour; the window is clipped at
    // the hour.
    skg::fx::NpyTable scap;
    scap.fields.append(skg::fx::NpyField{QStringLiteral("Pencere_ID"),
                                         QStringLiteral("<U50")});
    scap.fields.append(
        skg::fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")});
    scap.rows = 2;
    scap.strings.append(QStringList{"2020_01_24_17_w01", "2020_01_24_17_w17"});
    scap.strings.append(QStringList());
    scap.numeric.append(std::vector<double>());
    scap.numeric.append({3.333, 56.0});
    const auto w1 = windowSpan(scap, 0, 200), w17 = windowSpan(scap, 1, 200);
    QVERIFY(w1 && w17);
    QCOMPARE(w1->start, 1579885200.0); // 17:00:00
    QCOMPARE(w1->end, 1579885400.0);   // 17:03:20
    QCOMPARE(w17->end - w17->start, 200.0);
    LabelOptions plain;
    const LabelSummary s2 = labelWindows(scap, events, plain);
    QCOMPARE(s2.withEvents, 1); // 17:55:14 lies in [17:52:40, 17:56:00)

    // Round trip through CSV keeps the strings and the numbers.
    const QTemporaryDir dir;
    const QString csv = dir.filePath("labelled.csv");
    QVERIFY(writeCsv(csv, t));
    const skg::fx::NpyTable back = loadTable(csv);
    QCOMPARE(back.rows, 3);
    QCOMPARE(back.fields.size(), 12);
    QCOMPARE(back.value(0, 0).toString(), QStringLiteral("enz_w01"));
    QCOMPARE(back.value(1, 2).toDouble(), 2.0);
    QCOMPARE(back.value(0, yon).toString(), QStringLiteral("Güney Batı"));
    QCOMPARE(back.value(2, etiket).toDouble(), 0.0);
    QVERIFY(std::isnan(back.value(2, mag).toDouble()) ||
            back.value(2, mag).toString().isEmpty());
  }
};

QTEST_GUILESS_MAIN(TstAnalysisCore)
#include "tst_analysis_core.moc"
