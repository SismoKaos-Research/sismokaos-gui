// The feature-extraction port, second slice: catalog and exclusions, the
// streaming preprocessor, calibration, the window loop and the output
// writers, on a synthetic three-component station (tests/data/fx/enz, made
// with obspy: 10 Hz, 2 h 10 min, a 90 s gap in N, the last 10 min of E in a
// zip, a 40 s burst at 00:45 listed in catalog.csv).
#include <QtTest>

#include <cmath>
#include <numbers>

#include "fx/calibrate.h"
#include "fx/catalog.h"
#include "fx/config.h"
#include "fx/engine.h"
#include "fx/npy.h"
#include "fx/preprocess.h"

using namespace skg::fx;

// The station list lives in skg_tdvms's resources; a static library's
// resources are linked only when something asks for them.
static void initTdvmsData() { Q_INIT_RESOURCE(data); }

namespace {
const QString kData = QStringLiteral(SKG_TEST_DATA "/fx/enz");

std::vector<int> absoluteStarts(const std::vector<int> &chunks, int win,
                                int step) {
  int buffered = 0, drained = 0, pending = 0;
  std::vector<int> out;
  for (int n : chunks) {
    buffered += n;
    auto [starts, cursor] = windowStarts(buffered, win, step, pending);
    for (int s : starts)
      out.push_back(drained + s);
    pending = std::max(0, cursor - buffered);
    cursor = std::min(cursor, buffered);
    buffered -= cursor;
    drained += cursor;
  }
  return out;
}

QStringList csvLines(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
}

Config baseConfig(const QString &out) {
  Config c;
  c.station = QStringLiteral("SYN1");
  c.fs = 5.0;
  c.winSec = 200;
  c.stepSec = 200; // non-overlapping: the chaos features are O(n^2) per window
  c.freqmin = 0.1;
  c.freqmax = 2.0;
  c.dataDir = kData;
  c.outputRoot = out;
  c.calibration.catalog = kData + QStringLiteral("/catalog.csv");
  c.calibration.stationCoords = kData + QStringLiteral("/stations.csv");
  c.calibration.minMag = 2.0;
  return c;
}
} // namespace

class TstFxPipeline : public QObject {
  Q_OBJECT
private slots:
  void windowStartsStepLongerThanWindow() {
    const std::vector<int> chunks{1100, 1300, 90000, 77, 1249, 90000, 3};
    int total = 0;
    for (int c : chunks)
      total += c;
    const std::vector<int> got = absoluteStarts(chunks, 1000, 1250);
    std::vector<int> want;
    for (int s = 0; s + 1000 <= total; s += 1250)
      want.push_back(s);
    QVERIFY(got == want);
  }

  void windowStartsOverlapping() {
    const std::vector<int> chunks{5000, 4321, 999, 12000};
    int total = 0;
    for (int c : chunks)
      total += c;
    const std::vector<int> got = absoluteStarts(chunks, 1000, 250);
    std::vector<int> want;
    for (int s = 0; s + 1000 <= total; s += 250)
      want.push_back(s);
    QVERIFY(got == want);
  }

  void percentileMatchesNumpy() {
    QCOMPARE(percentile({1, 2, 3, 4}, 50.0), 2.5);
    QCOMPARE(percentile({1, 2, 3, 4}, 25.0), 1.75);
    QCOMPARE(percentile({7}, 90.0), 7.0);
    QVERIFY(std::isnan(percentile({}, 50.0)));
  }

