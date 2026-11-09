#include "sfc/metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace skg::sfc {

double Metrics::value(const QString &name) const {
  if (name == QLatin1String("accuracy"))
    return accuracy;
  if (name == QLatin1String("precision_macro"))
    return precisionMacro;
  if (name == QLatin1String("recall_macro"))
    return recallMacro;
  if (name == QLatin1String("roc_auc_macro"))
    return rocAucMacro;
  return f1Macro;
}

std::vector<std::vector<int>> confusionMatrix(const std::vector<int> &yTrue,
                                              const std::vector<int> &yPred,
                                              int nClasses) {
  std::vector<std::vector<int>> cm(size_t(nClasses),
                                   std::vector<int>(size_t(nClasses), 0));
  for (size_t i = 0; i < yTrue.size(); ++i)
    if (yTrue[i] >= 0 && yTrue[i] < nClasses && yPred[i] >= 0 &&
        yPred[i] < nClasses)
      ++cm[size_t(yTrue[i])][size_t(yPred[i])];
  return cm;
}

double rocAuc(const std::vector<char> &positive,
              const std::vector<double> &score) {
  // Mann-Whitney U with average ranks for ties.
  const size_t n = score.size();
  std::vector<size_t> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
            [&](size_t a, size_t b) { return score[a] < score[b]; });
  std::vector<double> rank(n);
  for (size_t i = 0; i < n;) {
    size_t j = i;
    while (j + 1 < n && score[idx[j + 1]] == score[idx[i]])
      ++j;
    const double r = (double(i) + double(j)) / 2.0 + 1.0;
    for (size_t k = i; k <= j; ++k)
      rank[idx[k]] = r;
    i = j + 1;
  }
  double sumPos = 0;
  size_t nPos = 0;
  for (size_t i = 0; i < n; ++i)
    if (positive[i]) {
      sumPos += rank[i];
      ++nPos;
    }
  const size_t nNeg = n - nPos;
  if (nPos == 0 || nNeg == 0)
    return std::nan("");
  return (sumPos - double(nPos) * double(nPos + 1) / 2.0) /
         (double(nPos) * double(nNeg));
}

RocCurve rocCurve(const std::vector<char> &positive,
                  const std::vector<double> &score) {
  RocCurve c;
  const size_t n = score.size();
  std::vector<size_t> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(),
            [&](size_t a, size_t b) { return score[a] > score[b]; });
  size_t nPos = 0;
  for (char p : positive)
    nPos += p ? 1 : 0;
  const size_t nNeg = n - nPos;
  if (nPos == 0 || nNeg == 0)
    return c;
  c.fpr.push_back(0);
  c.tpr.push_back(0);
  size_t tp = 0, fp = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i;
    while (j + 1 < n && score[idx[j + 1]] == score[idx[i]])
      ++j;
    for (size_t k = i; k <= j; ++k)
      (positive[idx[k]] ? tp : fp)++;
    c.fpr.push_back(double(fp) / double(nNeg));
    c.tpr.push_back(double(tp) / double(nPos));
    i = j + 1;
  }
  c.auc = rocAuc(positive, score);
  return c;
}

Metrics computeMetrics(const std::vector<int> &yTrue,
                       const std::vector<int> &yPred,
                       const std::vector<double> &scores, int nClasses) {
  Metrics m;
  const size_t n = yTrue.size();
  if (n == 0)
    return m;
  const auto cm = confusionMatrix(yTrue, yPred, nClasses);
  size_t right = 0;
  for (size_t i = 0; i < n; ++i)
    right += yTrue[i] == yPred[i];
  m.accuracy = double(right) / double(n);
  double p = 0, r = 0, f = 0;
  for (int c = 0; c < nClasses; ++c) {
    int tp = cm[size_t(c)][size_t(c)], predicted = 0, actual = 0;
    for (int k = 0; k < nClasses; ++k) {
      predicted += cm[size_t(k)][size_t(c)];
      actual += cm[size_t(c)][size_t(k)];
    }
    const double prec = predicted ? double(tp) / predicted : 0.0;
    const double rec = actual ? double(tp) / actual : 0.0;
    p += prec;
    r += rec;
    f += (prec + rec) > 0 ? 2 * prec * rec / (prec + rec) : 0.0;
  }
  m.precisionMacro = p / nClasses;
  m.recallMacro = r / nClasses;
  m.f1Macro = f / nClasses;
  if (!scores.empty() && scores.size() == n * size_t(nClasses)) {
    double sum = 0;
    int counted = 0;
    for (int c = 0; c < nClasses; ++c) {
      std::vector<char> pos(n);
      std::vector<double> sc(n);
      size_t nPos = 0;
      for (size_t i = 0; i < n; ++i) {
        pos[i] = yTrue[i] == c;
        nPos += pos[i];
        sc[i] = scores[i * size_t(nClasses) + size_t(c)];
      }
      if (nPos == 0 || nPos == n)
        continue;
      sum += rocAuc(pos, sc);
      ++counted;
    }
    m.rocAucMacro = counted ? sum / counted : std::nan("");
  }
  return m;
}

double logLoss(const std::vector<int> &yTrue, const std::vector<double> &proba,
               int nClasses) {
  double sum = 0;
  for (size_t i = 0; i < yTrue.size(); ++i) {
    const double p = std::clamp(proba[i * size_t(nClasses) + size_t(yTrue[i])],
                                1e-15, 1.0 - 1e-15);
    sum -= std::log(p);
  }
  return yTrue.empty() ? 0.0 : sum / double(yTrue.size());
}

} // namespace skg::sfc
