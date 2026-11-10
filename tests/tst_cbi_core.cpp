// The indicators library against indicators-forecast's tests: durations
// and window bounds, the weighting windows, the per-window indicators
// (b-value recovery on binned GR magnitudes, NaN for undefined values),
// Gardner-Knopoff windows and declustering, rate-matching homogenisation,
// the shipped conversion and fault zone, event markers and risk labels,
// smoothing, the CSV round trip, and the regression against the tables the
// ORIGINAL tool wrote for the synthetic catalog (tests/data/cbi).
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <random>

#include "cbi/catalog.h"
#include "cbi/decluster.h"
#include "cbi/indicators.h"
#include "cbi/magnitudes.h"
#include "cbi/table.h"
#include "cbi/windows.h"
#include "cbi/zones.h"

using namespace skg::cbi;

namespace {
QDateTime utc(const char *iso) {
  QDateTime dt = QDateTime::fromString(QString::fromLatin1(iso), Qt::ISODate);
  dt.setTimeZone(QTimeZone::utc());
  return dt;
}
double ep(const char *iso) { return toEpoch(utc(iso)); }

Catalog
events(std::initializer_list<std::tuple<const char *, double, double, double>>
           rows) {
  Catalog c;
  for (const auto &[t, lon, lat, m] : rows) {
    skg::analysis::Event e;
    e.epoch = ep(t);
    e.lon = lon;
    e.lat = lat;
    e.mag = m;
    c.events.push_back(e);
  }
  std::stable_sort(
      c.events.begin(), c.events.end(),
      [](const auto &a, const auto &b) { return a.epoch < b.epoch; });
  return c;
}

// Continuous GR magnitudes above the lower bin edge, rounded to binWidth.
void grCatalog(int n, double b, double mc, double binWidth, unsigned seed,
               std::vector<double> &mags, std::vector<double> &times) {
  std::mt19937_64 rng(seed);
  std::exponential_distribution<double> ex(b * std::log(10.0));
  std::uniform_int_distribution<long> ut(0, 365L * 86400 - 1);
  mags.clear();
  times.clear();
  for (int i = 0; i < n; ++i) {
    mags.push_back(roundDecimals(
        std::nearbyint(((mc - binWidth / 2) + ex(rng)) / binWidth) * binWidth,
        4));
    times.push_back(ep("2020-01-01") + double(ut(rng)));
  }
  std::sort(times.begin(), times.end());
}
} // namespace

class TstCbiCore : public QObject {
  Q_OBJECT
private slots:
  void durationParses();
  void windowBoundsHalfOpenInsideRange();
  void weightingWindowsRise();
  void bValueRecovered_data();
  void bValueRecovered();
  void emptyWindowIsNaN();
  void singleEventWindow();
  void halfGaussEnergyEqualsRectangularForEqualMagnitudes();
  void eventsOnWindowEndBelongToNext();
  void gkWindowsMatchPublishedFits();
  void gardnerKnopoffRemovesForeAndAftershocksOnly();
  void rateMatchingRecoversStretchAndShift();
  void conversionAppliesByPeriod();
  void shippedConversionIsMonotone();
  void maxCurvatureMcOnSynthetic();
  void distanceToMeridianSegment();
  void knownEafzEarthquakesLieOnTraces();
  void farAwayEventExcluded();
  void unknownStrandRejected();
  void eafzFileIsTheGemClipAndSelectsAsBefore();
  void shippedFaultLayerLoads();
  void dataFileLookup();
  void eachLargeEventMarkedOnceInFirstWindow();
  void newEventMarkedWhileLargerOldOneInView();
  void riskLabelAnchoredOnWindowEnd();
  void movingAverageAndBollinger();
  void pandasNumbers();
  void csvRoundTrip();
  void selectionTag();
  void legacyRegression_data();
  void legacyRegression();
};

