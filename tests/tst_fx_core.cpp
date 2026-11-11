// The feature-extraction port, first slice: config, miniSEED decoding,
// filter and FFT, the statistical and chaotic features, npy files. Reference
// values come from two places: sismokaos-cli's own test constants (Rust),
// and tests/data/fx/reference.json, produced by the Python tool's
// environment on the same deterministic signals.
#include <QtTest>

#include <cmath>
#include <numbers>

#include "fx/config.h"
#include "fx/dsp.h"
#include "fx/features.h"
#include "fx/mseed.h"
#include "fx/npy.h"

using namespace skg::fx;

namespace {
const QString kData = QStringLiteral(SKG_TEST_DATA "/fx/");

QJsonObject loadJson(const QString &name) {
  QFile f(kData + name);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

std::vector<double> zscore(std::vector<double> v, bool ddof1) {
  const double n = double(v.size());
  double mean = 0;
  for (double x : v)
    mean += x;
  mean /= n;
  double s = 0;
  for (double x : v)
    s += (x - mean) * (x - mean);
  const double sd = std::sqrt(s / (ddof1 ? n - 1 : n));
  for (double &x : v)
    x = (x - mean) / sd;
  return v;
}

// sismokaos-cli's reference_signal(): sin(i*0.13) + 0.5 sin(i*0.031), z-scored
// (ddof 1).
std::vector<double> rustReference() {
  std::vector<double> raw(1000);
  for (int i = 0; i < 1000; ++i)
    raw[i] = std::sin(i * 0.13) + 0.5 * std::sin(i * 0.031);
  return zscore(raw, true);
}

std::vector<double> logisticMap() {
  std::vector<double> raw(1000);
  double v = 0.3;
  for (double &r : raw) {
    v = 4.0 * v * (1.0 - v);
    r = v;
  }
  return zscore(raw, false);
}

// tests/data/fx/reference.json's signal: sines plus a logistic map at fs.
std::vector<double> pySignal(int n, double fs) {
  std::vector<double> x(n);
  double r = 0.4;
  for (int i = 0; i < n; ++i) {
    const double t = i / fs;
    r = 3.9 * r * (1 - r);
    x[i] = std::sin(2 * std::numbers::pi * 0.3 * t) +
           0.5 * std::sin(2 * std::numbers::pi * 1.7 * t + 1.0) +
           0.3 * (r - 0.5);
  }
  return x;
}
} // namespace

class TstFxCore : public QObject {
  Q_OBJECT
  QJsonObject ref_;

private slots:
  void initTestCase() {
    ref_ = loadJson("reference.json");
    QVERIFY2(!ref_.isEmpty(), "tests/data/fx/reference.json missing");
    const QJsonArray first = ref_.value("signal_first10").toArray();
    const auto x = pySignal(1000, 5.0);
    for (int i = 0; i < 10; ++i)
      QVERIFY2(std::abs(x[i] - first[i].toDouble()) < 1e-12,
               "test signal differs from Python's");
  }

  // --- config -----------------------------------------------------------
  void configRoundTripAndAliases() {
    Config c;
    c.station = "BODT";
    c.chaosSampentTol = std::array<double, 3>{1, 2, 3};
    Band b;
    b.freqmin = 1;
    b.freqmax = 5;
    b.fs = 25;
    b.winSec = 40;
    b.stepSec = 50;
    b.staSec = 1;
    b.ltaSec = 20;
    c.bands << b;
    const Config back = Config::fromJson(c.toJson());
    QCOMPARE(back.station, QString("BODT"));
    QVERIFY(back.chaosSampentTol);
    QCOMPARE((*back.chaosSampentTol)[2], 3.0);
    QCOMPARE(back.bands.size(), 1);
    QCOMPARE(back.bands[0].label(), QString("1-5Hz"));
    QCOMPARE(back.calibration.maxDistKm, 300.0);
    // The Python tool's config.json keys
    const Config py =
        Config::fromJson(QJsonDocument::fromJson(R"({"STATION":"ELZG","Fs":5.0,
      "WIN_SEC":200,"STEP_SEC":50,"STA_SEC":0.5,"LTA_SEC":60,"FREQMIN":0.1,"FREQMAX":2.0,
      "CHAOS_TAU":7,"CHAOS_DIM":4})")
                             .object());
    QCOMPARE(py.station, QString("ELZG"));
    QCOMPARE(py.chaosTau, 7);
    QCOMPARE(py.chaosDim, 4);
    QCOMPARE(py.winSize(), 1000);
    QCOMPARE(py.nlta(), 300);
  }

