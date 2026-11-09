#include "sfc/tuning.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace skg::sfc {

Validation::Validation(const SplitData &data, const Dataset &d,
                       const Settings &s, int nClasses)
    : s_(s), nClasses_(nClasses), cv_(s.cvFolds.has_value()), test_(data.test) {
  if (cv_) {
    X_ = concat(data.train, data.val);
    std::vector<QDate> groups;
    for (int i : X_.index)
      groups.push_back(d.date[size_t(i)]);
    folds_ =
        stratifiedGroupKFold(X_.y, groups, *s.cvFolds, quint64(s.randomSeed));
  } else {
    X_ = data.train;
    val_ = data.val;
  }
}

Metrics Validation::metricsOf(const Model &m, const Matrix &x) const {
  return computeMetrics(x.y, m.predict(x), m.proba(x), nClasses_);
}

namespace {
Metrics average(const std::vector<Metrics> &per) {
  Metrics m;
  if (per.empty())
    return m;
  double auc = 0;
  int aucN = 0;
  for (const Metrics &p : per) {
    m.accuracy += p.accuracy;
    m.precisionMacro += p.precisionMacro;
    m.recallMacro += p.recallMacro;
    m.f1Macro += p.f1Macro;
    if (!std::isnan(p.rocAucMacro)) {
      auc += p.rocAucMacro;
      ++aucN;
    }
  }
  const double n = double(per.size());
  m.accuracy /= n;
  m.precisionMacro /= n;
  m.recallMacro /= n;
  m.f1Macro /= n;
  m.rocAucMacro = aucN ? auc / aucN : std::nan("");
  return m;
}
double stdOf(const std::vector<double> &v) {
  if (v.empty())
    return 0;
  double mean = 0;
  for (double x : v)
    mean += x;
  mean /= double(v.size());
  double var = 0;
  for (double x : v)
    var += (x - mean) * (x - mean);
  return std::sqrt(var / double(v.size()));
}
} // namespace

TuneResult
Validation::tune(const QString &name,
                 const std::function<FitFn(const QVariantMap &)> &makeFit,
                 const Grid &grid, const LogFn &log, const StopFn &stop) {
  TuneResult best;
  bool have = false;
  for (const QVariantMap &params : expandGrid(grid)) {
    if (stop && stop())
      break;
    const FitFn fit = makeFit(params);
    Metrics metrics;
    std::unique_ptr<Model> model;
    double sd = 0;
    if (!cv_) {
      model = fit(X_);
      metrics = metricsOf(*model, val_);
    } else {
      std::vector<Metrics> per;
      std::vector<double> sel;
      for (const Fold &f : folds_) {
        if (stop && stop())
          break;
        auto m = fit(take(X_, f.train));
        per.push_back(metricsOf(*m, take(X_, f.val)));
        sel.push_back(per.back().value(s_.selectBy));
      }
      metrics = average(per);
      sd = stdOf(sel);
    }
    if (log)
      log(QStringLiteral("  %1 %2 -> val %3 %4%5")
              .arg(name, paramsText(params), s_.selectBy)
              .arg(metrics.value(s_.selectBy), 0, 'f', 3)
              .arg(cv_ ? QStringLiteral(" (std %1 over folds)")
                             .arg(sd, 0, 'f', 3)
                       : QString()));
    if (!have ||
        metrics.value(s_.selectBy) > best.valMetrics.value(s_.selectBy)) {
      best.params = params;
      best.valMetrics = metrics;
      best.selectStd = sd;
      best.model = std::move(model);
      have = true;
    }
  }
  return best;
}

std::unique_ptr<Model> Validation::refit(const FitFn &fit, TuneResult &best) {
  if (best.model)
    return std::move(best.model);
  return fit(X_);
}

SplitMetrics Validation::evaluate(const Model &model,
                                  const Metrics *cvVal) const {
  SplitMetrics r;
  r.train = metricsOf(model, X_);
  r.test = metricsOf(model, test_);
  r.val = cv_ && cvVal ? *cvVal : (cv_ ? Metrics() : metricsOf(model, val_));
  return r;
}

QVariantMap Validation::loggedParams(const QVariantMap &params,
                                     double sd) const {
  if (!cv_)
    return params;
  QVariantMap p = params;
  p.insert("cv_folds", *s_.cvFolds);
  p.insert(QStringLiteral("cv_%1_std").arg(s_.selectBy),
           std::round(sd * 10000) / 10000);
  return p;
}

// --- the network
// ---------------------------------------------------------------

Matrix Validation::balanced(const Matrix &m) const {
  if (!s_.annBalanceClasses)
    return m;
  std::vector<std::vector<int>> byClass(static_cast<size_t>(nClasses_));
  for (int r = 0; r < m.rows; ++r)
    byClass[size_t(m.y[size_t(r)])].push_back(r);
  size_t target = 0;
  for (const auto &c : byClass)
    target = std::max(target, c.size());
  Rng rng(quint64(s_.randomSeed));
  std::vector<int> rows;
  for (const auto &c : byClass) {
    if (c.empty())
      continue;
    rows.insert(rows.end(), c.begin(), c.end());
    for (size_t k = c.size(); k < target; ++k)
      rows.push_back(c[size_t(rng.below(int(c.size())))]);
  }
  return take(m, rows);
}

