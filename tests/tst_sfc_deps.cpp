// The classification port's third-party libraries, linked and exercised
// once: XGBoost's C API trains and predicts, libsvm trains and predicts,
// toml++ reads and writes.
#include <QtTest>

#include <cmath>
#include <sstream>
#include <vector>

#include "svm.h"
#include "toml.hpp"
#include "xgboost/c_api.h"

namespace {
// Two Gaussian blobs in 2-D, 200 points, deterministic.
struct Blobs {
  std::vector<float> x; // row-major, 2 columns
  std::vector<float> y;
  int n = 0;
};
Blobs blobs() {
  Blobs b;
  quint64 s = 7;
  auto rnd = [&] {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return double(s >> 11) / double(1ull << 53) - 0.5;
  };
  for (int i = 0; i < 200; ++i) {
    const int c = i % 2;
    b.x.push_back(float((c ? 2.0 : -2.0) + rnd()));
    b.x.push_back(float((c ? -1.0 : 1.0) + rnd()));
    b.y.push_back(float(c));
    ++b.n;
  }
  return b;
}
} // namespace

class TstSfcDeps : public QObject {
  Q_OBJECT
private slots:
  void xgboostTrainsAndPredicts() {
    const Blobs b = blobs();
    DMatrixHandle dtrain = nullptr;
    QCOMPARE(XGDMatrixCreateFromMat(b.x.data(), bst_ulong(b.n), 2, std::nanf(""), &dtrain), 0);
    QCOMPARE(XGDMatrixSetFloatInfo(dtrain, "label", b.y.data(), bst_ulong(b.n)), 0);
    BoosterHandle booster = nullptr;
    QCOMPARE(XGBoosterCreate(&dtrain, 1, &booster), 0);
    QCOMPARE(XGBoosterSetParam(booster, "objective", "binary:logistic"), 0);
    QCOMPARE(XGBoosterSetParam(booster, "max_depth", "2"), 0);
    QCOMPARE(XGBoosterSetParam(booster, "eta", "0.3"), 0);
    QCOMPARE(XGBoosterSetParam(booster, "nthread", "1"), 0);
    QCOMPARE(XGBoosterSetParam(booster, "verbosity", "0"), 0);
    for (int it = 0; it < 20; ++it)
      QCOMPARE(XGBoosterUpdateOneIter(booster, it, dtrain), 0);
    bst_ulong outLen = 0;
    const float *out = nullptr;
    QCOMPARE(XGBoosterPredict(booster, dtrain, 0, 0, 0, &outLen, &out), 0);
    QCOMPARE(int(outLen), b.n);
    int right = 0;
    for (int i = 0; i < b.n; ++i)
      right += (out[i] > 0.5f) == (b.y[size_t(i)] > 0.5f);
    QVERIFY(right >= 195);
    // Save and reload through the JSON model format.
    bst_ulong len = 0;
    const char *json = nullptr;
    QCOMPARE(XGBoosterSaveModelToBuffer(booster, R"({"format": "json"})", &len, &json), 0);
    QVERIFY(len > 100);
    BoosterHandle again = nullptr;
    QCOMPARE(XGBoosterCreate(nullptr, 0, &again), 0);
    QCOMPARE(XGBoosterLoadModelFromBuffer(again, json, len), 0);
    bst_ulong len2 = 0;
    const float *out2 = nullptr;
    QCOMPARE(XGBoosterPredict(again, dtrain, 0, 0, 0, &len2, &out2), 0);
    QCOMPARE(out2[0], out[0]);
    XGBoosterFree(again);
    XGBoosterFree(booster);
    XGDMatrixFree(dtrain);
  }

  void libsvmTrainsAndPredicts() {
    const Blobs b = blobs();
    std::vector<std::vector<svm_node>> nodes(size_t(b.n), std::vector<svm_node>(3));
    std::vector<svm_node *> rows(size_t(b.n));
    std::vector<double> labels(size_t(b.n));
    for (int i = 0; i < b.n; ++i) {
      nodes[size_t(i)][0] = {1, b.x[size_t(2 * i)]};
      nodes[size_t(i)][1] = {2, b.x[size_t(2 * i + 1)]};
      nodes[size_t(i)][2] = {-1, 0};
      rows[size_t(i)] = nodes[size_t(i)].data();
      labels[size_t(i)] = b.y[size_t(i)];
    }
    svm_problem prob{b.n, labels.data(), rows.data()};
    svm_parameter p{};
    p.svm_type = C_SVC;
    p.kernel_type = RBF;
    p.gamma = 0.5;
    p.C = 1;
    p.cache_size = 16;
    p.eps = 1e-3;
    p.shrinking = 1;
    p.probability = 1;
    QVERIFY(svm_check_parameter(&prob, &p) == nullptr);
    svm_set_print_string_function([](const char *) {});
    svm_model *model = svm_train(&prob, &p);
    QVERIFY(model != nullptr);
    QCOMPARE(svm_get_nr_class(model), 2);
    int right = 0;
    double prob2[2];
    for (int i = 0; i < b.n; ++i) {
      const double pred = svm_predict_probability(model, rows[size_t(i)], prob2);
      right += int(pred) == int(labels[size_t(i)]);
    }
    QVERIFY(right >= 195);
    QVERIFY(std::abs(prob2[0] + prob2[1] - 1.0) < 1e-9);
    svm_free_and_destroy_model(&model);
  }

  void tomlReadsAndWrites() {
    const auto tbl = toml::parse(R"(
station = "ELZG"
cv_folds = 5
feature_columns = ["raw_std", "wolf_lye"]
[tree_grids.xgboost]
max_depth = [3, 6]
)");
    QCOMPARE(QString::fromStdString(std::string(tbl["station"].value_or(""))), QStringLiteral("ELZG"));
    QCOMPARE(int(tbl["cv_folds"].value_or(0)), 5);
    QCOMPARE(int(tbl["feature_columns"].as_array()->size()), 2);
    QCOMPARE(int(tbl["tree_grids"]["xgboost"]["max_depth"].as_array()->size()), 2);
    std::ostringstream out;
    out << tbl;
    QVERIFY(out.str().find("station = ") != std::string::npos && out.str().find("ELZG") != std::string::npos);
  }
};

QTEST_GUILESS_MAIN(TstSfcDeps)
#include "tst_sfc_deps.moc"
