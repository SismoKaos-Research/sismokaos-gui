// A whole sfc run on synthetic data: every step writes what the Python tool
// writes, experiments.csv gets one row per model, the comparison ranks
// them, the error timeline follows the best model, the HTML report embeds
// everything; and the experiments helpers read it all back.
#include <QtTest>

#include <cmath>

#include "sfc/charts.h"
#include "sfc/experiments.h"
#include "sfc/run.h"

using namespace skg::sfc;

namespace {
Dataset blobs(int days) {
  Dataset d;
  d.featureNames = {"a", "b", "c"};
  Rng rng(5);
  auto gauss = [&] {
    const double u = std::max(1e-12, rng.uniform()), v = rng.uniform();
    return std::sqrt(-2 * std::log(u)) * std::cos(2 * M_PI * v);
  };
  const double centers[3][3] = {{0, 0, 0}, {3, 0, 1}, {0, 3, -1}};
  for (int day = 0; day < days; ++day) {
    const int label = day % 3;
    for (int w = 0; w < 30; ++w) {
      for (int f = 0; f < 3; ++f)
        d.X.push_back(centers[label][f] + gauss());
      d.label.push_back(label);
      d.cls.push_back(label);
      d.date.push_back(QDate(2020, 1, 1).addDays(day));
      d.daysToEq.push_back(days - day);
      d.windowStart.push_back(QString());
    }
  }
  return d;
}

Settings quick() {
  Settings s;
  s.station = "SYN";
  s.earthquakeName = "test_event";
  s.eqDate = QDate(2020, 2, 15);
  s.classTable = {{0, 1, 10, 1, 10}, {1, 11, 20, 1, 10}, {2, 21, 30, 1, 10}};
  s.featureColumns = {"a", "b", "c"};
  s.cvFolds.reset();
  s.scaler = "standard";
  s.runName = "quick";
  s.modelsToRun = {"logistic", "svm_rbf"};
  s.logisticGrid = Grid{{"C", {1, 10}}};
  s.svmGrids = {
      {"svm_rbf", Grid{{"C", {1}}, {"gamma", {QStringLiteral("scale")}}}}};
  s.treeModelsToRun = {"random_forest", "xgboost"};
  s.treeGrids = {{"random_forest", Grid{{"n_estimators", {30}},
                                        {"max_depth", {6}},
                                        {"min_samples_leaf", {1}}}},
                 {"xgboost", Grid{{"n_estimators", {30}},
                                  {"learning_rate", {0.2}},
                                  {"max_depth", {3}}}}};
  s.annGrid = Grid{{"hidden_layer_sizes", {QVariantList{8}}},
                   {"batch_size", {64}},
                   {"alpha", {0.001}},
                   {"learning_rate_init", {0.01}}};
  s.annMaxEpochs = 30;
  s.annPatience = 5;
  s.scatterPairs = {{"a", "b"}};
  s.scatterTriples = {{"a", "b", "c"}};
  s.scatterTripleFeatures.clear();
  return s;
}
} // namespace

class TstSfcRun : public QObject {
  Q_OBJECT
private slots:
  void chartsRender() {
    QVERIFY(!classDistributionFigure({"0", "1"}, {10, 5}, {400, 200}).isNull());
    const QImage cm = confusionFigure({{10, 2}, {1, 7}}, {"0", "1"}, "test");
    QCOMPARE(cm.size(), QSize(750, 675));
    QVERIFY(
        !rocFigure({{"Class 0 (AUC 0.90)", {0, 0.2, 1}, {0, 0.8, 1}}}, "roc")
             .isNull());
    QVERIFY(!comparisonFigure({"a", "b"}, {0.9, 0.8}, {0.8, 0.7}, {0.75, 0.7})
                 .isNull());
    QVERIFY(!lossFigure({1, 0.5, 0.3}, {1, 0.6, 0.5}, "loss").isNull());
    // PCA recovers the dominant direction
    std::vector<double> X;
    for (int i = 0; i < 200; ++i) {
      const double t = i / 10.0;
      X.push_back(t);
      X.push_back(2 * t + 0.01 * ((i % 3) - 1));
    }
    const Pca pca = pcaFit(X, 200, 2, 2);
    QVERIFY(pca.explained[0] > 0.99);
    QVERIFY(std::abs(std::abs(pca.components[0][1] / pca.components[0][0]) -
                     2.0) < 0.05);
    QCOMPARE(int(pcaTransform(pca, X, 200, 2).size()), 400);
    QCOMPARE(percentileOf({3, 1, 2}, 0.5), 2.0);
    QCOMPARE(tickLabel(0.5), QStringLiteral("0.5"));
    QCOMPARE(tickLabel(100), QStringLiteral("100"));
  }

