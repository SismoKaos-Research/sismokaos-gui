#include "sfc/split.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace skg::sfc {

std::vector<QString> splitByDay(const Dataset &d, const Settings &s) {
  Rng rng(quint64(s.randomSeed));
  QMap<QDate, QString> daySplit;
  QMap<int, QList<QDate>> daysByLabel;
  for (int r = 0; r < d.rows(); ++r) {
    QList<QDate> &days = daysByLabel[d.label[size_t(r)]];
    if (!days.contains(d.date[size_t(r)]))
      days << d.date[size_t(r)];
  }
  for (auto it = daysByLabel.begin(); it != daysByLabel.end(); ++it) {
    QList<QDate> days = it.value();
    std::sort(days.begin(), days.end());
    const std::vector<int> order = rng.permutation(days.size());
    const int nVal = std::max(1, int(std::lround(days.size() * s.valSize)));
    const int nTrain = std::max(1, int(std::lround(days.size() * s.trainSize)));
    for (int rank = 0; rank < int(order.size()); ++rank)
      daySplit.insert(days[order[size_t(rank)]],
                      rank < nTrain          ? QStringLiteral("train")
                      : rank < nTrain + nVal ? QStringLiteral("val")
                                             : QStringLiteral("test"));
  }
  std::vector<QString> out(size_t(d.rows()));
  for (int r = 0; r < d.rows(); ++r)
    out[size_t(r)] = daySplit.value(d.date[size_t(r)]);
  return out;
}

namespace {
double quantile(std::vector<double> v, double q) {
  if (v.empty())
    return std::nan("");
  std::sort(v.begin(), v.end());
  const double pos = q * double(v.size() - 1);
  const size_t lo = size_t(std::floor(pos)), hi = size_t(std::ceil(pos));
  return v[lo] + (v[hi] - v[lo]) * (pos - double(lo));
}
} // namespace

void Scaler::fit(const Dataset &d, const std::vector<int> &rows,
                 const QString &k, std::optional<double> c) {
  kind = k;
  clip = c;
  const int n = d.nFeatures();
  center.assign(size_t(n), 0.0);
  scale.assign(size_t(n), 1.0);
  for (int f = 0; f < n; ++f) {
    std::vector<double> col;
    col.reserve(rows.size());
    for (int r : rows)
      col.push_back(d.at(r, f));
    if (col.empty())
      continue;
    if (kind == QLatin1String("minmax")) {
      const auto [lo, hi] = std::minmax_element(col.begin(), col.end());
      center[size_t(f)] = *lo;
      scale[size_t(f)] = (*hi - *lo) != 0 ? (*hi - *lo) : 1.0;
    } else if (kind == QLatin1String("standard")) {
      const double mean =
          std::accumulate(col.begin(), col.end(), 0.0) / double(col.size());
      double var = 0;
      for (double v : col)
        var += (v - mean) * (v - mean);
      const double sd = std::sqrt(var / double(col.size()));
      center[size_t(f)] = mean;
      scale[size_t(f)] = sd != 0 ? sd : 1.0;
    } else if (kind == QLatin1String("robust")) {
      center[size_t(f)] = quantile(col, 0.5);
      const double iqr = quantile(col, 0.75) - quantile(col, 0.25);
      scale[size_t(f)] = iqr != 0 ? iqr : 1.0;
    } else
      throw std::invalid_argument(("unknown scaler: " + kind).toStdString());
  }
}

std::vector<double> Scaler::transform(const double *row, int n) const {
  std::vector<double> out(static_cast<size_t>(n));
  for (int f = 0; f < n; ++f) {
    double v = (row[f] - center[size_t(f)]) / scale[size_t(f)];
    if (clip)
      v = std::clamp(v, -*clip, *clip);
    out[size_t(f)] = v;
  }
  return out;
}