Validation::AnnFit Validation::trainOne(const QVariantMap &params,
                                        const Matrix &train, const Matrix &val,
                                        const StopFn &stop) const {
  AnnFit out{MlpModel(mlpOptions(params, s_, train.rows)), {}, {}, 0};
  out.model.name = QStringLiteral("ann");
  out.model.params = params;
  out.model.reset(train.cols, nClasses_);
  MlpModel best = out.model.clone();
  double bestLoss = std::numeric_limits<double>::infinity();
  int wait = 0;
  for (int e = 0; e < s_.annMaxEpochs; ++e) {
    if (stop && stop())
      break;
    out.model.epoch(train);
    out.trainLoss.push_back(
        logLoss(train.y, out.model.proba(train), nClasses_));
    out.valLoss.push_back(logLoss(val.y, out.model.proba(val), nClasses_));
    if (out.valLoss.back() < bestLoss) {
      bestLoss = out.valLoss.back();
      out.bestEpoch = e;
      wait = 0;
      best = out.model.clone();
    } else if (++wait >= s_.annPatience)
      break;
  }
  out.model = best.clone();
  return out;
}

TuneResult Validation::tuneAnn(const LogFn &log, const StopFn &stop) {
  TuneResult best;
  bool have = false;
  for (const QVariantMap &params : expandGrid(s_.annGrid)) {
    if (stop && stop())
      break;
    Metrics metrics;
    double sd = 0;
    int epochs = 0;
    std::unique_ptr<Model> model;
    std::vector<double> trainLoss, valLoss;
    if (!cv_) {
      AnnFit f = trainOne(params, balanced(X_), val_, stop);
      metrics = metricsOf(f.model, val_);
      epochs = f.bestEpoch + 1;
      trainLoss = f.trainLoss;
      valLoss = f.valLoss;
      model = std::make_unique<MlpModel>(f.model.clone());
    } else {
      std::vector<Metrics> per;
      std::vector<double> sel, ep;
      bool first = true;
      for (const Fold &fo : folds_) {
        if (stop && stop())
          break;
        const Matrix tr = take(X_, fo.train), va = take(X_, fo.val);
        AnnFit f = trainOne(params, balanced(tr), va, stop);
        per.push_back(metricsOf(f.model, va));
        sel.push_back(per.back().value(s_.selectBy));
        ep.push_back(f.bestEpoch + 1);
        if (first) {
          trainLoss = f.trainLoss;
          valLoss = f.valLoss;
          first = false;
        }
      }
      metrics = average(per);
      sd = stdOf(sel);
      double mean = 0;
      for (double e : ep)
        mean += e;
      epochs = ep.empty() ? 1 : int(std::lround(mean / double(ep.size())));
    }
    if (log)
      log(QStringLiteral("  %1 -> best epoch %2, val %3 %4%5")
              .arg(paramsText(params))
              .arg(epochs)
              .arg(s_.selectBy)
              .arg(metrics.value(s_.selectBy), 0, 'f', 3)
              .arg(cv_ ? QStringLiteral(" (std %1 over folds)")
                             .arg(sd, 0, 'f', 3)
                       : QString()));
    if (!have ||
        metrics.value(s_.selectBy) > best.valMetrics.value(s_.selectBy)) {
      best.params = params;
      best.valMetrics = metrics;
      best.selectStd = sd;
      best.bestEpoch = epochs;
      best.trainLoss = trainLoss;
      best.valLoss = valLoss;
      best.model = std::move(model);
      have = true;
    }
  }
  return best;
}

std::unique_ptr<Model> Validation::refitAnn(TuneResult &best) {
  if (best.model)
    return std::move(best.model);
  auto m = std::make_unique<MlpModel>(mlpOptions(best.params, s_, X_.rows));
  m->name = QStringLiteral("ann");
  m->params = best.params;
  m->nClasses = nClasses_;
  m->epochs = std::max(1, best.bestEpoch);
  m->fit(balanced(X_), {});
  return m;
}

Validation::FitFn fitFor(const QString &name, const QVariantMap &params,
                         const Settings &s, int nClasses) {
  return [name, params, &s, nClasses](const Matrix &X) {
    auto m = makeModel(name, params, s);
    m->nClasses = nClasses;
    if (name.startsWith(QLatin1String("svm_"))) {
      Matrix sub = X;
      if (s.svmMaxTrainWindows && X.rows > *s.svmMaxTrainWindows) {
        Rng rng(quint64(s.randomSeed));
        sub = take(X, rng.choice(X.rows, *s.svmMaxTrainWindows, false));
      }
      m->fit(sub, {});
      return m;
    }
    std::vector<double> w;
    if (s.classWeight && *s.classWeight == QLatin1String("balanced") &&
        name != QLatin1String("ann"))
      w = balancedWeights(X.y, nClasses);
    m->fit(X, w);
    return m;
  };
}

} // namespace skg::sfc