void TstCbiCore::durationParses() {
  const auto d = Duration::parse(QStringLiteral(" 12 m "));
  QVERIFY(d.has_value());
  QCOMPARE(d->amount, 12);
  QCOMPARE(d->unit, QLatin1Char('M'));
  QCOMPARE(d->shortName(), QStringLiteral("12ay"));
  QCOMPARE(d->label(QStringLiteral("tr")), QStringLiteral("12 Ay"));
  QCOMPARE((Duration{15, QLatin1Char('D')}).shortName(),
           QStringLiteral("15gun"));
  for (const char *bad : {"12", "0M", "3X", "M3"})
    QVERIFY2(!Duration::parse(QLatin1String(bad)).has_value(), bad);
}

void TstCbiCore::windowBoundsHalfOpenInsideRange() {
  const auto b = windowBounds(utc("2020-01-15"), utc("2021-01-15"),
                              Duration{6, QLatin1Char('M')},
                              Duration{1, QLatin1Char('M')});
  QCOMPARE(int(b.size()), 7);
  QCOMPARE(b.front().first, utc("2020-01-15"));
  QCOMPARE(b.front().second, utc("2020-07-15"));
  QVERIFY(b.back().second <= utc("2021-01-15"));
  // Month ends clamp as pandas' DateOffset does.
  QCOMPARE((Duration{1, QLatin1Char('M')}).addTo(utc("2020-01-31")),
           utc("2020-02-29"));
}

void TstCbiCore::weightingWindowsRise() {
  for (const auto &w : {halfGaussWindow(20), halfHammingWindow(20)}) {
    double s = 0;
    for (double v : w)
      s += v;
    QVERIFY(std::abs(s - 1.0) < 1e-12);
    for (size_t i = 1; i < w.size(); ++i)
      QVERIFY(w[i] > w[i - 1]);
  }
  QCOMPARE(rectangularWindow(3), (std::vector<double>{1, 1, 1}));
  QCOMPARE(sawtoothWindow(3), (std::vector<double>{0, 0.5, 1}));
}

void TstCbiCore::bValueRecovered_data() {
  QTest::addColumn<double>("b");
  QTest::newRow("0.8") << 0.8;
  QTest::newRow("1.0") << 1.0;
  QTest::newRow("1.3") << 1.3;
}

void TstCbiCore::bValueRecovered() {
  QFETCH(double, b);
  std::vector<double> mags, times;
  grCatalog(50000, b, 2.0, 0.1, 7, mags, times);
  const WindowIndicators r =
      computeWindowIndicators(mags, times, 2.0, 0.4, 0.1);
  QVERIFY2(std::abs(r.bValue - b) / b < 0.03,
           qPrintable(QString::number(r.bValue)));
  QVERIFY(std::isfinite(r.aValue));
  QVERIFY(std::isfinite(r.eta));
  QCOMPARE(r.eventCount, 50000);
}

void TstCbiCore::emptyWindowIsNaN() {
  const WindowIndicators r = computeWindowIndicators({}, {}, 2.0);
  QCOMPARE(r.eventCount, 0);
  for (const QString &c : windowIndicatorColumns())
    if (c != QLatin1String("Event_Count"))
      QVERIFY2(std::isnan(r.value(c)), qPrintable(c));
}

void TstCbiCore::singleEventWindow() {
  const WindowIndicators r =
      computeWindowIndicators({3.0}, {ep("2020-01-01")}, 2.0);
  QCOMPARE(r.T, 0.0);
  QCOMPARE(r.mmeanRectangular, 3.0);
  for (const char *k : {"a_value", "Delta_M", "dE_sqrt_rectangular",
                        "dE_sqrt_half_gauss", "eta", "mu"})
    QVERIFY2(std::isnan(r.value(QLatin1String(k))), k);
}

void TstCbiCore::halfGaussEnergyEqualsRectangularForEqualMagnitudes() {
  std::vector<double> mags(50, 3.0), times;
  for (int i = 0; i < 50; ++i)
    times.push_back(ep("2020-01-01") + i * 86400.0);
  const WindowIndicators r = computeWindowIndicators(mags, times, 2.0);
  QVERIFY(std::abs(r.dEsqrtHalfGauss - r.dEsqrtRectangular) <
          1e-9 * r.dEsqrtRectangular);
}