  void experimentsCsvRoundTrip() {
    const QTemporaryDir dir;
    const QString csv = dir.filePath("experiments.csv");
    Metrics a, b, c;
    a.accuracy = 0.9;
    a.f1Macro = 0.88;
    a.rocAucMacro = 0.95;
    b = a;
    b.f1Macro = 0.8;
    c = a;
    c.f1Macro = 0.79;
    QVERIFY(logExperiment(csv, "run_01", "logistic", {{"C", 10}}, {"a", "b"}, a,
                          b, c));
    QVERIFY(logExperiment(csv, "run_01", "svm_rbf",
                          {{"C", 1}, {"gamma", "scale"}}, {"a", "b"}, a, c, b));
    QVERIFY(logExperiment(csv, "run_01", "logistic", {{"C", 1}}, {"a", "b"}, a,
                          c, c)); // a rerun: the latest counts
    const auto all = loadTrials(csv, false);
    QCOMPARE(int(all.size()), 3);
    const auto latest = loadTrials(csv);
    QCOMPARE(int(latest.size()), 2);
    QCOMPARE(latest[0].model, QStringLiteral("logistic"));
    QCOMPARE(latest[0].params, QStringLiteral("{'C': 1}"));
    QVERIFY(std::abs(latest[0].valF1 - 0.79) < 1e-9);
    QCOMPARE(latest[1].params, QStringLiteral("{'C': 1, 'gamma': 'scale'}"));
    QCOMPARE(latest[0].nFeatures, 2);
    const auto best = bestPerModel(latest);
    QCOMPARE(
        best[0].model,
        QStringLiteral("logistic")); // 0.79 vs svm's 0.79? svm val is c = 0.79
                                     // too -> order by val desc, stable
    QVERIFY(writeComparisonCsv(dir.filePath("cmp.csv"), best));
    QVERIFY(writeConfusionCsv(dir.filePath("cm.csv"), {{5, 1}, {2, 8}},
                              {"0", "1"}));
    QStringList labels;
    const auto cm = readConfusionCsv(dir.filePath("cm.csv"), &labels);
    QVERIFY(cm.has_value());
    QCOMPARE((*cm)[1][0], 2);
    QCOMPARE(labels, (QStringList{"0", "1"}));
    QCOMPARE(removeRun(dir.path(), "run_01"), 3);
    QVERIFY(!QFileInfo::exists(csv));
    // change texts
    Settings p = quick(), r = quick();
    r.featureColumns = {"a", "c", "d"};
    r.cvFolds = 3;
    r.station = "OTHER"; // a project key: not reported
    const QStringList ch = changeTexts(r.asMap(), p.asMap());
    QCOMPARE(ch.size(), 2);
    QVERIFY(ch.contains("cv_folds = 3"));
    QVERIFY(ch.contains("feature_columns -b +d"));
  }

