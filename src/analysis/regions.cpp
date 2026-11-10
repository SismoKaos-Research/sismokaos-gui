#include "analysis/regions.h"

#include <QDir>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include "fx/csv.h"

namespace skg::analysis {

namespace {
// SplitMix64: small, fast, and the same everywhere.
class Rng {
public:
  explicit Rng(quint64 seed) : s_(seed) {}
  quint64 next() {
    quint64 z = (s_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  size_t below(size_t n) { return static_cast<size_t>(next() % n); }

private:
  quint64 s_;
};

double dist2(const Point2 &a, const Point2 &b) {
  const double d0 = a[0] - b[0], d1 = a[1] - b[1];
  return d0 * d0 + d1 * d1;
}

std::runtime_error fail(const QString &message) {
  return std::runtime_error(message.toStdString());
}
} // namespace

PointTable loadPointTable(const QString &path) {
  const QStringList all = fx::lines(fx::readMaybeGz(path));
  if (all.isEmpty())
    throw fail(path + QStringLiteral(" is empty"));
  PointTable t;
  t.headerLine = all.first();
  if (t.headerLine.startsWith(QChar(0xFEFF)))
    t.headerLine.remove(0, 1);
  t.header = fx::splitCsv(t.headerLine);
  t.latCol = fx::column(t.header, {"latitude", "enlem", "lat"});
  t.lonCol = fx::column(t.header, {"longitude", "boylam", "lon"});
  if (t.latCol < 0 || t.lonCol < 0)
    throw fail(QStringLiteral("%1 needs latitude and longitude columns "
                              "(latitude/enlem, longitude/boylam); its "
                              "columns are %2")
                   .arg(path, t.header.join(QStringLiteral(", "))));
  for (qsizetype i = 1; i < all.size(); ++i) {
    if (all[i].trimmed().isEmpty())
      continue;
    const QStringList r = fx::splitCsv(all[i]);
    const auto la = fx::number(r, t.latCol), lo = fx::number(r, t.lonCol);
    if (!la || !lo || !std::isfinite(*la) || !std::isfinite(*lo))
      continue;
    t.rows << all[i];
    t.points.push_back({*la, *lo});
  }
  return t;
}

int Som::winner(const Point2 &x) const {
  int best = 0;
  double bestD = std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < weights.size(); ++i) {
    const double d = dist2(weights[i], x);
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

Som trainSom(const std::vector<Point2> &x, const SomOptions &o) {
  if (x.empty())
    throw fail(QStringLiteral("no points to train the map on"));
  if (o.rows < 1 || o.cols < 1 || o.iterations < 1 || o.sigma <= 0 ||
      o.learningRate <= 0)
    throw fail(QStringLiteral("the map needs a positive size, iteration "
                              "count, width and rate"));
  Rng rng(o.seed);
  Som som;
  som.rows = o.rows;
  som.cols = o.cols;
  const size_t units =
      static_cast<size_t>(o.rows) * static_cast<size_t>(o.cols);
  som.weights.resize(units);
  for (Point2 &w : som.weights)
    w = x[rng.below(x.size())];
  std::vector<double> gr(static_cast<size_t>(o.rows)),
      gc(static_cast<size_t>(o.cols));
  for (int t = 0; t < o.iterations; ++t) {
    const Point2 &sample = x[rng.below(x.size())];
    const int win = som.winner(sample);
    const int wr = win / o.cols, wc = win % o.cols;
    const double decay = 1.0 + double(t) / (double(o.iterations) / 2.0);
    const double eta = o.learningRate / decay, sig = o.sigma / decay;
    const double d = 2.0 * sig * sig;
    for (int r = 0; r < o.rows; ++r)
      gr[static_cast<size_t>(r)] = std::exp(-double((r - wr) * (r - wr)) / d);
    for (int c = 0; c < o.cols; ++c)
      gc[static_cast<size_t>(c)] = std::exp(-double((c - wc) * (c - wc)) / d);
    for (int r = 0; r < o.rows; ++r)
      for (int c = 0; c < o.cols; ++c) {
        const double g =
            eta * gr[static_cast<size_t>(r)] * gc[static_cast<size_t>(c)];
        Point2 &w = som.weights[static_cast<size_t>(r * o.cols + c)];
        w[0] += g * (sample[0] - w[0]);
        w[1] += g * (sample[1] - w[1]);
      }
  }
  return som;
}

double estimateBandwidth(const std::vector<Point2> &x, double quantile) {
  if (x.empty())
    throw fail(QStringLiteral("no points to estimate a bandwidth from"));
  const size_t n = x.size();
  const size_t k =
      std::clamp<size_t>(static_cast<size_t>(double(n) * quantile), 1, n);
  std::vector<double> d(n);
  double sum = 0;
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = 0; j < n; ++j)
      d[j] = dist2(x[i], x[j]);
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(k - 1),
                     d.end());
    sum += std::sqrt(d[k - 1]);
  }
  return sum / double(n);
}

MeanShiftResult meanShift(const std::vector<Point2> &x, double bandwidth,
                          int maxIterations) {
  if (x.empty())
    throw fail(QStringLiteral("no points to cluster"));
  if (!(bandwidth > 0))
    throw fail(QStringLiteral("the Mean Shift bandwidth is not positive; "
                              "raise the quantile"));
  const double bw2 = bandwidth * bandwidth;
  // Seeds: the occupied bins of a grid of the bandwidth's size.
  std::map<std::pair<long long, long long>, int> bins;
  for (const Point2 &p : x)
    ++bins[{static_cast<long long>(std::nearbyint(p[0] / bandwidth)),
            static_cast<long long>(std::nearbyint(p[1] / bandwidth))}];
  std::vector<Point2> seeds;
  if (bins.size() == x.size())
    seeds = x;
  else
    for (const auto &b : bins)
      seeds.push_back({double(b.first.first) * bandwidth,
                       double(b.first.second) * bandwidth});
  // Climb from every seed.
  struct Peak {
    Point2 center;
    int count;
  };
  std::vector<Peak> peaks;
  const double stop = 1e-3 * bandwidth;
  for (Point2 mean : seeds) {
    int count = 0;
    for (int it = 0;; ++it) {
      Point2 sum{0, 0};
      count = 0;
      for (const Point2 &p : x)
        if (dist2(p, mean) <= bw2) {
          sum[0] += p[0];
          sum[1] += p[1];
          ++count;
        }
      if (count == 0)
        break;
      const Point2 next{sum[0] / count, sum[1] / count};
      const double moved = std::sqrt(dist2(next, mean));
      mean = next;
      if (moved <= stop || it == maxIterations)
        break;
    }
    if (count == 0)
      continue;
    const bool known =
        std::any_of(peaks.begin(), peaks.end(),
                    [&](const Peak &q) { return q.center == mean; });
    if (!known)
      peaks.push_back({mean, count});
  }
  if (peaks.empty())
    throw fail(QStringLiteral("Mean Shift found no centre"));
  std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) {
    if (a.count != b.count)
      return a.count > b.count;
    return a.center > b.center;
  });
  // A centre within the bandwidth of a denser one is the same region.
  std::vector<bool> unique(peaks.size(), true);
  MeanShiftResult out;
  out.bandwidth = bandwidth;
  for (size_t i = 0; i < peaks.size(); ++i) {
    if (!unique[i])
      continue;
    for (size_t j = i + 1; j < peaks.size(); ++j)
      if (dist2(peaks[i].center, peaks[j].center) <= bw2)
        unique[j] = false;
    out.centers.push_back(peaks[i].center);
  }
  out.labels.resize(x.size());
  for (size_t i = 0; i < x.size(); ++i) {
    int best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for (size_t c = 0; c < out.centers.size(); ++c) {
      const double d = dist2(x[i], out.centers[c]);
      if (d < bestD) {
        bestD = d;
        best = static_cast<int>(c);
      }
    }
    out.labels[i] = best;
  }
  return out;
}

