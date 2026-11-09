// The classification port, first slice: settings in sfc's TOML form, the
// dataset from sfc's CSV and from an fx table through the day selection,
// the split by day, the scalers, the folds and the metrics.
#include <QtTest>

#include <cmath>

#include "sfc/dataset.h"
#include "sfc/metrics.h"
#include "sfc/settings.h"
#include "sfc/split.h"

using namespace skg::sfc;

namespace {
// An fx-style table: one window every 200 s over `days` consecutive UTC
// days ending the day after the event, two feature columns.
skg::fx::NpyTable fxTable(const QDate &first, int days) {
  skg::fx::NpyTable t;
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("N_RMS"), QStringLiteral("<f8")});
  t.fields.append(
      skg::fx::NpyField{QStringLiteral("N_WOLF_LYE"), QStringLiteral("<f8")});
  QStringList ids;
  std::vector<double> zaman, rms, wolf;
  int n = 0;
  const double t0 = double(
      QDateTime(first, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch());
  for (int d = 0; d < days; ++d)
    for (int w = 1; w <= 432; ++w) {
      ids << QStringLiteral("x_w%1").arg(++n);
      zaman.push_back((t0 + d * 86400 + w * 200.0) / 60.0);
      rms.push_back(100.0 + d + 0.01 * w);
      wolf.push_back(w == 7 ? std::nan("") : 0.1 * d);
    }
  t.rows = n;
  t.strings.append(ids);
  t.strings.append(QStringList());
  t.strings.append(QStringList());
  t.strings.append(QStringList());
  t.numeric.append(std::vector<double>());
  t.numeric.append(zaman);
  t.numeric.append(rms);
  t.numeric.append(wolf);
  return t;
}
} // namespace

class TstSfcCore : public QObject {
  Q_OBJECT
private slots:
  void gridsAndParams() {
    Grid g{{"C", {1, 10}}, {"gamma", {QStringLiteral("scale"), 1}}};
    const auto combos = expandGrid(g);
    QCOMPARE(combos.size(), 4);
    QCOMPARE(combos[0].value("C").toInt(), 1);
    QCOMPARE(combos[0].value("gamma").toString(), QStringLiteral("scale"));
    QCOMPARE(combos[1].value("gamma").toInt(),
             1); // the last key varies fastest
    QCOMPARE(paramsText(combos[0]),
             QStringLiteral("{'C': 1, 'gamma': 'scale'}"));
    QCOMPARE(
        paramsText(
            {{"hidden_layer_sizes", QVariantList{32, 16}}, {"alpha", 0.001}}),
        QStringLiteral("{'alpha': 0.001, 'hidden_layer_sizes': (32, 16)}"));
    QCOMPARE(paramsText({{"hidden_layer_sizes", QVariantList{16}}}),
             QStringLiteral("{'hidden_layer_sizes': (16,)}"));
  }