void TstCbiCore::eventsOnWindowEndBelongToNext() {
  const Catalog c = events({{"2020-01-01", 0, 0, 2.0},
                            {"2020-01-20", 0, 0, 2.5},
                            {"2020-02-01", 0, 0, 3.0},
                            {"2020-03-01", 0, 0, 2.0}});
  IndicatorConfig cfg;
  cfg.mc = 2.0;
  cfg.window = Duration{1, QLatin1Char('M')};
  cfg.step = Duration{1, QLatin1Char('M')};
  const IndicatorTable t = computeIndicatorTable(c, cfg);
  QCOMPARE(t.rows(), 2);
  QCOMPARE(*t.column(QStringLiteral("Event_Count")),
           (std::vector<double>{2, 1}));
}

void TstCbiCore::gkWindowsMatchPublishedFits() {
  const auto [d5, t5] = gkWindow(5.0);
  const auto [d7, t7] = gkWindow(7.0);
  QVERIFY(std::abs(d5 - 40.0) / 40.0 < 0.01);
  QVERIFY(std::abs(t5 - 143.7) / 143.7 < 0.01);
  QVERIFY(std::abs(t7 - 915) / 915 < 0.01);
  Q_UNUSED(d7);
}

void TstCbiCore::gardnerKnopoffRemovesForeAndAftershocksOnly() {
  const Catalog c = events({
      {"2020-01-20", 39.00, 38.40, 4.0}, // foreshock, 4 days before, ~5 km
      {"2020-01-24", 39.06, 38.36, 6.8}, // mainshock
      {"2020-02-15", 39.20, 38.40, 5.1}, // aftershock, ~13 km
      {"2020-02-16", 36.20, 36.20, 4.5}, // independent: ~350 km away
      {"2023-01-01", 39.06, 38.36,
       4.0}, // independent: after the ~2.5 yr window
  });
  QCOMPARE(gardnerKnopoff(c),
           (std::vector<bool>{false, true, false, true, true}));
}

void TstCbiCore::rateMatchingRecoversStretchAndShift() {
  std::mt19937_64 rng(1);
  std::exponential_distribution<double> ex(std::log(10.0));
  Catalog c;
  const int n = 40000;
  const double p0 = ep("2008-01-01"), p1 = ep("2013-12-31"),
               r0 = ep("2014-01-01"), r1 = ep("2019-12-31");
  for (int i = 0; i < n; ++i) {
    skg::analysis::Event e;
    e.epoch = p0 + (p1 - p0) * i / (n - 1);
    e.mag = roundDecimals(1.2 * (1.0 + ex(rng)) - 0.3,
                          1); // same rate, distorted scale
    c.events.push_back(e);
  }
  for (int i = 0; i < n; ++i) {
    skg::analysis::Event e;
    e.epoch = r0 + (r1 - r0) * i / (n - 1);
    e.mag = roundDecimals(1.0 + ex(rng), 1);
    c.events.push_back(e);
  }
  const auto [from, to] =
      fitRateMatching(c, {ep("2008-01-01"), ep("2014-01-01")},
                      {ep("2014-01-01"), ep("2020-01-01")}, 50);
  QVERIFY(from.size() > 10);
  for (size_t i = 0; i < from.size(); ++i)
    QVERIFY2(std::abs(to[i] - (from[i] + 0.3) / 1.2) < 0.08,
             qPrintable(QStringLiteral("%1 -> %2").arg(from[i]).arg(to[i])));
}

void TstCbiCore::conversionAppliesByPeriod() {
  MagnitudeConversion conv;
  conv.periods = {
      {std::nullopt, utc("2011-10-23"), {2.0, 4.0}, {1.5, 4.2}},
      {utc("2011-10-23"), utc("2013-11-20"), {2.0, 4.0}, {1.8, 3.8}}};
  conv.referenceStart = utc("2014-01-01");
  conv.referenceEnd = utc("2020-01-01");
  Catalog c = events({{"2010-01-01", 0, 0, 3.0},
                      {"2012-01-01", 0, 0, 3.0},
                      {"2015-01-01", 0, 0, 3.0},
                      {"2010-01-02", 0, 0, 5.0}});
  conv.apply(c);
  QCOMPARE(c.magnitudes(), (std::vector<double>{2.8, 5.2, 2.8, 3.0}));
  QCOMPARE(c.originalMagnitude, (std::vector<double>{3.0, 5.0, 3.0, 3.0}));
  // JSON round trip
  const MagnitudeConversion back = MagnitudeConversion::fromJson(conv.toJson());
  QCOMPARE(int(back.periods.size()), 2);
  QVERIFY(!back.periods[0].start.has_value());
  QCOMPARE(back.periods[1].mTo, conv.periods[1].mTo);
}