  void wholeRun() {
    // SKG_SFC_DUMP=<dir> keeps the project there for a look at the figures.
    const QTemporaryDir tmp;
    const QByteArray dump = qgetenv("SKG_SFC_DUMP");
    const QString projectPath =
        dump.isEmpty() ? tmp.path() : QString::fromLocal8Bit(dump);
    QDir().mkpath(projectPath);
    const Settings s = quick();
    Dataset d = blobs(30);
    prepareClasses(d, s);
    QStringList log;
    RunHooks hooks;
    hooks.log = [&](const QString &l) { log << l; };
    int progressCalls = 0;
    hooks.progress = [&](int, int, const QString &) { ++progressCalls; };
    const auto outcomes = runSteps(projectPath, s, d, allSteps(), hooks);
    QCOMPARE(int(outcomes.size()), 8);
    for (const StepOutcome &o : outcomes)
      QVERIFY2(o.status == "OK", qPrintable(o.step + ": " + o.error));
    QVERIFY(progressCalls > 8);
    const QString run = runDir(projectPath, s);
    QVERIFY(run.endsWith("/results/SYN/test_event/N/models/quick"));
    for (const char *f : {"settings.toml",
                          "data_understanding_report.txt",
                          "data_understanding/class_distribution.png",
                          "data_understanding/boxplots_per_class.png",
                          "data_understanding/histograms.png",
                          "data_understanding/correlation_matrix.png",
                          "split_report.txt",
                          "split_days.csv",
                          "split_visualization/split_counts.png",
                          "split_visualization/feature_distributions.png",
                          "split_visualization/pca_scatter_2d.png",
                          "split_visualization/scatter_2d/scatter_a_b.png",
                          "split_visualization/scatter_3d/scatter_a_b_c.png",
                          "baseline_report.txt",
                          "logistic_confusion_matrix.csv",
                          "logistic_confusion_matrix.png",
                          "logistic_roc_curves.png",
                          "logistic_model.json",
                          "svm_rbf_model.json",
                          "ann_report.txt",
                          "ann_loss_curve.png",
                          "ann_model.json",
                          "trees_report.txt",
                          "random_forest_model.json",
                          "xgboost_roc_curves.png",
                          "error_timeline_report.txt",
                          "error_timeline/daily_error_rates.csv",
                          "error_timeline/error_timeline_class0.png",
                          "error_timeline/error_timeline_class2.png"})
      QVERIFY2(QFileInfo::exists(run + '/' + f), f);
    const QString models = modelsDir(projectPath, s);
    QVERIFY(QFileInfo::exists(models + "/experiments.csv"));
    QVERIFY(QFileInfo::exists(models + "/model_comparison.csv"));
    QVERIFY(QFileInfo::exists(models + "/model_comparison.png"));
    QVERIFY(QFileInfo::exists(models + "/model_comparison_report.txt"));
    const auto trials = loadTrials(models + "/experiments.csv");
    QCOMPARE(int(trials.size()),
             5); // logistic, svm_rbf, ann, random_forest, xgboost
    for (const Trial &t : trials) {
      QVERIFY2(
          t.testF1 > 0.8,
          qPrintable(t.model + QStringLiteral(" test f1 %1").arg(t.testF1)));
      QCOMPARE(t.features, QStringLiteral("a b c"));
    }
    QCOMPARE(runNames(models), QStringList{"quick"});
    const auto settings = runSettings(models, "quick");
    QVERIFY(settings.has_value());
    QCOMPARE(settings->value("run_name").toString(), QStringLiteral("quick"));
    const auto summary = summarize(models, s.asMap());
    QCOMPARE(int(summary.size()), 1);
    QCOMPARE(summary[0].models, 5);
    QVERIFY(summary[0].changes.has_value() && summary[0].changes->isEmpty());
    // split_days: one row per day, all three splits present
    QFile sd(run + "/split_days.csv");
    QVERIFY(sd.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(sd.readAll());
    QCOMPARE(text.count('\n'), 31);
    QVERIFY(text.contains(",train") && text.contains(",val") &&
            text.contains(",test"));
    // the report
    const QString html = writeHtmlReport(projectPath, s);
    QVERIFY(html.endsWith("/report.html"));
    QFile h(html);
    QVERIFY(h.open(QIODevice::ReadOnly));
    const QString page = QString::fromUtf8(h.readAll());
    QVERIFY(page.contains("SYN / test_event"));
    QVERIFY(page.contains("id=\"quick\""));
    QVERIFY(page.contains("data:image/png;base64,"));
    QVERIFY(page.contains("Confusion matrices"));
    QVERIFY(log.join('\n').contains("=== Tuning logistic ==="));
    QVERIFY(log.join('\n').contains("SUMMARY (quick)"));
    // a second run with merged labels differs from the project in one setting
    Settings s2 = s;
    s2.runName = "merged";
    s2.labelMerge = QMap<int, int>{{0, 0}, {1, 1}, {2, 1}};
    Dataset d2 = blobs(30);
    prepareClasses(d2, s2);
    const auto out2 =
        runSteps(projectPath, s2, d2, {"split", "trees", "compare"}, hooks);
    QCOMPARE(int(out2.size()), 3);
    for (const StepOutcome &o : out2)
      QVERIFY2(o.status == "OK", qPrintable(o.step + ": " + o.error));
    const auto summary2 = summarize(models, s.asMap());
    QCOMPARE(int(summary2.size()), 2);
    QVERIFY(summary2[1].changes.has_value());
    QVERIFY(summary2[1].changes->join(" ").contains("label_merge"));
    QCOMPARE(int(loadTrials(models + "/experiments.csv").size()), 7);
    QCOMPARE(removeRun(models, "merged"), 2);
    QCOMPARE(runNames(models), QStringList{"quick"});
  }
};

QTEST_MAIN(TstSfcRun) // the figures need a QGuiApplication for fonts
#include "tst_sfc_run.moc"