  void settingsToml() {
    const QString text = R"(
station = "ELZG"
earthquake_name = "elazig_2020"
eq_time = "2020-01-24T17:55:00Z"
class_table = [[0, 1, 30, 1, 30], [1, 31, 90, 2, 30], [2, 91, 365, 9, 30], [3, 366, 1826, 49, 30]]
feature_columns = ["raw_std", "wolf_lye", "ros_lye"]
label_merge = {0 = 0, 1 = 1, 2 = 1, 3 = 2}
cv_folds = "none"
clip_after_scaling = "none"
freqmin = 0.1
run_name = "quick"
[tree_grids.xgboost]
n_estimators = [50]
max_depth = [3]
)";
    const Settings s = Settings::fromToml(text);
    QCOMPARE(s.station, QStringLiteral("ELZG"));
    QCOMPARE(s.eqDate, QDate(2020, 1, 24));
    QCOMPARE(s.eqCutoffHour, 17);
    QCOMPARE(s.classTable.size(), 4);
    QCOMPARE(s.classTable[2].step.value(), 9);
    QCOMPARE(s.featureColumns.size(), 3);
    QVERIFY(s.labelMerge.has_value());
    QCOMPARE(s.labelMerge->value(2), 1);
    QVERIFY(!s.cvFolds.has_value());
    QVERIFY(!s.clipAfterScaling.has_value());
    QCOMPARE(s.passthrough.value("freqmin").toDouble(), 0.1);
    QCOMPARE(s.treeGrids.value("xgboost").value("max_depth").size(), 1);
    QCOMPARE(s.treeGrids.size(), 1); // the file's table replaces the default
    const QMap<int, int> map = s.labelToClass();
    QCOMPARE(map.value(3), 2);
    QCOMPARE(s.classNames().value(1), QStringLiteral("31-365 days"));
    // Round trip, with the passthrough kept.
    const Settings back = Settings::fromToml(s.toToml());
    QCOMPARE(back.asMap(), s.asMap());
    QVERIFY(back.toToml().contains("freqmin = 0.1"));
    QVERIFY(back.toToml().contains("cv_folds = 'none'") ||
            back.toToml().contains("cv_folds = \"none\""));
    // differences: run_name and project keys do not count
    Settings other = s;
    other.runName = "x";
    other.station = "Y";
    other.cvFolds = 3;
    QCOMPARE(Settings::differences(s.asMap(), other.asMap()),
             QStringList{"cv_folds"});
    QCOMPARE(Settings::differences(s.asMap(), other.asMap(), false),
             (QStringList{"cv_folds", "station"}));
    // a list label_merge, as the command line gives it
    const Settings lm = Settings::fromToml("label_merge = [0, 0, 1, 1]");
    QCOMPARE(lm.labelMerge->value(1), 0);
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        Settings::fromToml("label_merge = [0, 2, 2, 3]").labelToClass());
  }

  void rngIsDeterministic() {
    Rng a(42), b(42);
    QCOMPARE(a.next(), b.next());
    const auto p = Rng(1).permutation(10);
    QCOMPARE(int(p.size()), 10);
    std::vector<int> sorted = p;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 10; ++i)
      QCOMPARE(sorted[size_t(i)], i);
    QCOMPARE(int(Rng(3).choice(100, 10, false).size()), 10);
  }

  void daySelectionAndFxDataset() {
    Settings s;
    s.eqDate = QDate(2020, 1, 24);
    s.eqCutoffHour = 17;
    s.classTable = {{0, 1, 5, 1, 5}, {1, 6, 20, 3, 4}};
    // Data from 2020-01-03 to 2020-01-25; 01-12 missing (a planned label-1 day,
    // moved to 01-11).
    QMap<QDate, int> perDay;
    for (QDate d(2020, 1, 3); d <= QDate(2020, 1, 25); d = d.addDays(1))
      if (d != QDate(2020, 1, 12))
        perDay.insert(d, d == QDate(2020, 1, 20) ? 100 : 432);
    const DaySelection sel = selectDays(s, perDay, 400);
    // the event day + 5 label-0 days + 4 label-1 days
    QCOMPARE(sel.rows.size(), 10);
    QCOMPARE(sel.rows.first().date,
             QDate(2020, 1,
                   9)); // 15 days before (the planned 18 is 01-06... no: 6, 9,
                        // 12, 15 days before = 01-18, 01-15, 01-12, 01-09)
    int moved = 0;
    for (const auto &r : sel.rows) {
      QCOMPARE(int(r.date.daysTo(s.eqDate)), r.daysToEq);
      if (!r.reason.isEmpty())
        ++moved;
    }
    QCOMPARE(moved, 1); // 01-12 (no file) -> 01-11; 01-20 is incomplete but
                        // label 0 has step 1, shift 0 1 -> shift 0 keeps it)
    QVERIFY(sel.log.join('\n').contains("replaced"));
    // 01-20 has step 1 -> max shift 0 -> kept although incomplete
    bool kept20 = false;
    for (const auto &r : sel.rows)
      if (r.date == QDate(2020, 1, 20))
        kept20 = r.reason.isEmpty();
    QVERIFY(kept20);

    const auto table = fxTable(QDate(2020, 1, 3), 23);
    const QMap<QDate, int> counted = fxWindowsPerDay(table);
    QCOMPARE(counted.value(QDate(2020, 1, 3)), 432);
    Dataset d = fromFxTable(table, s, sel, {"N_RMS", "N_WOLF_LYE"});
    // 9 full days x 432 + the event day's hours before 17:00 (17 h x 18
    // windows)
    QCOMPARE(d.rows(), 9 * 432 + 17 * 18);
    QCOMPARE(d.nFeatures(), 2);
    QStringList log;
    const int dropped = prepareClasses(d, s, &log);
    QCOMPARE(dropped, 10); // one NaN window per day (w == 7)
    QCOMPARE(d.rows(), 9 * 432 + 17 * 18 - 10);
    QCOMPARE(int(d.days().size()), 10);

    // sfc CSV round trip
    const QTemporaryDir dir;
    Settings cs = s;
    cs.featureColumns = {"N_RMS", "N_WOLF_LYE"};
    QVERIFY(saveSfcCsv(dir.filePath("f.csv"), d, cs));
    const Dataset back = loadSfcCsv(dir.filePath("f.csv"), cs);
    QCOMPARE(back.rows(), d.rows());
    QCOMPARE(back.label[0], d.label[0]);
    QCOMPARE(back.at(5, 0), d.at(5, 0));
    QCOMPARE(back.windowStart[0], d.windowStart[0]);
    QFile f(dir.filePath("f.csv"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString head = QString::fromUtf8(f.readLine());
    QVERIFY(head.startsWith("Station;Date;Window_Start_UTC;Window_End_UTC;Days_"
                            "To_EQ;Label;N_RMS;N_WOLF_LYE"));
  }

  void splitScalersAndFolds() {
    Settings s;
    s.eqDate = QDate(2020, 1, 24);
    s.classTable = {{0, 1, 10, 1, 10}, {1, 11, 30, 2, 10}};
    s.labelMerge = QMap<int, int>{{0, 0}, {1, 1}};
    QMap<QDate, int> perDay;
    for (QDate d(2019, 12, 20); d <= QDate(2020, 1, 23); d = d.addDays(1))
      perDay.insert(d, 432);
    const DaySelection sel = selectDays(s, perDay, 0);
    Dataset d = fromFxTable(fxTable(QDate(2019, 12, 20), 35), s, sel,
                            {"N_RMS", "N_WOLF_LYE"});
    prepareClasses(d, s);
    const SplitData data = prepareData(d, s);
    QCOMPARE(data.train.rows + data.val.rows + data.test.rows, d.rows());
    // Every day lies in one split only.
    QMap<QDate, QString> seen;
    for (int r = 0; r < d.rows(); ++r) {
      const QString &sp = data.split[size_t(r)];
      if (seen.contains(d.date[size_t(r)]))
        QCOMPARE(seen.value(d.date[size_t(r)]), sp);
      seen.insert(d.date[size_t(r)], sp);
    }
    // 10 days per label: 6 train, 2 val, 2 test
    int train = 0, val = 0, test = 0;
    for (const QString &sp : seen)
      (sp == "train" ? train : sp == "val" ? val : test)++;
    QCOMPARE(train, 12);
    QCOMPARE(val, 4);
    QCOMPARE(test, 4);
    // the same seed gives the same split
    QVERIFY(splitByDay(d, s) == data.split);
    // robust scaling: the training median maps to 0; clipping holds
    QCOMPARE(data.scaler.kind, QStringLiteral("robust"));
    for (int r = 0; r < data.train.rows; ++r)
      for (int c = 0; c < 2; ++c)
        QVERIFY(std::abs(data.train.row(r)[c]) <= 5.0);
    const Scaler again = Scaler::fromJson(data.scaler.toJson());
    QCOMPARE(again.center, data.scaler.center);
    Settings mm = s;
    mm.scaler = "minmax";
    mm.clipAfterScaling.reset();
    const SplitData m2 = prepareData(d, mm);
    double lo = 1, hi = 0;
    for (int r = 0; r < m2.train.rows; ++r) {
      lo = std::min(lo, m2.train.row(r)[0]);
      hi = std::max(hi, m2.train.row(r)[0]);
    }
    QVERIFY(std::abs(lo) < 1e-12 && std::abs(hi - 1) < 1e-12);
    Settings bad = s;
    bad.scaler = "nope";
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, prepareData(d, bad));
    QVERIFY(
        splitSummary(d, data.split, s).join('\n').contains("Days per class"));

    // folds over train + val keep days together and stratify
    const Matrix tv = concat(data.train, data.val);
    std::vector<QDate> groups;
    for (int i : tv.index)
      groups.push_back(d.date[size_t(i)]);
    const auto folds = stratifiedGroupKFold(tv.y, groups, 4, 42);
    QCOMPARE(int(folds.size()), 4);
    size_t total = 0;
    for (const Fold &f : folds) {
      total += f.val.size();
      QCOMPARE(f.train.size() + f.val.size(), tv.y.size());
      QSet<QDate> valDays, trainDays;
      int c0 = 0, c1 = 0;
      for (int i : f.val) {
        valDays.insert(groups[size_t(i)]);
        (tv.y[size_t(i)] ? c1 : c0)++;
      }
      for (int i : f.train)
        trainDays.insert(groups[size_t(i)]);
      QVERIFY(valDays.intersect(trainDays).isEmpty());
      if (!(c0 > 0 && c1 > 0))
        qWarning() << "fold without both classes:" << c0 << c1 << "days"
                   << valDays.size();
      QVERIFY(c0 > 0 && c1 > 0);
    }
    QCOMPARE(total, tv.y.size());
    const Matrix sub = take(tv, folds[0].val);
    QCOMPARE(sub.rows, int(folds[0].val.size()));
  }

  void metricsByHand() {
    const std::vector<int> yt{0, 0, 1, 1, 2, 2}, yp{0, 1, 1, 1, 2, 0};
    const Metrics m = computeMetrics(yt, yp, {}, 3);
    QVERIFY(std::abs(m.accuracy - 4.0 / 6.0) < 1e-12);
    // class 0: p 1/2 r 1/2 f 1/2; class 1: p 2/3 r 1 f 0.8; class 2: p 1 r 1/2
    // f 2/3
    QVERIFY(std::abs(m.precisionMacro - (0.5 + 2.0 / 3.0 + 1.0) / 3.0) < 1e-12);
    QVERIFY(std::abs(m.recallMacro - (0.5 + 1.0 + 0.5) / 3.0) < 1e-12);
    QVERIFY(std::abs(m.f1Macro - (0.5 + 0.8 + 2.0 / 3.0) / 3.0) < 1e-12);
    QVERIFY(std::isnan(m.rocAucMacro));
    const auto cm = confusionMatrix(yt, yp, 3);
    QCOMPARE(cm[0][1], 1);
    QCOMPARE(cm[2][0], 1);
    // AUC: perfect, random-ish, with ties
    QCOMPARE(rocAuc({0, 0, 1, 1}, {0.1, 0.2, 0.8, 0.9}), 1.0);
    QCOMPARE(rocAuc({0, 1, 0, 1}, {0.5, 0.5, 0.5, 0.5}), 0.5);
    QVERIFY(std::abs(rocAuc({0, 0, 1, 1}, {0.1, 0.8, 0.2, 0.9}) - 0.75) <
            1e-12);
    const RocCurve c = rocCurve({0, 0, 1, 1}, {0.1, 0.8, 0.2, 0.9});
    QCOMPARE(c.tpr.back(), 1.0);
    QCOMPARE(c.fpr.back(), 1.0);
    QVERIFY(std::abs(c.auc - 0.75) < 1e-12);
    // macro AUC from probabilities
    const std::vector<double> proba{0.9, 0.1, 0.0, 0.6, 0.3, 0.1,
                                    0.2, 0.7, 0.1, 0.3, 0.6, 0.1,
                                    0.1, 0.1, 0.8, 0.5, 0.2, 0.3};
    const Metrics m2 = computeMetrics(yt, yp, proba, 3);
    QVERIFY(m2.rocAucMacro > 0.8 && m2.rocAucMacro <= 1.0);
    QVERIFY(std::abs(logLoss({0, 1}, {0.5, 0.5, 0.25, 0.75}, 2) -
                     (-(std::log(0.5) + std::log(0.75)) / 2)) < 1e-12);
    QVERIFY(std::abs(m.value("f1_macro") - m.f1Macro) < 1e-15);
  }
};

QTEST_GUILESS_MAIN(TstSfcCore)
#include "tst_sfc_core.moc"