void TstCbiCore::shippedConversionIsMonotone() {
  const MagnitudeConversion conv = MagnitudeConversion::afadDefault();
  QCOMPARE(int(conv.periods.size()), 2);
  for (const PeriodMap &p : conv.periods) {
    QVERIFY(p.mFrom.size() > 10);
    for (size_t i = 1; i < p.mFrom.size(); ++i) {
      QVERIFY(p.mFrom[i] > p.mFrom[i - 1]);
      QVERIFY(p.mTo[i] >= p.mTo[i - 1]);
    }
  }
  QVERIFY(conv.periods[0].contains(ep("2010-05-05")));
  QVERIFY(!conv.periods[0].contains(ep("2012-05-05")));
  QVERIFY(conv.periods[1].contains(ep("2012-05-05")));
}

void TstCbiCore::maxCurvatureMcOnSynthetic() {
  // GR above 2.0 plus a roll-off below it: the modal bin is 2.0.
  std::vector<double> mags, times;
  grCatalog(20000, 1.0, 2.0, 0.1, 3, mags, times);
  std::mt19937_64 rng(4);
  std::uniform_real_distribution<double> u(1.0, 2.0);
  for (int i = 0; i < 3000; ++i)
    mags.push_back(roundToStep(u(rng), 0.1));
  QVERIFY(std::abs(maxCurvatureMc(mags) - 2.2) < 1e-9);
  QVERIFY(std::isnan(maxCurvatureMc({})));
}

void TstCbiCore::distanceToMeridianSegment() {
  FaultZone z;
  z.segments = {{38.0, 37.0, 38.0, 39.0}};
  // 0.5 deg east at 38N is ~43.9 km; beyond the segment end measures to the
  // end.
  const double expected = 111.32 * std::cos(38.0 * M_PI / 180.0) * 0.5;
  QVERIFY(std::abs(z.distanceKm(38.5, 38.0) - expected) < 1e-6 * expected);
  QCOMPARE(z.distanceKm(38.0, 38.0), 0.0);
  QVERIFY(std::abs(z.distanceKm(38.0, 39.5) - 0.5 * 110.57) < 1e-6 * 55);
}

void TstCbiCore::knownEafzEarthquakesLieOnTraces() {
  struct Case {
    const char *name;
    double lon, lat;
    const char *strand;
    double maxKm;
  };
  const Case cases[] = {
      {"2020 Sivrice Mw6.8", 39.063, 38.359, "main", 5},
      {"2023 Pazarcik Mw7.7", 37.040, 37.277, "main", 25},
      {"2023 Elbistan Mw7.6", 37.231, 38.078, "northern", 10}};
  for (const Case &c : cases) {
    const FaultZone z = eafz({QLatin1String(c.strand)});
    QVERIFY2(z.distanceKm(c.lon, c.lat) < c.maxKm, c.name);
  }
  QCOMPARE(zoneStrands(eafzPath()),
           (QStringList{QStringLiteral("main"), QStringLiteral("northern")}));
  QCOMPARE(eafz().name, QStringLiteral("East Anatolian Fault Zone"));
}

void TstCbiCore::farAwayEventExcluded() {
  const Catalog c = events(
      {{"2020-01-24", 39.063, 38.359, 6.8}, {"2020-01-25", 29.0, 39.0, 4.0}});
  const Catalog sel = selectNearFault(c, eafz(), 30);
  QCOMPARE(int(sel.size()), 1);
  QCOMPARE(sel.events[0].lon, 39.063);
}

void TstCbiCore::unknownStrandRejected() {
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           eafz({QStringLiteral("southern")}));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, loadFaultZone(QStringLiteral(
                                                   "/nonexistent.geojson")));
}