  void meanFrequencyOfASine() {
    std::vector<double> x(2000);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] = std::sin(2 * std::numbers::pi * 0.7 * double(i) / 5.0);
    QVERIFY(std::abs(meanFrequency(x, 5.0) - 0.7) < 0.02);
  }

  void catalogAndExclusions() {
    const auto events = loadCatalog(kData + QStringLiteral("/catalog.csv"));
    QCOMPARE(int(events.size()), 2);
    const auto t = parseAfadTime(QStringLiteral("05/03/2024 00:45:00"));
    QVERIFY(t.has_value());
    QCOMPARE(events[0].epoch, *t);
    QCOMPARE(events[0].mag, 3.2);
    const auto pos = stationCoords(kData + QStringLiteral("/stations.csv"),
                                   QStringLiteral("SYN1"));
    QVERIFY(pos.has_value());
    QCOMPARE(pos->first, 38.60);
    QCOMPARE(pos->second, 39.20);
    QVERIFY(!stationCoords(kData + QStringLiteral("/stations.csv"),
                           QStringLiteral("NOPE")));
    QVERIFY(std::abs(haversineKm(38.60, 39.20, 38.60, 39.30) - 8.72) < 0.1);

    CalibrationConfig opts;
    opts.minMag = 2.0;
    const Exclusions ex(events, *pos, opts);
    QCOMPARE(ex.size(), 1); // the far event is beyond max_dist_km
    QVERIFY(ex.overlaps(*t - 60, *t - 50));   // inside pre_sec
    QVERIFY(ex.overlaps(*t + 200, *t + 210)); // inside min_post_sec
    QVERIFY(!ex.overlaps(*t - 1000, *t - 900));
    QCOMPARE(ex.countWithin(*t - 86400, *t + 86400), 1);
    // M3.2 coda: 3 * 10^((3.2 + 0.87) / 2) = 324 s > min_post 300.
    QVERIFY(std::abs(postEventSec(3.2, opts) -
                     3.0 * std::pow(10.0, (3.2 + 0.87) / 2.0)) < 1e-9);
  }

  void embeddedStationList() {
    initTdvmsData();
    // The AFAD list the tdvms port embeds serves as the default station list.
    const auto pos = stationCoords(QStringLiteral(":/tdvms/data/stations.csv"),
                                   QStringLiteral("ADVT"));
    QVERIFY(pos.has_value());
    QVERIFY(std::abs(pos->first - 40.4332) < 1e-6);
    QVERIFY(std::abs(pos->second - 29.7383) < 1e-6);
  }

  void processComponentKeepsLengthAndMask() {
    std::vector<double> x(4000);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] = std::sin(2 * std::numbers::pi * 0.5 * double(i) / 10.0);
    for (size_t i = 1000; i < 1010; ++i)
      x[i] = std::nan("");
    auto [y, mask] = processComponent(x, 0.1, 2.0, 10.0, 2, 500, 3500);
    QCOMPARE(int(y.size()), 1500);
    QCOMPARE(int(mask.size()), 1500);
    int fabricated = 0;
    for (bool b : mask)
      fabricated += b;
    QCOMPARE(fabricated, 5); // 10 interpolated samples, every second one kept
    for (double v : y)
      QVERIFY(std::isfinite(v));
  }

  void preprocessStreamsTheStation() {
    const QTemporaryDir out;
    Config cfg = baseConfig(out.path());
    PreprocessOptions opt;
    opt.stagingRoot = out.path();
    QStringList log;
    opt.log = [&](const QString &m) { log << m; };
    int samples = 0, interpN = 0, nanE = 0;
    double firstEpoch = -1, lastEnd = -1;
    QVERIFY(preprocessDirectory(cfg.dataDir, cfg, opt, [&](ChannelChunk &&c) {
      QCOMPARE(c.fs, 5.0);
      QCOMPARE(c.e.size(), c.n.size());
      QCOMPARE(c.e.size(), c.z.size());
      if (firstEpoch < 0)
        firstEpoch = c.startEpoch;
      else
        QVERIFY(std::abs(c.startEpoch - lastEnd) < 1e-6);
      lastEnd = c.startEpoch + double(c.e.size()) / c.fs;
      samples += int(c.e.size());
      for (bool b : c.nInterp)
        interpN += b;
      for (double v : c.e)
        nanE += std::isnan(v);
    }));
    // 2 h 10 min at 5 Hz, the zipped tail included.
    QCOMPARE(samples, 7800 * 5);
    QCOMPARE(nanE, 0);
    QCOMPARE(interpN, 90 * 5);
    QVERIFY(std::abs(firstEpoch - 1709596800.0) < 1e-6);
    QVERIFY(!QDir(out.path())
                 .entryList({QStringLiteral(".unzipped-*")},
                            QDir::Dirs | QDir::Hidden)
                 .size());
  }

  void featureWriterColumnsAndDev() {
    FeatureWriter w({QStringLiteral("A"), QStringLiteral("B")});
    w.addWindow(QStringLiteral("x_w01"), 1.0,
                {{QStringLiteral("A"), 1.0}, {QStringLiteral("B"), 2.0}},
                {0, 0, 0.5});
    w.addWindow(
        QStringLiteral("x_w02"), 2.0,
        {{QStringLiteral("A"), 1.5}, {QStringLiteral("B"), std::nan("")}},
        {0, 0, 0});
    w.addWindow(QStringLiteral("x_w03"), 3.0,
                {{QStringLiteral("A"), 1.0}, {QStringLiteral("B"), 4.0}},
                {0, 0, 0});
    QCOMPARE(w.columns(),
             QStringList({"Pencere_ID", "Zaman_Dk", "A", "B", "A_DEV", "B_DEV",
                          "E_INTERP_FRAC", "N_INTERP_FRAC", "Z_INTERP_FRAC"}));
    const QTemporaryDir out;
    const QString base = out.path() + QStringLiteral("/x_features");
    Config cfg;
    w.finish(base, cfg);
    const QStringList lines = csvLines(base + QStringLiteral(".csv"));
    QCOMPARE(lines.size(), 4);
    QCOMPARE(
        lines[1],
        QStringLiteral(
            "x_w01,1.000000,1.000000,2.000000,,,0.000000,0.000000,0.500000"));
    QCOMPARE(
        lines[2],
        QStringLiteral(
            "x_w02,2.000000,1.500000,,0.500000,,0.000000,0.000000,0.000000"));
    QCOMPARE(lines[3], QStringLiteral("x_w03,3.000000,1.000000,4.000000,-0."
                                      "500000,,0.000000,0.000000,0.000000"));
    const NpyTable t = readNpy(base + QStringLiteral(".npy"));
    QCOMPARE(t.rows, 3);
    QCOMPARE(t.fields.size(), 9);
    QCOMPARE(t.value(1, 0).toString(), QStringLiteral("x_w02"));
    QCOMPARE(t.value(2, 4).toDouble(), -0.5);
    QVERIFY(std::isnan(t.value(0, 4).toDouble()));
    QVERIFY(
        QFileInfo::exists(out.path() + QStringLiteral("/x_run_metadata.json")));
  }

  void rawBinaryWriterSegmentsAndNan() {
    const QTemporaryDir out;
    Config cfg;
    {
      RawBinaryWriter w(out.path() + QStringLiteral("/a"));
      w.writeChunk({1, 2}, {3, 4}, {5, 6}, 5.0, 1000.0);
      w.writeChunk({7}, {8}, {9}, 5.0, 1000.4);            // continues
      w.writeChunk({std::nan("")}, {1}, {1}, 5.0, 4600.0); // an hour later
      QCOMPARE(w.finish(cfg), qint64(4));
      QFile f(w.path());
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray b = f.readAll();
      QCOMPARE(b.size(), 4 * 3 * 4);
      const float *v = reinterpret_cast<const float *>(b.constData());
      QCOMPARE(v[0], 1.0f);
      QCOMPARE(v[1], 3.0f);
      QCOMPARE(v[2], 5.0f);
      QCOMPARE(v[6], 7.0f);
      QVERIFY(std::isnan(v[9]));
      QFile side(w.path() + QStringLiteral(".json"));
      QVERIFY(side.open(QIODevice::ReadOnly));
      const QJsonObject meta = QJsonDocument::fromJson(side.readAll()).object();
      QCOMPARE(meta.value("segments").toArray().size(), 2);
      QCOMPARE(meta.value("segments")
                   .toArray()
                   .at(1)
                   .toObject()
                   .value("sample_offset")
                   .toInt(),
               3);
      QCOMPARE(meta.value("nan_counts").toObject().value("E").toInt(), 1);
      QCOMPARE(meta.value("dtype").toString(), QStringLiteral("<f4"));
    }
  }

  void preprocessOnlyRun() {
    const QTemporaryDir out;
    const Config cfg = baseConfig(out.path());
    RunHooks hooks;
    const RunResult r = runPreprocessOnly(cfg, hooks);
    QVERIFY(!r.stopped);
    QCOMPARE(r.samples, qint64(7800 * 5));
    QCOMPARE(QFileInfo(r.rawF32).size(), qint64(7800 * 5 * 12));
    QVERIFY(QFileInfo::exists(r.rawSidecar));
    QVERIFY(r.rawF32.endsWith(QStringLiteral("/enz_preprocessed.f32")));
  }

  void fullRunCalibratesAndWrites() {
    const QTemporaryDir out;
    Config cfg = baseConfig(out.path());
    QStringList log;
    RunHooks hooks;
    hooks.log = [&](const QString &m) { log << m; };
    QStringList phases;
    hooks.phase = [&](const QString &p) { phases << p; };
    const RunResult r = runPipeline(cfg, hooks);
    QVERIFY(!r.stopped);
    QCOMPARE(phases.size(), 2);
    // 7800 s of data at 5 Hz, 200 s windows every 200 s.
    QCOMPARE(r.windows, (7800 - 200) / 200 + 1);
    QVERIFY(r.featuresCsv.endsWith(QStringLiteral("/enz_features.csv")));
    QVERIFY(QFileInfo::exists(r.featuresNpy));
    QVERIFY(QFileInfo::exists(r.metadata));
    QVERIFY(QFileInfo::exists(r.calibration));

    const Calibration cal = Calibration::load(r.calibration);
    QCOMPARE(cal.station, QStringLiteral("SYN1"));
    QCOMPARE(cal.fs, 5.0);
    QVERIFY(cal.tau >= 1);
    QVERIFY(cal.dim >= 1);
    QCOMPARE(cal.exclusionIntervals, 1);
    QCOMPARE(cal.windowsTotal, 7800 / 200);
    // The burst's exclusion and the gap remove windows: quiet < total.
    QVERIFY(cal.windowsQuiet < cal.windowsTotal);
    QVERIFY(cal.windowsQuiet >= 10);
    for (double tol : cal.sampentTol)
      QVERIFY(tol > 0);

    const NpyTable t = readNpy(r.featuresNpy);
    QCOMPARE(t.rows, r.windows);
    const QStringList names = featureNames(false);
    QCOMPARE(t.fields.size(), 2 + 2 * names.size() + 3);
    // Zaman_Dk advances by the step; window ids are numbered from 1.
    QCOMPARE(t.value(0, 0).toString(), QStringLiteral("enz_w01"));
    // Zaman_Dk is an epoch in minutes (~2.8e7), so the difference carries
    // rounding.
    QVERIFY(std::abs(t.value(1, 1).toDouble() - t.value(0, 1).toDouble() -
                     200.0 / 60.0) < 1e-6);
    // The N gap (01:20:00-01:21:30) is fabricated: windows over it carry an
    // N_INTERP_FRAC, the others none.
    const int nInterpCol = t.fields.size() - 2;
    int flagged = 0;
    for (int r2 = 0; r2 < t.rows; ++r2)
      flagged += t.value(r2, nInterpCol).toDouble() > 0;
    QVERIFY(flagged >= 1 && flagged <= 2);
    for (int r2 = 0; r2 < t.rows; ++r2) {
      QCOMPARE(t.value(r2, nInterpCol - 1).toDouble(), 0.0);
      QCOMPARE(t.value(r2, nInterpCol + 1).toDouble(), 0.0);
    }
    // The metadata carries the calibrated parameters.
    const Config meta = Config::load(r.metadata);
    QCOMPARE(meta.chaosTau, cal.tau);
    QCOMPARE(meta.chaosDim, cal.dim);
    QVERIFY(meta.chaosSampentTol.has_value());

    // A second run that reuses the calibration skips the calibration phase.
    const QTemporaryDir out2;
    Config cfg2 = baseConfig(out2.path());
    cfg2.calibrationFile = r.calibration;
    phases.clear();
    const RunResult r2 = runPipeline(cfg2, hooks);
    QCOMPARE(phases, QStringList{QStringLiteral("Extracting features")});
    QCOMPARE(r2.windows, r.windows);
    const QStringList a = csvLines(r.featuresCsv), b = csvLines(r2.featuresCsv);
    QCOMPARE(a, b);
  }

  void runStopsCooperatively() {
    const QTemporaryDir out;
    Config cfg = baseConfig(out.path());
    cfg.calibrationFile = QString();
    RunHooks hooks;
    int calls = 0;
    hooks.shouldStop = [&] { return ++calls > 3; };
    bool threw = false;
    RunResult r;
    try {
      r = runPipeline(cfg, hooks);
    } catch (const std::runtime_error &) {
      threw = true; // the calibration pass reports a stop as an error
    }
    QVERIFY(threw || r.stopped);
    QVERIFY(
        !QFileInfo::exists(out.path() + QStringLiteral("/enz_features.csv")));
  }
};

QTEST_GUILESS_MAIN(TstFxPipeline)
#include "tst_fx_pipeline.moc"
#include <algorithm>
#include <stdexcept>
