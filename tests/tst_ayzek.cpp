// The ayzek launcher's logic: where an installation keeps its models, the
// command line built from the options, the lines ayzek prints while running
// and the counts of its final report.
#include <QtTest>

#include "ayzek/launcher.h"

using namespace skg::ayzek;

namespace {
bool fakeTool(const QString &path) {
  return QFile::copy(QStringLiteral(SKG_TEST_HELPER), path);
}
} // namespace

class TstAyzek : public QObject {
  Q_OBJECT
private slots:
  void installWithModels() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QVERIFY(!installAt(tmp.filePath("missing")).has_value());
#ifdef Q_OS_WIN
    const QString exe = tmp.filePath("ayzek.exe");
#else
    const QString exe = tmp.filePath("ayzek");
#endif
    QVERIFY(fakeTool(exe));
    auto in = installAt(exe);
    QVERIFY(in.has_value());
    QVERIFY(!in->hasModels());
    QCOMPARE(QFileInfo(in->dir).canonicalFilePath(),
             QFileInfo(tmp.path()).canonicalFilePath());
    QVERIFY(QDir(tmp.path()).mkdir("models"));
    QFile st(tmp.filePath("models/stations.csv"));
    QVERIFY(st.open(QIODevice::WriteOnly));
    st.write("code,lat,lon\n");
    st.close();
    in = installAt(exe);
    QVERIFY(in->hasModels());
    QVERIFY(in->modelsDir.endsWith("/models"));
  }

  void replayArguments() {
    Options o;
    o.files = {"/d/DEMI.mseed", "/d/MANT.mseed"};
    o.speed = 10;
    o.from = "2025-11-10T18:20:00";
    o.catalog = "/d/demo.csv";
    o.modelsDir = "/m";
    o.minStations = 3;
    o.threshold = 0.85;
    o.sites = {{"Istanbul", 41.0082, 28.9784}};
    o.extra = {"--slack", "4"};
    const QStringList a = arguments(o);
    QCOMPARE(a.first(), QString("--no-color"));
    QCOMPARE(a.last(), QString("/d/MANT.mseed"));
    const int sp = int(a.indexOf("--speed"));
    QVERIFY(sp > 0);
    QCOMPARE(a[sp + 1], QString("10"));
    QCOMPARE(a[int(a.indexOf("--from")) + 1], QString("2025-11-10T18:20:00"));
    QCOMPARE(a[int(a.indexOf("--catalog")) + 1], QString("/d/demo.csv"));
    QCOMPARE(a[int(a.indexOf("--models")) + 1], QString("/m"));
    QCOMPARE(a[int(a.indexOf("--min-stations")) + 1], QString("3"));
    QCOMPARE(a[int(a.indexOf("--threshold")) + 1], QString("0.85"));
    QCOMPARE(a[int(a.indexOf("--site")) + 1],
             QString("Istanbul,41.0082,28.9784"));
    QVERIFY(a.contains("--slack"));
    QVERIFY(!a.contains("--detector")); // the default detector is not spelt
    QVERIFY(!a.contains("--seedlink"));
    // Files come after every option, so a file named like an option cannot
    // swallow the next one.
    QVERIFY(a.indexOf("/d/DEMI.mseed") > a.indexOf("--speed"));
  }

  void detectorAndFlags() {
    Options o;
    o.files = {"x.mseed"};
    o.speed = 0;
    o.detector = Detector::Transformer;
    o.locate = Locate::Geometry;
    o.assess = true;
    o.assessCsv = "/o/assess.csv";
    o.scoresDir = "/o/scores";
    o.noMagnitude = true;
    o.verbose = true;
    const QStringList a = arguments(o);
    QCOMPARE(a[int(a.indexOf("--detector")) + 1], QString("transformer"));
    QCOMPARE(a[int(a.indexOf("--locate")) + 1], QString("geometry"));
    QVERIFY(a.contains("--assess"));
    QCOMPARE(a[int(a.indexOf("--assess-csv")) + 1], QString("/o/assess.csv"));
    QCOMPARE(a[int(a.indexOf("--scores")) + 1], QString("/o/scores"));
    QVERIFY(a.contains("--no-magnitude"));
    QVERIFY(a.contains("--verbose"));
    QCOMPARE(a[int(a.indexOf("--speed")) + 1], QString("0"));
    QCOMPARE(detectorArg(Detector::StaLta), QString("stalta"));
    QCOMPARE(detectorArg(Detector::FilterPicker), QString("filterpicker"));
  }

  void liveArguments() {
    Options o;
    o.mode = Mode::Live;
    o.server = "geofon.gfz.de:18000";
    o.streams = "GE.WLF,GE.APE";
    o.archiveDir = "/d/live";
    o.durationS = 3600;
    const QStringList a = arguments(o);
    QCOMPARE(a[int(a.indexOf("--seedlink")) + 1],
             QString("geofon.gfz.de:18000"));
    QCOMPARE(a[int(a.indexOf("--streams")) + 1], QString("GE.WLF,GE.APE"));
    QCOMPARE(a[int(a.indexOf("--archive")) + 1], QString("/d/live"));
    QCOMPARE(a[int(a.indexOf("--duration")) + 1], QString("3600"));
    QVERIFY(!a.contains("--speed")); // ayzek refuses --speed with --seedlink
  }

  void validation() {
    QTemporaryDir tmp;
    Options o;
    QVERIFY(!validate(o).isEmpty()); // no files
    o.files = {tmp.filePath("nope.mseed")};
    QVERIFY(validate(o).contains("nope.mseed"));
    QFile f(tmp.filePath("a.mseed"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    o.files = {f.fileName()};
    QCOMPARE(validate(o), QString());
    o.catalog = tmp.filePath("missing.csv");
    QVERIFY(!validate(o).isEmpty());
    o.catalog.clear();
    Options live;
    live.mode = Mode::Live;
    QVERIFY(!validate(live).isEmpty());
    live.server = "host:18000";
    QVERIFY(!validate(live).isEmpty()); // no streams
    live.streams = "KO.KULA";
#ifdef Q_OS_WIN
    QVERIFY(!validate(live).isEmpty()); // not in the Windows build
#else
    QCOMPARE(validate(live), QString());
#endif
  }

  void sites() {
    const QList<Site> s =
        parseSites("Istanbul,41.0082,28.9784\n\nbad line\n"
                   " Bursa , 40.1828 , 29.0667 \nX,1,notanumber");
    QCOMPARE(s.size(), 2);
    QCOMPARE(s[1].name, QString("Bursa"));
    QCOMPARE(s[1].lon, 29.0667);
    QCOMPARE(sitesText(s),
             QString("Istanbul,41.0082,28.9784\nBursa,40.1828,29.0667"));
  }

  void eventLines() {
    auto e = parseEventLine(
        "ALARM    #3   18:21:09.00  earthquake detected by KULA, SIMA  (AFAD "
        "MW 4.9 at 18:20:51.00, 18.0 s ago)");
    QVERIFY(e.has_value());
    QCOMPARE(e->tag, QString("ALARM"));
    QCOMPARE(e->id, 3);
    QCOMPARE(e->time, QString("18:21:09.00"));
    QVERIFY(e->text.startsWith("earthquake detected by KULA, SIMA"));
    e = parseEventLine("PDMAG    #3   18:21:12.50  Pd magnitude M4.7 ± 0.2 "
                       "(M<4 0%, 4-5.5 100%, M>=5.5 0%) from 3 stations");
    QVERIFY(e.has_value());
    QCOMPARE(e->tag, QString("PDMAG"));
    QVERIFY(e->text.startsWith("Pd magnitude M4.7"));
    // Colour codes, should ayzek ever think it writes to a terminal.
    e = parseEventLine("\x1b[1;31mALARM   \x1b[0m #1   10:00:00.00  x");
    QVERIFY(e.has_value());
    QCOMPARE(e->id, 1);
    QVERIFY(!parseEventLine("  correct  (match an AFAD event)   3  75%")
                 .has_value());
    QVERIFY(!parseEventLine("ayzek: unknown option --foo").has_value());
    QVERIFY(!parseEventLine("").has_value());
  }

  void summary() {
    const QStringList log = {
        "ALARM    #1   18:21:09.00  earthquake detected by KULA, SIMA",
        "PLOC     #1   18:21:10.50  P times locate it at 39.240N 28.119E",
        "ALARM    #2   18:40:00.00  earthquake detected by BAND, MANT",
        "",
        "  Alarms compared with the AFAD catalogue",
        "    correct  (match an AFAD event)        1   50%",
        "    false    (no AFAD event)              1   50%",
        "",
        "  AFAD events within 250 km                9",
        "    detected                              8   89%",
        "    detected before S                     7   78%",
        "    missed                                1   11%",
        "",
        "  By magnitude      AFAD events   detected   missed",
    };
    const Summary s = parseSummary(log);
    QVERIFY(s.any());
    QCOMPARE(s.alarms, 2);
    QCOMPARE(s.correct, 1);
    QCOMPARE(s.falseAlarms, 1);
    QCOMPARE(s.events, 9);
    QCOMPARE(s.detected, 8);
    QCOMPARE(s.detectedBeforeS, 7);
    QCOMPARE(s.missed, 1);
    const Summary none = parseSummary({"usage: ayzek [options]"});
    QVERIFY(!none.any());
    QCOMPARE(none.events, -1);
  }
};

QTEST_GUILESS_MAIN(TstAyzek)
#include "tst_ayzek.moc"