namespace {
IndicatorTable windows(std::initializer_list<const char *> starts,
                       int days = 30) {
  IndicatorTable t;
  for (const char *s : starts) {
    t.start.push_back(utc(s));
    t.end.push_back(utc(s).addDays(days));
    t.xLabel << QString();
  }
  return t;
}
} // namespace

void TstCbiCore::eachLargeEventMarkedOnceInFirstWindow() {
  IndicatorTable t =
      windows({"2020-01-01", "2020-01-16", "2020-02-01", "2020-02-16"});
  // Two distinct events of the same magnitude; the original tool only marked
  // the first.
  const Catalog c = events({{"2020-01-10", 0, 0, 5.5},
                            {"2020-02-05", 0, 0, 5.5},
                            {"2020-02-20", 0, 0, 5.2}});
  markFirstAppearance(t, c);
  QCOMPARE(*t.column(QStringLiteral("Mmax_obs_first_5_to_6")),
           (std::vector<double>{5.5, 5.5, 5.2, 0.0}));
  QCOMPARE(*t.column(QStringLiteral("Mmax_obs_first_6_plus")),
           (std::vector<double>{0, 0, 0, 0}));
}

void TstCbiCore::newEventMarkedWhileLargerOldOneInView() {
  IndicatorTable t = windows({"2020-01-01", "2020-01-16"});
  const Catalog c =
      events({{"2020-01-20", 0, 0, 6.2}, {"2020-02-10", 0, 0, 5.1}});
  markFirstAppearance(t, c);
  QCOMPARE(*t.column(QStringLiteral("Mmax_obs_first_6_plus")),
           (std::vector<double>{6.2, 0.0}));
  QCOMPARE(*t.column(QStringLiteral("Mmax_obs_first_5_to_6")),
           (std::vector<double>{0.0, 5.1}));
}

void TstCbiCore::riskLabelAnchoredOnWindowEnd() {
  // Window ends 2020-01-31; the event is 10 days after the end but 40 after the
  // start.
  IndicatorTable t = windows({"2020-01-01", "2019-06-01"});
  labelPreEventWindows(t, {ep("2020-02-10")}, 30);
  QCOMPARE(t.risk, (QStringList{riskNear(), riskFar()}));
  IndicatorTable t2 = windows({"2020-01-01"});
  labelPreEventWindows(t2, {ep("2020-01-20")}, 90); // before the window end
  QCOMPARE(t2.risk, (QStringList{riskFar()}));
}

void TstCbiCore::movingAverageAndBollinger() {
  IndicatorTable t =
      windows({"2020-01-01", "2020-02-01", "2020-03-01", "2020-04-01"});
  const double nan = std::nan("");
  t.addColumn(QStringLiteral("b_value"), {1.0, 3.0, nan, 5.0});
  addMovingAverage(t, 2, {QStringLiteral("b_value")});
  const std::vector<double> &ma = *t.column(QStringLiteral("b_value_MA2"));
  QCOMPARE(ma[0], 1.0);
  QCOMPARE(ma[1], 2.0);
  QCOMPARE(ma[2], 3.0); // the NaN is skipped, as pandas does
  QCOMPARE(ma[3], 5.0);
  addBollingerBands(t, 3, 2.0, {QStringLiteral("b_value")});
  const std::vector<double> &up = *t.column(QStringLiteral("b_value_BB_Upper"));
  const std::vector<double> &w = *t.column(QStringLiteral("b_value_BB_Width"));
  QCOMPARE(w[0], 0.0); // a single value: std NaN -> 0
  QVERIFY(std::abs(up[1] - (2.0 + 2.0 * std::sqrt(2.0))) < 1e-12);
  QVERIFY(std::abs(w[3] - 2 * 2.0 * std::sqrt(2.0)) <
          1e-12); // {3, 5}: sample std sqrt(2)
}

void TstCbiCore::pandasNumbers() {
  QCOMPARE(pandasNumber(0.0), QStringLiteral("0.0"));
  QCOMPARE(pandasNumber(5.3), QStringLiteral("5.3"));
  QCOMPARE(pandasNumber(182.86349537037037),
           QStringLiteral("182.86349537037037"));
  QCOMPARE(pandasNumber(1000000.0), QStringLiteral("1000000.0"));
  QCOMPARE(pandasNumber(std::nan("")), QString());
  QCOMPARE(pandasNumber(1e-7), QStringLiteral("1e-07"));
}