Regions regionalise(const std::vector<Point2> &x, const RegionOptions &o) {
  if (x.empty())
    throw fail(QStringLiteral("the catalog has no event with coordinates"));
  Regions out;
  out.method = o.method;
  out.regionOf.resize(x.size());
  std::vector<Point2> centers;
  SomOptions so;
  so.sigma = o.sigma();
  so.iterations = o.iterations;
  so.seed = o.seed;
  if (o.method == RegionMethod::Som) {
    if (o.regions < 1)
      throw fail(QStringLiteral("the number of regions must be at least 1"));
    so.rows = 1;
    so.cols = o.regions;
    const Som som = trainSom(x, so);
    for (size_t i = 0; i < x.size(); ++i)
      out.regionOf[i] = som.winner(x[i]) + 1;
    centers = som.weights;
  } else {
    so.rows = o.gridRows;
    so.cols = o.gridCols;
    const Som som = trainSom(x, so);
    const double bw = estimateBandwidth(som.weights, o.quantile);
    const MeanShiftResult ms = meanShift(som.weights, bw);
    out.bandwidth = bw;
    for (size_t i = 0; i < x.size(); ++i)
      out.regionOf[i] = ms.labels[static_cast<size_t>(som.winner(x[i]))] + 1;
    centers = ms.centers;
  }
  for (size_t c = 0; c < centers.size(); ++c) {
    RegionSummary s;
    s.id = static_cast<int>(c) + 1;
    s.center = centers[c];
    s.latMin = s.lonMin = std::numeric_limits<double>::infinity();
    s.latMax = s.lonMax = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < x.size(); ++i)
      if (out.regionOf[i] == s.id) {
        ++s.count;
        s.latMin = std::min(s.latMin, x[i][0]);
        s.latMax = std::max(s.latMax, x[i][0]);
        s.lonMin = std::min(s.lonMin, x[i][1]);
        s.lonMax = std::max(s.lonMax, x[i][1]);
      }
    if (s.count > 0)
      out.regions.push_back(s);
  }
  return out;
}

QString methodPrefix(RegionMethod m) {
  return m == RegionMethod::Som ? QStringLiteral("som")
                                : QStringLiteral("sommeanshift");
}

QStringList saveRegionCsvs(const PointTable &table, const Regions &regions,
                           const QString &dir) {
  if (regions.regionOf.size() != table.points.size())
    throw fail(QStringLiteral("the regions do not belong to this catalog"));
  if (!QDir().mkpath(dir))
    throw fail(QStringLiteral("cannot create %1").arg(dir));
  QStringList paths;
  for (const RegionSummary &s : regions.regions) {
    const QString path =
        QDir(dir).filePath(QStringLiteral("%1_region_%2.csv")
                               .arg(methodPrefix(regions.method))
                               .arg(s.id));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
      throw fail(QStringLiteral("cannot write %1").arg(path));
    f.write(table.headerLine.toUtf8());
    f.write("\n");
    for (size_t i = 0; i < regions.regionOf.size(); ++i)
      if (regions.regionOf[i] == s.id) {
        f.write(table.rows[static_cast<qsizetype>(i)].toUtf8());
        f.write("\n");
      }
    if (!f.commit())
      throw fail(QStringLiteral("cannot write %1").arg(path));
    paths << path;
  }
  return paths;
}

} // namespace skg::analysis