  void configValidation() {
    Config c;
    QVERIFY(c.validate().isEmpty());
    c.freqmax = 2.6; // above Nyquist at 5 Hz
    QVERIFY(!c.validate().isEmpty());
    c.freqmax = 2.0;
    c.chaosRosRise = {0.9, 0.1};
    QCOMPARE(c.validate().size(), 1);
    c.chaosRosRise = {0.1, 0.9};
    QVERIFY(c.checkDecimation(100.0).isEmpty());
    QVERIFY(c.checkDecimation(30.0).isEmpty()); // 30/5 = 6, exact
    c.fs = 30.0;
    c.freqmax = 2.0;
    QVERIFY(!c.checkDecimation(100.0).isEmpty()); // 100/30 -> 33.3 Hz
    Band b1, b2;
    b1.freqmin = b2.freqmin = 1;
    b1.freqmax = b2.freqmax = 5;
    b1.fs = b2.fs = 25;
    b1.winSec = b2.winSec = 40;
    b1.stepSec = b2.stepSec = 50;
    b1.staSec = b2.staSec = 1;
    b1.ltaSec = b2.ltaSec = 20;
    c.fs = 5;
    c.bands << b1 << b2;
    QVERIFY(c.validate().join(' ').contains("unique"));
    c.bands.removeLast();
    c.outputRoot = "/out";
    const auto runs = c.bandRuns();
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs[0].band, QString("1-5Hz"));
    QCOMPARE(runs[0].outputRoot, QString("/out/1-5Hz"));
    QCOMPARE(runs[0].spectralSplitHz, std::sqrt(5.0));
    c.bandFilter << "nope";
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, c.bandRuns());
  }

  // --- miniSEED ---------------------------------------------------------
  void decodesEveryEncoding() {
    const QJsonObject exp = loadJson("gapped_expected.json");
    QVERIFY(!exp.isEmpty());
    for (const char *name : {"gapped_steim1", "gapped_steim2", "gapped_int32",
                             "gapped_float64", "gapped_steim2_le"}) {
      const QString path = kData + name + ".mseed";
      ChannelTable table;
      const auto refs = indexFile(path, 0, table);
      QVERIFY2(refs.size() >= 5, name);
      // The index is kept small: a number for the channel, not its text.
      QVERIFY(sizeof(RecordRef) <= 32);
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadOnly));
      // Sum per channel across records, compared with obspy's per-trace sums.
      QMap<QString, double> sums;
      QMap<QString, int> counts;
      for (const RecordRef &r : refs) {
        f.seek(r.offset);
        const QByteArray rec = f.read(r.length);
        const RecordHeader h = parseHeader(rec);
        QVERIFY2(h.valid, name);
        QCOMPARE(h.sampleRate, 20.0);
        const auto samples = decodeRecord(rec, h);
        QCOMPARE(int(samples.size()), h.sampleCount);
        for (double v : samples)
          sums[table.channels[r.channel].id] += v;
        counts[table.channels[r.channel].id] += int(samples.size());
      }
      const QJsonObject channels = exp.value("channels").toObject();
      for (auto it = channels.begin(); it != channels.end(); ++it) {
        double want = 0;
        int n = 0;
        for (const QJsonValue &t : it->toArray()) {
          want += t.toObject().value("sum").toDouble();
          n += t.toObject().value("n").toInt();
        }
        QVERIFY2(sums.contains(it.key()),
                 qPrintable(QString(name) + " " + it.key()));
        QCOMPARE(counts[it.key()], n);
        QVERIFY2(std::abs(sums[it.key()] - want) < 1e-6,
                 qPrintable(QString("%1 %2: %3 vs %4")
                                .arg(name, it.key())
                                .arg(sums[it.key()])
                                .arg(want)));
      }
    }
    // int16 fixture holds the data divided by 10
    ChannelTable table16;
    const auto refs16 = indexFile(kData + "gapped_int16.mseed", 0, table16);
    QVERIFY(!refs16.empty());
    QFile f16(kData + "gapped_int16.mseed");
    QVERIFY(f16.open(QIODevice::ReadOnly));
    f16.seek(refs16.front().offset);
    const QByteArray rec = f16.read(refs16.front().length);
    const RecordHeader h = parseHeader(rec);
    QCOMPARE(h.encoding, 1);
    QCOMPARE(int(decodeRecord(rec, h).size()), h.sampleCount);
  }

  void recordTimesAndGaps() {
    const QJsonObject exp = loadJson("gapped_expected.json");
    ChannelTable table;
    const auto refs = indexFile(kData + "gapped_steim2.mseed", 0, table);
    double hhzStart = 1e30, hhzEnd = -1e30;
    for (const RecordRef &r : refs)
      if (table.channels[r.channel].id.endsWith("HHZ")) {
        hhzStart = std::min(hhzStart, r.startEpoch);
        hhzEnd = std::max(hhzEnd, r.startEpoch +
                                      (r.count - 1) /
                                          table.channels[r.channel].rate);
      }
    // 2020-01-24T00:00:00Z
    QCOMPARE(hhzStart, double(daysFromCivil(2020, 1, 24)) * 86400.0);
    // three 10 s traces, 1 s and 3 s gaps: last sample at 34 s minus 1/fs...
    QVERIFY(std::abs((hhzEnd - hhzStart) - (10 + 1 + 10 + 3 + 10 - 1.0 / 20)) <
            1e-6);
    QCOMPARE(exp.value("gaps").toArray().size(), 2);
    QCOMPARE(table.channels[refs.front().channel].component, 'E');
  }

  // --- filter and FFT ------------------------------------------------------
  void bandpassIsButterworth() {
    for (const auto &[fs, lo, hi] :
         std::vector<std::array<double, 3>>{{100, 0.02, 0.1},
                                            {100, 0.1, 1.0},
                                            {100, 0.1, 2.0},
                                            {100, 1, 5},
                                            {100, 5, 20},
                                            {5, 0.1, 2}}) {
      const BandPass f(4, fs, lo, hi);
      auto warp = [&](double x) {
        return 2 * fs * std::tan(std::numbers::pi * x / fs);
      };
      for (double freq :
           {lo / 4, lo, std::sqrt(lo * hi), hi, std::min(hi * 2, fs / 2.1)}) {
        const double w = warp(freq), wl = warp(lo), wh = warp(hi);
        const double xx = (w * w - wl * wh) / (w * (wh - wl));
        const double ideal = 1.0 / std::sqrt(1.0 + std::pow(xx, 8));
        QVERIFY2(std::abs(f.magnitude(freq, fs) - ideal) < 1e-9,
                 qPrintable(
                     QStringLiteral("%1-%2 at %3").arg(lo).arg(hi).arg(freq)));
      }
    }
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, BandPass(4, 5.0, 0.1, 2.6));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, BandPass(3, 5.0, 0.1, 2.0));
  }

  void filtfiltIsZeroPhaseAndMatchesScipySections() {
    const double fs = 100;
    const BandPass f(4, fs, 0.1, 2.0);
    std::vector<double> x(60000);
    for (size_t i = 0; i < x.size(); ++i)
      x[i] = std::sin(2 * std::numbers::pi * 0.5 * i / fs);
    const auto y = f.filtfilt(x, 5000);
    for (size_t i = 20000; i < 40000; i += 997)
      QVERIFY2(std::abs(y[i] - x[i]) < 1e-3, qPrintable(QString::number(i)));
    // scipy's sos for the same design: the cascaded response must agree.
    const QJsonArray sos = ref_.value("sos").toArray();
    QCOMPARE(sos.size(), 4);
    auto scipyMag = [&](double freq) {
      const double w = 2 * std::numbers::pi * freq / fs;
      const std::complex<double> z1(std::cos(w), -std::sin(w));
      const std::complex<double> z2 = z1 * z1;
      double r = 1;
      for (const QJsonValue &s : sos) {
        const QJsonArray c = s.toArray();
        const std::complex<double> num =
            c[0].toDouble() + c[1].toDouble() * z1 + c[2].toDouble() * z2;
        const std::complex<double> den =
            c[3].toDouble() + c[4].toDouble() * z1 + c[5].toDouble() * z2;
        r *= std::abs(num / den);
      }
      return r;
    };
    for (double freq : {0.05, 0.1, 0.5, 1.0, 2.0, 5.0})
      QVERIFY2(std::abs(f.magnitude(freq, fs) - scipyMag(freq)) < 1e-9,
               qPrintable(QString::number(freq)));
    QCOMPARE(settleSeconds(0.1), 50.0);
  }

  void fftMatchesNumpyOnOddLength() {
    const auto xn = zscore(pySignal(1000, 5.0), true);
    const auto p = powerSpectrum(xn);
    QCOMPARE(p.size(), size_t(501));
    const QJsonArray p05 = ref_.value("power_0_5").toArray();
    for (int i = 0; i < 5; ++i)
      QVERIFY2(std::abs(p[i] - p05[i].toDouble()) <
                   1e-9 * std::max(1.0, std::abs(p05[i].toDouble())),
               qPrintable(QString::number(i)));
    double sum = 0;
    for (double v : p)
      sum += v;
    QVERIFY(std::abs(sum - ref_.value("power_sum").toDouble()) < 1e-8 * sum);
    std::vector<double> freqs(501);
    for (int i = 0; i < 501; ++i)
      freqs[i] = i * 5.0 / 1000;
    QCOMPARE(dominantFrequency(p, freqs), ref_.value("dominant").toDouble());
    const auto [centroid, lf, hf] = spectralCentroidAndEnergy(freqs, p, 1.0);
    QVERIFY(std::abs(centroid - ref_.value("centroid").toDouble()) < 1e-9);
    QVERIFY(std::abs(lf - ref_.value("lf").toDouble()) < 1e-12);
    QVERIFY(std::abs(lf + hf - 1.0) < 1e-12);
    // power of two still works and a tiny transform too
    QCOMPARE(powerSpectrum(std::vector<double>(1024, 1.0))[0], 1024.0);
    QCOMPARE(powerSpectrum({1.0, 2.0, 3.0}).size(), size_t(2));
  }

  // --- statistics vs the Python tool ---------------------------------------
  void statisticsMatchPython() {
    const auto x = pySignal(1000, 5.0);
    const auto xn = zscore(x, true);
    const Moments m = moments(x);
    QVERIFY(std::abs(m.m3 / std::pow(m.var, 1.5) -
                     ref_.value("skew").toDouble()) < 1e-10);
    QVERIFY(std::abs(m.m4 / (m.var * m.var) - ref_.value("kurt").toDouble()) <
            1e-10);
    QVERIFY(std::abs(rms(x) - ref_.value("rms").toDouble()) < 1e-12);
    QVERIFY(std::abs(zeroCrossingRate(x) - ref_.value("zcr").toDouble()) <
            6e-6); // Python rounds to 5 places
    const auto h = hjorth(x);
    const QJsonObject hj = ref_.value("hjorth").toObject();
    QVERIFY(std::abs(h[0] - hj.value("activity").toDouble()) < 1e-5);
    QVERIFY(std::abs(h[1] - hj.value("mobility").toDouble()) < 1e-5);
    QVERIFY(std::abs(h[2] - hj.value("complexity").toDouble()) < 1e-5);
    QVERIFY(std::abs(permutationEntropy(x) -
                     ref_.value("perm_entropy").toDouble()) < 1e-5);
    const StaLta sl = staLta(xn, 2, 300);
    const QJsonObject st = ref_.value("stalta").toObject();
    QVERIFY(std::abs(sl.max - st.value("sta_lta_max").toDouble()) < 1e-5);
    QVERIFY(std::abs(sl.mean - st.value("sta_lta_mean").toDouble()) < 1e-5);
    QVERIFY(std::abs(sl.median - st.value("sta_lta_median").toDouble()) < 1e-5);
    std::vector<double> y = pySignal(1000, 5.0);
    std::reverse(y.begin(), y.end());
    QVERIFY(std::abs(crossCorrelation(x, y) -
                     ref_.value("corrcoef").toDouble()) < 1e-12);
    QVERIFY(std::isnan(crossCorrelation(x, std::vector<double>(1000, 1.0))));
    QVERIFY(std::isnan(staLta(xn, 2, 2000).max));
  }

  void gapsAndDetrend() {
    std::vector<double> d(400);
    for (size_t i = 0; i < d.size(); ++i)
      d[i] = 100 * std::sin(i / 400.0 * 4 * std::numbers::pi);
    const auto clean = d;
    for (int gap : {1, 2, 5, 20, 100}) {
      auto g = clean;
      for (int k = 150; k < 150 + gap; ++k)
        g[k] = std::nan("");
      const auto filled = interpolateGaps(g);
      QCOMPARE(int(std::count(filled.begin(), filled.end(), true)), gap);
      double peakV = 0;
      for (int k = 150; k < 150 + gap; ++k)
        peakV = std::max(peakV, std::abs(g[k]));
      QVERIFY2(peakV <= 110.0, qPrintable(QString::number(gap)));
    }
    std::vector<double> edge = clean;
    edge[0] = edge[1] = edge[399] = std::nan("");
    const auto filled = interpolateGaps(edge);
    QVERIFY(std::isnan(edge[0]) && std::isnan(edge[1]) &&
            std::isnan(edge[399]));
    QVERIFY(
        std::none_of(filled.begin(), filled.end(), [](bool b) { return b; }));
    std::vector<double> ramp(1000);
    for (size_t i = 0; i < ramp.size(); ++i)
      ramp[i] = 5.0 + 0.25 * i;
    ramp[10] = ramp[500] = std::nan("");
    detrendLinear(ramp);
    QVERIFY(std::isnan(ramp[10]) && std::isnan(ramp[500]));
    for (double v : ramp)
      if (!std::isnan(v))
        QVERIFY(std::abs(v) < 1e-6);
    // scipy.signal.detrend on the fs=100 signal
    std::vector<double> z = pySignal(2000, 100.0);
    detrendLinear(z);
    const QJsonArray dt = ref_.value("detrend_linear_first3").toArray();
    for (int i = 0; i < 3; ++i)
      QVERIFY(std::abs(z[i] - dt[i].toDouble()) < 1e-9);
  }

  // --- chaos vs sismokaos-cli and the Python tool
  // ------------------------------
  void chaosMatchesRustReference() {
    const auto x = rustReference();
    QVERIFY(std::abs(wolfLye(x, 5.0, 5, 5, 5) - 0.3035492535457496) < 1e-10);
    const auto [s, l] =
        rosensteinFixedWindow(x, 5.0, 5, 5, {0.5, 5.0, 5.0, 20.0}, 1.0);
    QVERIFY2(std::abs(s - 0.38280772273173597) < 1e-10,
             qPrintable(QString::number(s, 'g', 17)));
    QVERIFY2(std::abs(l - -0.029816027419789917) < 1e-10,
             qPrintable(QString::number(l, 'g', 17)));
    QVERIFY(std::abs(sampleEntropy(x, 2, 0.2) - 0.39819536425420143) < 1e-9);
    QVERIFY2(
        std::abs(correlationDimension(x, 5, 5) - 2.3299697367683927) < 1e-10,
        qPrintable(QString::number(correlationDimension(x, 5, 5), 'g', 17)));
    QCOMPARE(amiTau(x, 20), std::optional<int>(15));
    QCOMPARE(amiTau(x, 40), std::optional<int>(15));
    QCOMPARE(amiTau(logisticMap(), 20), std::optional<int>(8));
    QVERIFY(!amiTau(std::vector<double>(100, 1.0), 20));
    QCOMPARE(fnnDim(x, 3, 10), std::optional<int>(3));
    QCOMPARE(fnnDim(x, 5, 10), std::optional<int>(3));
    QCOMPARE(fnnDim(x, 12, 10), std::optional<int>(4));
    QCOMPARE(fnnDim(logisticMap(), 1, 10), std::optional<int>(1));
    QVERIFY(!fnnDim(std::vector<double>(x.begin(), x.begin() + 20), 10, 8));
    // Absolute tolerance equals relative r at the own std; scaling changes it.
    double mean = 0;
    for (double v : x)
      mean += v;
    mean /= x.size();
    double ss = 0;
    for (double v : x)
      ss += (v - mean) * (v - mean);
    const double sd = std::sqrt(ss / (x.size() - 1.0));
    QCOMPARE(sampleEntropyTol(x, 2, 0.2 * sd), sampleEntropy(x, 2, 0.2));
    std::vector<double> loud = x;
    for (double &v : loud)
      v *= 3;
    QVERIFY(sampleEntropyTol(loud, 2, 0.2 * sd) !=
            sampleEntropyTol(x, 2, 0.2 * sd));
    // the logistic map is chaotic: ln 2 per iteration
    const auto [ls, ll] =
        rosensteinLye(logisticMap(), 1.0, 1, 2, 1.0, 20.0, {0.1, 0.9});
    QVERIFY2(ls > 0.4 && ls < 0.9, qPrintable(QString::number(ls)));
    Q_UNUSED(ll);
    // rise_fit: climbs one per step to 10, then flat
    std::vector<double> curve(31);
    for (int j = 0; j <= 30; ++j)
      curve[j] = std::min(double(j), 10.0);
    const auto [rs, rl] = riseFit(curve, 1.0, 1.0, {0.1, 0.9});
    QVERIFY(std::abs(rs - 1.0) < 1e-12);
    QVERIFY(rl > 0.0 && rl < 0.2);
    QVERIFY(std::abs(riseFit(curve, 1.0, 0.5, {0.1, 0.9}).first - 0.5) < 1e-12);
    QVERIFY(std::isnan(
        riseFit(std::vector<double>(20, 1.0), 1, 1, {0.1, 0.9}).first));
    const std::vector<double> shortV{1.0, 2.0, 3.0};
    QVERIFY(std::isnan(wolfLye(shortV, 5, 5, 5, 5)));
    QVERIFY(std::isnan(sampleEntropy(shortV, 2, 0.2)));
    QCOMPARE(correlationDimension(shortV, 5, 5), 0.0);
    QCOMPARE(pythonRound(2.5), 2.0);
    QCOMPARE(pythonRound(3.5), 4.0);
    QCOMPARE(pythonRound(2.4), 2.0);
  }

  void chaosMatchesPythonTool() {
    const auto xn = zscore(pySignal(1000, 5.0), true);
    QVERIFY2(std::abs(wolfLye(xn, 5.0, 5, 5, 5) -
                      ref_.value("wolf").toDouble()) < 1e-9,
             qPrintable(QString::number(wolfLye(xn, 5.0, 5, 5, 5), 'g', 17)));
    const auto [s, l] =
        rosensteinFixedWindow(xn, 5.0, 5, 5, {0.5, 5.0, 5.0, 20.0}, 1.0);
    QVERIFY2(std::abs(s - ref_.value("ros_short").toDouble()) < 1e-9,
             qPrintable(QString::number(s, 'g', 17)));
    QVERIFY2(std::abs(l - ref_.value("ros_long").toDouble()) < 1e-9,
             qPrintable(QString::number(l, 'g', 17)));
    QVERIFY(std::abs(sampleEntropy(xn, 2, 0.2) -
                     ref_.value("sampen").toDouble()) < 1e-9);
    QVERIFY2(
        std::abs(correlationDimension(xn, 5, 5) -
                 ref_.value("corrdim").toDouble()) < 1e-9,
        qPrintable(QString::number(correlationDimension(xn, 5, 5), 'g', 17)));
  }

  void computeWindowHasEveryFeature() {
    Config cfg;
    const auto e = pySignal(1000, 5.0), n = pySignal(1000, 5.0);
    std::vector<double> z = e;
    z[10] = std::nan("");
    const FeatureRow row = computeWindow(e, n, z, cfg);
    const QStringList names = featureNames(false);
    QCOMPARE(row.size(), names.size());
    QCOMPARE(names.size(), 3 + 3 * 21);
    QMap<QString, double> m;
    for (const auto &[k, v] : row)
      m.insert(k, v);
    QVERIFY(std::isnan(m["Z_RMS"])); // the gappy component is NaN throughout
    QVERIFY(std::isnan(m["Z_CORR_DIM"]));
    QVERIFY(std::isnan(m["EZ_CROSS_CORR"]));
    QVERIFY(std::abs(m["EN_CROSS_CORR"] - 1.0) < 1e-12);
    QVERIFY(m["E_RMS"] > 0 && std::isfinite(m["E_WOLF_LYE"]));
    QVERIFY(std::isfinite(m["N_STA_LTA_Max"]));
    cfg.chaosOnly = true;
    QCOMPARE(featureNames(true).size(), 15);
    QCOMPARE(computeWindow(e, n, z, cfg).size(), 15);
    cfg.chaosOnly = false;
    cfg.chaosMinSamples = 5000;
    const FeatureRow shortRow = computeWindow(e, n, e, cfg);
    for (const auto &[k, v] : shortRow)
      if (k.endsWith("WOLF_LYE") || k.endsWith("ROS_SHORT"))
        QVERIFY(std::isnan(v));
  }

  // --- npy -----------------------------------------------------------------
  void npyRoundTrip() {
    NpyTable t;
    t.fields = {{"Pencere_ID", "<U50"}, {"Zaman_Dk", "<f8"}, {"E_RMS", "<f8"}};
    t.rows = 3;
    t.strings = {{"a_w01", "a_w02", QString::fromUtf8("çok_uzun_w03")}, {}, {}};
    t.numeric = {{}, {1.5, 2.5, 3.5}, {0.1, std::nan(""), 0.3}};
    const QByteArray bytes = encodeNpy(t);
    QVERIFY(bytes.startsWith(QByteArray("\x93NUMPY", 6)));
    const int headerLen = quint8(bytes[8]) | (quint8(bytes[9]) << 8);
    QCOMPARE((10 + headerLen) % 64, 0); // the header is padded to 64 bytes
    const QByteArray dump = qgetenv("SKG_NPY_DUMP");
    if (!dump.isEmpty()) {
      QFile out(QString::fromLocal8Bit(dump));
      QVERIFY(out.open(QIODevice::WriteOnly));
      out.write(bytes);
    }
    const NpyTable back = decodeNpy(bytes);
    QCOMPARE(back.rows, 3);
    QCOMPARE(back.fields.size(), 3);
    QCOMPARE(back.value(2, 0).toString(), QString::fromUtf8("çok_uzun_w03"));
    QCOMPARE(back.value(1, 1).toDouble(), 2.5);
    QVERIFY(std::isnan(back.value(1, 2).toDouble()));
    QCOMPARE(back.value(2, 2).toDouble(), 0.3);
    const QByteArray win =
        encodeWindowNpy({{"E", {1, 2}}, {"N", {3}}, {"Z", {4, 5}}});
    const NpyTable w = decodeNpy(win);
    QCOMPARE(w.rows, 2);
    QVERIFY(std::isnan(w.value(1, 1).toDouble()));
    QCOMPARE(w.value(1, 2).toDouble(), 5.0);
  }
};

QTEST_GUILESS_MAIN(TstFxCore)
#include "tst_fx_core.moc"
#include <algorithm>
#include <stdexcept>