void TstCbiCore::csvRoundTrip() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const Catalog c =
      loadCatalog(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  QCOMPARE(int(c.size()), 2506);
  IndicatorConfig cfg;
  cfg.mc = 1.3;
  cfg.window = Duration{6, QLatin1Char('M')};
  cfg.step = Duration{1, QLatin1Char('M')};
  IndicatorTable t = computeIndicatorTable(c, cfg);
  labelPreEventWindows(t, {ep("2019-04-02")}, 90);
  const QString path = dir.filePath(QStringLiteral("t.csv"));
  QVERIFY(writeTableCsv(t, path));
  const IndicatorTable back = readTableCsv(path);
  QCOMPARE(back.rows(), t.rows());
  QCOMPARE(back.columns, t.columns);
  QCOMPARE(back.risk, t.risk);
  QCOMPARE(back.start, t.start);
  for (size_t cI = 0; cI < t.values.size(); ++cI)
    for (size_t r = 0; r < t.values[cI].size(); ++r) {
      const double a = t.values[cI][r], b = back.values[cI][r];
      QVERIFY2((std::isnan(a) && std::isnan(b)) || a == b,
               qPrintable(
                   QStringLiteral("%1 row %2").arg(t.columns[int(cI)]).arg(r)));
    }
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray head = f.read(64);
  QVERIFY2(head.startsWith("\xEF\xBB\xBF"
                           "Window_Index,x_label,Window_Start_Time"),
           head.toHex(' ').constData());
}

