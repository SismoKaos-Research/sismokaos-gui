// The classifiers behind one interface on separable synthetic data: each
// learns three blobs, saves and reloads; the tuning picks the better of two
// parameter sets on the validation days and by cross-validation; the network
// stops early.
#include <QtTest>

#include <cmath>

#include "sfc/models.h"
#include "sfc/tuning.h"

using namespace skg::sfc;

namespace {
// Three Gaussian blobs in 3-D over `days` days, 40 windows a day, label = blob.
Dataset blobs(int days, quint64 seed = 11) {
  Dataset d;
  d.featureNames = {"a", "b", "c"};
  Rng rng(seed);
  auto gauss = [&] {
    const double u = std::max(1e-12, rng.uniform()), v = rng.uniform();
    return std::sqrt(-2 * std::log(u)) * std::cos(2 * M_PI * v);
  };
  const double centers[3][3] = {{0, 0, 0}, {4, 0, 2}, {0, 4, -2}};
  for (int day = 0; day < days; ++day) {
    const int label = day % 3;
    for (int w = 0; w < 40; ++w) {
      for (int f = 0; f < 3; ++f)
        d.X.push_back(centers[label][f] + 0.8 * gauss());
      d.label.push_back(label);
      d.cls.push_back(label);
      d.date.push_back(QDate(2020, 1, 1).addDays(day));
      d.daysToEq.push_back(days - day);
      d.windowStart.push_back(QString());
    }
  }
  return d;
}

Settings quickSettings() {
  Settings s;
  s.classTable = {{0, 1, 10, 1, 10}, {1, 11, 20, 1, 10}, {2, 21, 30, 1, 10}};
  s.featureColumns = {"a", "b", "c"};
  s.cvFolds.reset();
  s.scaler = "standard";
  return s;
}
} // namespace

class TstSfcModels : public QObject {
  Q_OBJECT
private slots:
  void everyModelLearnsTheBlobs() {
    const Dataset d = blobs(30);
    const Settings s = quickSettings();
    const SplitData data = prepareData(d, s);
    QVERIFY(data.train.rows > 300 && data.test.rows > 100);
    const QTemporaryDir dir;
    struct Case {
      QString name;
      QVariantMap params;
      double minAccuracy;
    };
    const QList<Case> cases{
        {"logistic", {{"C", 10}}, 0.9},
        {"svm_linear", {{"C", 1}}, 0.9},
        {"svm_poly", {{"C", 1}, {"degree", 3}}, 0.85},
        {"svm_rbf", {{"C", 1}, {"gamma", "scale"}}, 0.9},
        {"random_forest",
         {{"n_estimators", 50}, {"max_depth", 10}, {"min_samples_leaf", 1}},
         0.9},
        {"gradient_boosting",
         {{"n_estimators", 50}, {"learning_rate", 0.1}, {"max_depth", 2}},
         0.9},
        {"xgboost",
         {{"n_estimators", 50}, {"learning_rate", 0.1}, {"max_depth", 3}},
         0.9},
        {"ann",
         {{"hidden_layer_sizes", QVariantList{16}},
          {"batch_size", 64},
          {"alpha", 0.001},
          {"learning_rate_init", 0.01}},
         0.9},
    };
    for (const Case &c : cases) {
      auto fit = fitFor(c.name, c.params, s, 3);
      std::unique_ptr<Model> m;
      if (c.name == "ann") {
        m = makeModel(c.name, c.params, s);
        m->nClasses = 3;
        static_cast<MlpModel *>(m.get())->epochs = 60;
        m->fit(data.train, {});
      } else
        m = fit(data.train);
      const Metrics test = computeMetrics(data.test.y, m->predict(data.test),
                                          m->proba(data.test), 3);
      QVERIFY2(test.accuracy >= c.minAccuracy,
               qPrintable(QStringLiteral("%1: accuracy %2")
                              .arg(c.name)
                              .arg(test.accuracy)));
      QVERIFY2(
          test.rocAucMacro > 0.95,
          qPrintable(
              QStringLiteral("%1: auc %2").arg(c.name).arg(test.rocAucMacro)));
      // probabilities sum to one
      const std::vector<double> p = m->proba(data.test);
      for (int r = 0; r < std::min(5, data.test.rows); ++r)
        QVERIFY(std::abs(p[size_t(r) * 3] + p[size_t(r) * 3 + 1] +
                         p[size_t(r) * 3 + 2] - 1.0) < 1e-6);
      // save and reload gives the same predictions
      const QString path = dir.filePath(c.name + "_model.json");
      QVERIFY(m->saveTo(path));
      auto again = loadModel(path, s);
      QCOMPARE(again->name, c.name);
      QCOMPARE(again->nClasses, 3);
      QVERIFY(again->predict(data.test) == m->predict(data.test));
    }
  }