QString Scaler::toJson() const {
  QJsonArray c, s;
  for (double v : center)
    c.append(v);
  for (double v : scale)
    s.append(v);
  QJsonObject o{{"kind", kind}, {"center", c}, {"scale", s}};
  if (clip)
    o.insert("clip", *clip);
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Scaler Scaler::fromJson(const QString &json) {
  const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
  Scaler sc;
  sc.kind = o.value("kind").toString();
  for (const QJsonValue &v : o.value("center").toArray())
    sc.center.push_back(v.toDouble());
  for (const QJsonValue &v : o.value("scale").toArray())
    sc.scale.push_back(v.toDouble());
  if (o.contains("clip"))
    sc.clip = o.value("clip").toDouble();
  return sc;
}

SplitData prepareData(const Dataset &d, const Settings &s) {
  SplitData out;
  out.split = splitByDay(d, s);
  std::vector<int> trainRows;
  for (int r = 0; r < d.rows(); ++r)
    if (out.split[size_t(r)] == QLatin1String("train"))
      trainRows.push_back(r);
  out.scaler.fit(d, trainRows, s.scaler, s.clipAfterScaling);
  for (Matrix *m : {&out.train, &out.val, &out.test})
    m->cols = d.nFeatures();
  for (int r = 0; r < d.rows(); ++r) {
    Matrix &m = out.split[size_t(r)] == QLatin1String("train") ? out.train
                : out.split[size_t(r)] == QLatin1String("val") ? out.val
                                                               : out.test;
    const std::vector<double> row = out.scaler.transform(
        d.X.data() + size_t(r) * size_t(d.nFeatures()), d.nFeatures());
    m.X.insert(m.X.end(), row.begin(), row.end());
    m.y.push_back(d.cls[size_t(r)]);
    m.index.push_back(r);
    ++m.rows;
  }
  return out;
}

QStringList splitSummary(const Dataset &d, const std::vector<QString> &split,
                         const Settings &s) {
  const QStringList parts{"train", "val", "test"};
  QMap<int, QMap<QString, QSet<QDate>>> days;
  QMap<int, QMap<QString, int>> windows;
  for (int r = 0; r < d.rows(); ++r) {
    days[d.cls[size_t(r)]][split[size_t(r)]].insert(d.date[size_t(r)]);
    ++windows[d.cls[size_t(r)]][split[size_t(r)]];
  }
  QStringList out;
  out << QStringLiteral("Days per class and split:")
      << QStringLiteral("Class  train  val  test");
  for (auto it = days.begin(); it != days.end(); ++it)
    out << QStringLiteral("%1  %2  %3  %4")
               .arg(it.key(), 5)
               .arg(it.value().value("train").size(), 5)
               .arg(it.value().value("val").size(), 3)
               .arg(it.value().value("test").size(), 4);
  out << QString() << QStringLiteral("Windows per class and split:")
      << QStringLiteral("Class  train  val  test");
  for (auto it = windows.begin(); it != windows.end(); ++it)
    out << QStringLiteral("%1  %2  %3  %4")
               .arg(it.key(), 5)
               .arg(it.value().value("train"), 5)
               .arg(it.value().value("val"), 3)
               .arg(it.value().value("test"), 4);
  Q_UNUSED(s);
  return out;
}

std::vector<Fold> stratifiedGroupKFold(const std::vector<int> &y,
                                       const std::vector<QDate> &groups, int k,
                                       quint64 seed) {
  // scikit-learn's algorithm: groups in order of the spread of their class
  // counts (largest first, ties shuffled), each assigned to the fold where
  // it least increases the per-class imbalance.
  const int nClasses = *std::max_element(y.begin(), y.end()) + 1;
  QMap<QDate, std::vector<int>> counts;
  QMap<QDate, std::vector<int>> members;
  for (size_t i = 0; i < y.size(); ++i) {
    auto &c = counts[groups[i]];
    if (c.empty())
      c.assign(size_t(nClasses), 0);
    ++c[size_t(y[i])];
    members[groups[i]].push_back(int(i));
  }
  std::vector<QDate> order(counts.keyBegin(), counts.keyEnd());
  Rng rng(seed);
  const std::vector<int> perm = rng.permutation(int(order.size()));
  std::vector<QDate> shuffled;
  for (int i : perm)
    shuffled.push_back(order[size_t(i)]);
  auto spread = [&](const QDate &g) {
    const std::vector<int> &c = counts[g];
    double mean = 0;
    for (int v : c)
      mean += v;
    mean /= double(c.size());
    double var = 0;
    for (int v : c)
      var += (v - mean) * (v - mean);
    return std::sqrt(var / double(c.size()));
  };
  std::stable_sort(
      shuffled.begin(), shuffled.end(),
      [&](const QDate &a, const QDate &b) { return spread(a) > spread(b); });
  std::vector<int> total(size_t(nClasses), 0);
  for (int v : y)
    ++total[size_t(v)];
  std::vector<std::vector<int>> foldCounts(
      size_t(k), std::vector<int>(size_t(nClasses), 0));
  std::vector<std::vector<int>> foldMembers(static_cast<size_t>(k));
  // Per-fold sample totals, for the tie-break.
  std::vector<int> foldSamples(size_t(k), 0);
  for (const QDate &g : shuffled) {
    int best = 0;
    double bestEval = std::numeric_limits<double>::infinity();
    for (int f = 0; f < k; ++f) {
      // Add g to fold f; the std over folds of every class's share, averaged
      // over the classes (scikit-learn's criterion).
      double eval = 0;
      for (int c = 0; c < nClasses; ++c) {
        if (!total[size_t(c)])
          continue;
        double mean = 0;
        std::vector<double> share(static_cast<size_t>(k));
        for (int o = 0; o < k; ++o) {
          const int cnt = foldCounts[size_t(o)][size_t(c)] +
                          (o == f ? counts[g][size_t(c)] : 0);
          share[size_t(o)] = double(cnt) / double(total[size_t(c)]);
          mean += share[size_t(o)];
        }
        mean /= k;
        double var = 0;
        for (double sh : share)
          var += (sh - mean) * (sh - mean);
        eval += std::sqrt(var / k);
      }
      eval /= nClasses;
      const bool better = eval < bestEval - 1e-12 ||
                          (std::abs(eval - bestEval) <= 1e-12 &&
                           foldSamples[size_t(f)] < foldSamples[size_t(best)]);
      if (better) {
        bestEval = eval;
        best = f;
      }
    }
    for (int c = 0; c < nClasses; ++c)
      foldCounts[size_t(best)][size_t(c)] += counts[g][size_t(c)];
    foldSamples[size_t(best)] += int(members[g].size());
    foldMembers[size_t(best)].insert(foldMembers[size_t(best)].end(),
                                     members[g].begin(), members[g].end());
  }
  std::vector<Fold> folds(static_cast<size_t>(k));
  for (int f = 0; f < k; ++f) {
    folds[size_t(f)].val = foldMembers[size_t(f)];
    std::sort(folds[size_t(f)].val.begin(), folds[size_t(f)].val.end());
    for (int o = 0; o < k; ++o)
      if (o != f)
        folds[size_t(f)].train.insert(folds[size_t(f)].train.end(),
                                      foldMembers[size_t(o)].begin(),
                                      foldMembers[size_t(o)].end());
    std::sort(folds[size_t(f)].train.begin(), folds[size_t(f)].train.end());
  }
  return folds;
}

Matrix concat(const Matrix &a, const Matrix &b) {
  Matrix m = a;
  m.X.insert(m.X.end(), b.X.begin(), b.X.end());
  m.y.insert(m.y.end(), b.y.begin(), b.y.end());
  m.index.insert(m.index.end(), b.index.begin(), b.index.end());
  m.rows += b.rows;
  m.cols = a.cols ? a.cols : b.cols;
  return m;
}

Matrix take(const Matrix &m, const std::vector<int> &rows) {
  Matrix out;
  out.cols = m.cols;
  for (int r : rows) {
    out.X.insert(out.X.end(), m.row(r), m.row(r) + m.cols);
    out.y.push_back(m.y[size_t(r)]);
    out.index.push_back(m.index[size_t(r)]);
    ++out.rows;
  }
  return out;
}

} // namespace skg::sfc