void TstCbiCore::selectionTag() {
  CatalogSelection s;
  QCOMPARE(s.tag(), QStringLiteral("eafz30km"));
  s.homogenize = s.decluster = true;
  QCOMPARE(s.tag(), QStringLiteral("eafz30km_hom_dc"));
  s.zone = CatalogSelection::Zone::None;
  s.homogenize = s.decluster = false;
  QCOMPARE(s.tag(), QStringLiteral("all"));
  // The selection applies: the synthetic catalog is far from the EAFZ buffer
  // only partly, and the start date drops events.
  const Catalog c =
      loadCatalog(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  CatalogSelection all;
  all.zone = CatalogSelection::Zone::None;
  all.start = utc("2020-01-01");
  QStringList log;
  const Catalog sel = all.apply(c, &log);
  QVERIFY(sel.size() < c.size());
  QVERIFY(sel.events.front().epoch >= ep("2020-01-01"));
  QCOMPARE(log.size(), 1);
}

void TstCbiCore::legacyRegression_data() {
  QTest::addColumn<QString>("reference");
  QTest::addColumn<double>("mc");
  QTest::addColumn<QString>("window");
  QTest::addColumn<QString>("step");
  QTest::newRow("6M/15D mc1.3")
      << QStringLiteral("legacy_6M_15D_mc1.3.csv") << 1.3
      << QStringLiteral("6M") << QStringLiteral("15D");
  QTest::newRow("3M/3D mc2.0")
      << QStringLiteral("legacy_3M_3D_mc2.0.csv") << 2.0 << QStringLiteral("3M")
      << QStringLiteral("3D");
}

// The original tool's tables, which differ from the fork (and this port)
// only where a bug was fixed deliberately: undefined indicators are NaN
// not 0; the b-value bin correction was Mc - 0.005 (mag_bin 0.01 here);
// dE_sqrt_half_gauss used weights summing to 1 (x Event_Count); a second
// event of an already-seen magnitude is now marked too.
void TstCbiCore::legacyRegression() {
  QFETCH(QString, reference);
  QFETCH(double, mc);
  QFETCH(QString, window);
  QFETCH(QString, step);
  const Catalog c =
      loadCatalog(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  IndicatorConfig cfg;
  cfg.mc = mc;
  cfg.window = *Duration::parse(window);
  cfg.step = *Duration::parse(step);
  cfg.magBin = 0.01;
  const IndicatorTable actual = computeIndicatorTable(c, cfg);
  const IndicatorTable expected =
      readTableCsv(QStringLiteral(SKG_TEST_DATA "/cbi/") + reference);
  QCOMPARE(actual.rows(), expected.rows());
  QCOMPARE(actual.xLabel, expected.xLabel);
  QCOMPARE(actual.start, expected.start);
  QCOMPARE(actual.end, expected.end);
  const QStringList unchanged{"T",
                              "b_value",
                              "a_value",
                              "Delta_M",
                              "eta",
                              "mu",
                              "Event_Count",
                              "Mmax_obs",
                              "Mmean_rectangular",
                              "dE_sqrt_rectangular",
                              "Mmean_half_gauss"};
  for (const QString &col : unchanged) {
    const std::vector<double> &a = *actual.column(col),
                              &e = *expected.column(col);
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::isnan(a[i])) {
        QVERIFY2(e[i] == 0.0,
                 qPrintable(QStringLiteral("%1[%2] legacy %3 for NaN")
                                .arg(col)
                                .arg(i)
                                .arg(e[i])));
        continue;
      }
      const double tol = 1e-9 * std::max(1.0, std::abs(e[i]));
      QVERIFY2(std::abs(a[i] - e[i]) <= tol,
               qPrintable(QStringLiteral("%1[%2]: %3 vs legacy %4")
                              .arg(col)
                              .arg(i)
                              .arg(a[i], 0, 'g', 17)
                              .arg(e[i], 0, 'g', 17)));
    }
  }
  const std::vector<double> &hg =
      *actual.column(QStringLiteral("dE_sqrt_half_gauss"));
  const std::vector<double> &hgL =
      *expected.column(QStringLiteral("dE_sqrt_half_gauss"));
  const std::vector<double> &n =
      *expected.column(QStringLiteral("Event_Count"));
  for (size_t i = 0; i < hg.size(); ++i) {
    if (std::isnan(hg[i])) {
      QCOMPARE(hgL[i], 0.0);
      continue;
    }
    QVERIFY2(std::abs(hg[i] - hgL[i] * n[i]) <= 1e-9 * std::abs(hg[i]),
             qPrintable(QStringLiteral("dE_sqrt_half_gauss[%1]: %2 vs %3")
                            .arg(i)
                            .arg(hg[i])
                            .arg(hgL[i] * n[i])));
  }
  // Markers: where the original marked, we mark the same magnitude...
  int repeats = 0;
  for (const char *col : {"Mmax_obs_first_5_to_6", "Mmax_obs_first_6_plus"}) {
    const std::vector<double> &a = *actual.column(QLatin1String(col)),
                              &e = *expected.column(QLatin1String(col));
    for (size_t i = 0; i < a.size(); ++i)
      if (e[i] > 0)
        QCOMPARE(a[i], e[i]);
      else if (a[i] > 0)
        ++repeats;
  }
  // ...and the 2019-09-15 M5.3, which repeats an earlier magnitude, is now
  // marked too.
  QVERIFY(repeats >= 1);
}

