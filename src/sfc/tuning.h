// How a model's parameter combinations are compared and the best one is
// reported: on the fixed validation days, or by grouped k-fold over the
// train + validation days with a refit on all of them. Port of tuning.py,
// with train_ann.py's epoch-wise early stopping for the network.
#pragma once

#include <QString>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

#include "sfc/dataset.h"
#include "sfc/metrics.h"
#include "sfc/models.h"
#include "sfc/settings.h"
#include "sfc/split.h"

namespace skg::sfc {

using LogFn = std::function<void(const QString &)>;
using StopFn = std::function<bool()>;

struct SplitMetrics {
  Metrics train, val, test;
  Metrics of(const QString &name) const {
    return name == "train" ? train : name == "val" ? val : test;
  }
};

struct TuneResult {
  QVariantMap params;
  Metrics
      valMetrics; // the validation metrics (cross-validation mean with folds)
  double selectStd = 0;         // std of select_by over the folds
  std::unique_ptr<Model> model; // trained on the training days; null with folds
  int bestEpoch = 0;            // the network
  std::vector<double> trainLoss, valLoss; // the network's history (first fold)
};

class Validation {
public:
  Validation(const SplitData &data, const Dataset &d, const Settings &s,
             int nClasses);
  bool cv() const { return cv_; }
  const Matrix &trainMatrix() const {
    return X_;
  } // train, or train + val with folds
  const Matrix &test() const { return test_; }

  using FitFn = std::function<std::unique_ptr<Model>(const Matrix &)>;
  // Every combination of the grid through `makeFit(params)`; the best by
  // select_by.
  TuneResult tune(const QString &name,
                  const std::function<FitFn(const QVariantMap &)> &makeFit,
                  const Grid &grid, const LogFn &log, const StopFn &stop);
  // The model to report: the tuned one, or the best parameters refitted on
  // train + val.
  std::unique_ptr<Model> refit(const FitFn &fit, TuneResult &best);
  // Train / val / test metrics of a model; with folds, val is the mean given.
  SplitMetrics evaluate(const Model &model, const Metrics *cvVal) const;
  // Parameters as logged to experiments.csv.
  QVariantMap loggedParams(const QVariantMap &params, double std) const;

  // The network: epoch-wise training with early stopping on the validation
  // loss (patience), per fold with cross-validation.
  TuneResult tuneAnn(const LogFn &log, const StopFn &stop);
  std::unique_ptr<Model> refitAnn(TuneResult &best);

  const Settings &settings() const { return s_; }
  int nClasses() const { return nClasses_; }

private:
  const Settings &s_;
  int nClasses_;
  bool cv_;
  Matrix X_, val_, test_;
  std::vector<Fold> folds_;
  Metrics metricsOf(const Model &m, const Matrix &x) const;
  Matrix balanced(const Matrix &m) const; // oversampled for the network
  struct AnnFit {
    MlpModel model;
    std::vector<double> trainLoss, valLoss;
    int bestEpoch = 0;
  };
  AnnFit trainOne(const QVariantMap &params, const Matrix &train,
                  const Matrix &val, const StopFn &stop) const;
};

// A fit function for a model name with class weights per sfc's rules
// (balanced sample weights for trees, SVM subsampling).
Validation::FitFn fitFor(const QString &name, const QVariantMap &params,
                         const Settings &s, int nClasses,
                         const StopFn &stop = {});

} // namespace skg::sfc