  void balancedWeightsAndParamsText() {
    const auto w = balancedWeights({0, 0, 0, 1}, 2);
    QVERIFY(std::abs(w[0] - 4.0 / (2 * 3)) < 1e-12);
    QVERIFY(std::abs(w[3] - 4.0 / (2 * 1)) < 1e-12);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             makeModel("nope", {}, Settings()));
  }

  void tuningPicksTheBetterParameters() {
    const Dataset d = blobs(30);
    Settings s = quickSettings();
    const SplitData data = prepareData(d, s);
    QStringList log;
    // Fixed validation days: an absurdly small C loses to a sane one.
    {
      Validation v(data, d, s, 3);
      QVERIFY(!v.cv());
      auto makeFit = [&](const QVariantMap &p) {
        return fitFor("logistic", p, s, 3);
      };
      TuneResult best = v.tune("logistic", makeFit, Grid{{"C", {1e-6, 10}}},
                               [&](const QString &l) { log << l; }, {});
      QCOMPARE(best.params.value("C").toDouble(), 10.0);
      QVERIFY(best.model);
      QCOMPARE(log.size(), 2);
      auto model = v.refit(makeFit(best.params), best);
      const SplitMetrics sm = v.evaluate(*model, nullptr);
      QVERIFY(sm.test.f1Macro > 0.85 && sm.val.f1Macro > 0.85);
      QCOMPARE(v.loggedParams(best.params, 0).size(), 1);
    }
    // Cross-validation: five folds over train + val, refit on all of them.
    {
      s.cvFolds = 3;
      Validation v(data, d, s, 3);
      QVERIFY(v.cv());
      auto makeFit = [&](const QVariantMap &p) {
        return fitFor("xgboost", p, s, 3);
      };
      TuneResult best = v.tune("xgboost", makeFit,
                               Grid{{"n_estimators", {20}},
                                    {"max_depth", {2, 4}},
                                    {"learning_rate", {0.2}}},
                               {}, {});
      QVERIFY(!best.model); // not trained on the full set yet
      auto model = v.refit(makeFit(best.params), best);
      QVERIFY(model);
      const SplitMetrics sm = v.evaluate(*model, &best.valMetrics);
      QVERIFY(sm.test.accuracy > 0.85);
      QVERIFY(std::abs(sm.val.f1Macro - best.valMetrics.f1Macro) < 1e-12);
      const QVariantMap logged = v.loggedParams(best.params, best.selectStd);
      QCOMPARE(logged.value("cv_folds").toInt(), 3);
      QVERIFY(logged.contains("cv_f1_macro_std"));
    }
  }

  void networkStopsEarlyAndRefits() {
    const Dataset d = blobs(30);
    Settings s = quickSettings();
    s.annGrid = Grid{{"hidden_layer_sizes", {QVariantList{8}}},
                     {"batch_size", {64}},
                     {"alpha", {0.001}},
                     {"learning_rate_init", {0.01}}};
    s.annMaxEpochs = 100;
    s.annPatience = 5;
    const SplitData data = prepareData(d, s);
    Validation v(data, d, s, 3);
    TuneResult best = v.tuneAnn({}, {});
    QVERIFY(best.model);
    QVERIFY(best.bestEpoch >= 1);
    QVERIFY(int(best.valLoss.size()) <= 100);
    QVERIFY(best.valLoss.front() > best.valLoss[size_t(best.bestEpoch)]);
    auto model = v.refitAnn(best);
    const SplitMetrics sm = v.evaluate(*model, nullptr);
    QVERIFY(sm.test.accuracy > 0.9);
    // with folds the network is refitted for the mean best epoch
    s.cvFolds = 3;
    Validation vc(data, d, s, 3);
    TuneResult bc = vc.tuneAnn({}, {});
    QVERIFY(!bc.model);
    auto refit = vc.refitAnn(bc);
    QVERIFY(vc.evaluate(*refit, &bc.valMetrics).test.accuracy > 0.9);
  }
};

QTEST_GUILESS_MAIN(TstSfcModels)
#include "tst_sfc_models.moc"