// The EAFZ zone file left the binary (it is CC BY-SA data shipped beside the
// program); the zone it defines must be the one it always was.
void TstCbiCore::eafzFileIsTheGemClipAndSelectsAsBefore() {
  const QString path = eafzPath();
  QVERIFY2(!path.startsWith(QLatin1Char(':')), qPrintable(path));
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(
      QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex(),
      QByteArray("94f6262d0b5cbf2df8a57d425dd3320d8a460bc56c2655d1f69cd979ee25"
                 "d9d3"));
  QCOMPARE(int(eafz().segments.size()), 158);
  // 2108 of the synthetic catalog's 2506 events lie within 30 km of the
  // traces (counted independently from the GeoJSON with the same
  // projection; the nearest event to the limit is 33 m from it).
  const Catalog c =
      loadCatalog(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  QCOMPARE(int(c.size()), 2506);
  QCOMPARE(int(selectNearFault(c, eafz(), 30).size()), 2108);
  CatalogSelection sel; // the default: EAFZ, 30 km
  sel.homogenize = sel.decluster = false;
  QCOMPARE(int(sel.apply(c).size()), 2108);
}

// The layer drawn on maps: EFSM20's fault sources, whole Turkey, no strands.
void TstCbiCore::shippedFaultLayerLoads() {
  const FaultZone &z = shippedFaults();
  QCOMPARE(int(z.segments.size()), 2845); // 422 traces, 3267 vertices
  QCOMPARE(z.name, QStringLiteral("Seismogenic fault sources, EFSM20"));
  QVERIFY(z.strands.isEmpty());
  QVERIFY(zoneStrands(shippedFaultsPath()).isEmpty());
  QCOMPARE(&shippedFaults(), &z); // read once
  // On the North Anatolian Fault (Izmit 1999, Duzce 1999, Erzincan 1992),
  // and nothing near the middle of the Black Sea.
  QVERIFY(z.distanceKm(29.97, 40.70) < 5);
  QVERIFY(z.distanceKm(31.15, 40.77) < 5);
  QVERIFY(z.distanceKm(39.63, 39.71) < 5);
  QVERIFY(z.distanceKm(34.0, 43.3) > 50);
  // It is not the zone file, and asking it for a strand is an error that
  // names the problem (the maps must load it without strands).
  QVERIFY(shippedFaultsPath() != eafzPath());
  try {
    loadFaultZone(shippedFaultsPath(), {QStringLiteral("main")});
    QFAIL("a strand of a file without strands was accepted");
  } catch (const std::runtime_error &e) {
    QVERIFY(QString::fromUtf8(e.what()).contains(QLatin1String("unknown strand")));
  }
  QVERIFY(shippedFaultsLabel(QStringLiteral("en"))
              .contains(QLatin1String("hazard model")));
  QVERIFY(!shippedFaultsLabel(QStringLiteral("en"))
               .contains(QLatin1String("active"), Qt::CaseInsensitive));
  QVERIFY(shippedFaultsLabel(QStringLiteral("tr"))
              .contains(QString::fromUtf8("tehlike modeli")));
  QVERIFY(shippedFaultsCredit(QStringLiteral("en"))
              .contains(QLatin1String("CC BY 4.0")));
}

void TstCbiCore::dataFileLookup() {
  // Uninstalled, the files come from the source tree.
  const QStringList dirs = dataDirs();
  QVERIFY(!dirs.isEmpty());
  QVERIFY(QFileInfo(dataFile(QStringLiteral("eafz.geojson"))).isFile());
  QVERIFY(QFileInfo(dataFile(QStringLiteral("faults_tr.geojson"))).isFile());
  // Beside a program: <dir>/data, share/sismokaos-gui/data, Resources/data.
  const QString app = QCoreApplication::applicationDirPath();
  QVERIFY(dirs.contains(QDir::cleanPath(app + QStringLiteral("/data"))));
  QVERIFY(dirs.contains(
      QDir::cleanPath(app + QStringLiteral("/../share/sismokaos-gui/data"))));
  QVERIFY(
      dirs.contains(QDir::cleanPath(app + QStringLiteral("/../Resources/data"))));
  // A missing file names itself and every place searched.
  try {
    dataFile(QStringLiteral("no_such_file.geojson"));
    QFAIL("a missing data file was found");
  } catch (const std::runtime_error &e) {
    const QString what = QString::fromUtf8(e.what());
    QVERIFY(what.contains(QLatin1String("no_such_file.geojson")));
    for (const QString &d : dirs)
      QVERIFY2(what.contains(d), qPrintable(what));
  }
  // $SKG_DATA_DIR comes first.
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QFile::copy(eafzPath(), tmp.filePath(QStringLiteral("eafz.geojson"))));
  qputenv("SKG_DATA_DIR", tmp.path().toUtf8());
  QCOMPARE(dataDirs().first(), QDir::cleanPath(tmp.path()));
  QCOMPARE(QFileInfo(eafzPath()).canonicalFilePath(),
           QFileInfo(tmp.filePath(QStringLiteral("eafz.geojson")))
               .canonicalFilePath());
  // ...and a file it lacks is still found further down the list.
  QVERIFY(QFileInfo(shippedFaultsPath()).isFile());
  qunsetenv("SKG_DATA_DIR");
}

QTEST_GUILESS_MAIN(TstCbiCore)
#include "tst_cbi_core.moc"
