#include "fx/features.h"

#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "fx/dsp.h"
#include <utility>

namespace skg::fx {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// --- statistics
// ----------------------------------------------------------------

Moments moments(const std::vector<double> &x) {
  const double n = double(x.size());
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= n;
  double m2 = 0, m3 = 0, m4 = 0;
  for (double v : x) {
    const double d = v - mean, d2 = d * d;
    m2 += d2;
    m3 += d2 * d;
    m4 += d2 * d2;
  }
  return {mean, m2 / n, m3 / n, m4 / n};
}

double peak(const std::vector<double> &x) {
  double p = kNaN;
  for (double v : x)
    if (std::isnan(p) || std::abs(v) > p)
      p = std::abs(v);
  return p;
}

double rms(const std::vector<double> &x) {
  if (x.empty())
    return kNaN;
  double s = 0;
  for (double v : x)
    s += v * v;
  return std::sqrt(s / double(x.size()));
}

double zeroCrossingRate(const std::vector<double> &x) {
  if (x.size() < 2)
    return kNaN;
  size_t crossings = 0;
  for (size_t i = 1; i < x.size(); ++i)
    crossings += (x[i - 1] >= 0.0) != (x[i] >= 0.0);
  return double(crossings) / double(x.size() - 1);
}

double dominantFrequency(const std::vector<double> &power,
                         const std::vector<double> &freqs) {
  if (power.size() <= 1)
    return kNaN;
  size_t best = 1;
  for (size_t i = 2; i < power.size(); ++i)
    if (power[i] > power[best])
      best = i;
  return best < freqs.size() ? freqs[best] : kNaN;
}

std::array<double, 3>
spectralCentroidAndEnergy(const std::vector<double> &freqs,
                          const std::vector<double> &power, double splitHz) {
  double total = 0;
  for (double p : power)
    total += p;
  if (!(total > 0))
    return {0.0, 0.0, 0.0};
  double centroid = 0, lf = 0, hf = 0;
  for (size_t i = 0; i < freqs.size() && i < power.size(); ++i) {
    centroid += freqs[i] * power[i];
    (freqs[i] <= splitHz ? lf : hf) += power[i];
  }
  return {centroid / total, lf / total, hf / total};
}

std::array<double, 3> hjorth(const std::vector<double> &x) {
  const size_t n = x.size();
  if (n < 3)
    return {kNaN, kNaN, kNaN};
  auto popVar = [](const std::vector<double> &v) {
    double mean = 0;
    for (double a : v)
      mean += a;
    mean /= double(v.size());
    double s = 0;
    for (double a : v)
      s += (a - mean) * (a - mean);
    return s / double(v.size());
  };
  std::vector<double> d1(n - 1), d2(n - 2);
  for (size_t i = 0; i + 1 < n; ++i)
    d1[i] = x[i + 1] - x[i];
  for (size_t i = 0; i + 1 < d1.size(); ++i)
    d2[i] = d1[i + 1] - d1[i];
  const double vx = popVar(x), vd1 = popVar(d1), vd2 = popVar(d2);
  const double activity = vx;
  const double mobility = vx > 0 ? std::sqrt(vd1 / vx) : 0.0;
  const double complexity =
      (vd1 > 0 && mobility > 0) ? std::sqrt(vd2 / vd1) / mobility : 0.0;
  return {activity, mobility, complexity};
}

double permutationEntropy(const std::vector<double> &x) {
  if (x.size() < 3)
    return kNaN;
  std::array<size_t, 6> counts{};
  size_t total = 0;
  for (size_t i = 0; i + 2 < x.size(); ++i) {
    const double a = x[i], b = x[i + 1], c = x[i + 2];
    int idx;
    if (a <= b)
      idx = b <= c ? 0 : a <= c ? 1 : 4;
    else
      idx = a <= c ? 2 : b <= c ? 3 : 5;
    ++counts[idx];
    ++total;
  }
  double h = 0;
  for (size_t cnt : counts)
    if (cnt) {
      const double p = double(cnt) / double(total);
      h -= p * std::log(p);
    }
  return h / std::log(6.0);
}

double crossCorrelation(const std::vector<double> &a,
                        const std::vector<double> &b) {
  if (a.size() != b.size() || a.empty())
    return kNaN;
  const double n = double(a.size());
  double s1 = 0, s2 = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    s1 += a[i];
    s2 += b[i];
  }
  const double m1 = s1 / n, m2 = s2 / n;
  double cov = 0, v1 = 0, v2 = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    const double dx = a[i] - m1, dy = b[i] - m2;
    cov += dx * dy;
    v1 += dx * dx;
    v2 += dy * dy;
  }
  if (v1 == 0 || v2 == 0)
    return kNaN;
  return cov / std::sqrt(v1 * v2);
}

// --- chaos
// -------------------------------------------------------------------------

namespace {

struct Embedding {
  std::vector<double> y; // row-major: point i is y[i*dim .. i*dim+dim)
  int m = 0, dim = 0;
  const double *point(int i) const { return y.data() + size_t(i) * dim; }
  double dist(int a, int b) const {
    const double *pa = point(a), *pb = point(b);
    double s = 0;
    for (int t = 0; t < dim; ++t) {
      const double d = pa[t] - pb[t];
      s += d * d;
    }
    return std::sqrt(s);
  }
};

Embedding embed(const std::vector<double> &x, int tau, int dim) {
  Embedding e;
  e.dim = dim;
  e.m = int(x.size()) - (dim - 1) * tau;
  if (e.m <= 0) {
    e.m = 0;
    return e;
  }
  e.y.resize(size_t(e.m) * dim);
  for (int i = 0; i < dim; ++i)
    for (int j = 0; j < e.m; ++j)
      e.y[size_t(j) * dim + i] = x[size_t(i) * tau + j];
  return e;
}

double regressionSlope(const double *xs, const double *ys, int n) {
  if (n < 2)
    return kNaN;
  double mx = 0, my = 0;
  for (int i = 0; i < n; ++i) {
    mx += xs[i];
    my += ys[i];
  }
  mx /= n;
  my /= n;
  double num = 0, den = 0;
  for (int i = 0; i < n; ++i) {
    num += (xs[i] - mx) * (ys[i] - my);
    den += (xs[i] - mx) * (xs[i] - mx);
  }
  return den == 0 ? kNaN : num / den;
}

// Port of sismokaos-cli's wolf_next_point: the nearest neighbour within 30
// degrees of the current displacement (if also within scale_mx), else the
// nearest neighbour regardless of angle.
int wolfNextPoint(bool flag1, const Embedding &e, int currentPoint,
                  int currentPair, int npt, int yLen, int evolve,
                  double scaleMx) {
  const int ep = currentPoint + evolve;
  if (ep >= yLen)
    return currentPair;
  const bool safe = currentPair + evolve < yLen;
  const int endIdx = safe ? currentPair + evolve : currentPair + evolve - 1;
  if (endIdx >= yLen)
    return currentPair;
  const double *pe = e.point(ep), *pair = e.point(endIdx);
  double endSq = 0;
  for (int t = 0; t < e.dim; ++t)
    endSq += (pe[t] - pair[t]) * (pe[t] - pair[t]);
  const double endDist = std::sqrt(endSq);
  const int bound = std::min(npt, yLen);
  const double angleLimit = 30.0 * std::numbers::pi / 180.0;
  bool haveAngle = false;
  int angleIdx = -1;
  double angleD = kInf;
  int bestIdx = -1;
  double bestD = kInf;
  for (int k = 0; k < bound; ++k) {
    if (k + 10 >= ep && k <= ep + 10)
      continue;
    const double *pk = e.point(k);
    double sq = 0;
    for (int t = 0; t < e.dim; ++t)
      sq += (pe[t] - pk[t]) * (pe[t] - pk[t]);
    const double yd = std::sqrt(sq);
    if (yd <= 0.0)
      continue;
    if (yd < bestD) {
      bestD = yd;
      bestIdx = k;
    }
    if (flag1 || (haveAngle && yd >= angleD))
      continue;
    const double denom = yd * endDist;
    if (denom <= 0.0)
      continue;
    double dot = 0;
    for (int t = 0; t < e.dim; ++t)
      dot += (pe[t] - pair[t]) * (pe[t] - pk[t]);
    const double theta =
        std::acos(std::clamp(std::abs(dot / denom), -1.0, 1.0));
    if (theta < angleLimit) {
      haveAngle = true;
      angleIdx = k;
      angleD = yd;
    }
  }
  if (haveAngle && angleD <= scaleMx)
    return angleIdx;
  return bestIdx < 0 ? currentPair : bestIdx;
}

} // namespace

double wolfLye(const std::vector<double> &x, double fs, int tau, int dim,
               int evolve) {
  const int n = int(x.size());
  if (dim <= 0 || tau <= 0 || evolve <= 0 || (dim - 1) * tau + evolve >= n)
    return kNaN;
  double minV = kInf, maxV = -kInf;
  for (double v : x) {
    minV = std::min(minV, v);
    maxV = std::max(maxV, v);
  }
  const double scaleMx = (maxV - minV) / 10.0;
  const double dt = 1.0 / fs;
  const Embedding e = embed(x, tau, dim);
  const int npt = n - (dim - 1) * tau - evolve;
  if (npt <= 0)
    return kNaN;
  const int yLen = std::min(npt + evolve, e.m);

  int currentPair = -1;
  double bestDist = kInf;
  for (int i = 11; i < std::min(npt, yLen); ++i) {
    const double d = e.dist(0, i);
    if (d > 0.0 && d < bestDist) {
      bestDist = d;
      currentPair = i;
    }
  }
  if (currentPair < 0)
    return kNaN;

  quint64 its = 0;
  double distSum = 0, lye = 0;
  for (int i = 0; i < npt; i += evolve) {
    const int ep = i + evolve;
    if (ep >= yLen)
      break;
    const bool safe = currentPair + evolve < yLen;
    const int pairEp = safe ? currentPair + evolve : currentPair + evolve - 1;
    if (pairEp >= yLen)
      break;
    const double startDist = e.dist(i, currentPair);
    const double endDist = e.dist(ep, pairEp);
    if (startDist > 0.0 && endDist > 0.0) {
      distSum += std::log2(endDist / startDist) / (evolve * dt);
      ++its;
      lye = distSum / double(its);
    }
    if (endDist < scaleMx) {
      currentPair += evolve;
      if (currentPair > npt) {
        currentPair -= evolve;
        currentPair =
            wolfNextPoint(true, e, i, currentPair, npt, yLen, evolve, scaleMx);
      }
    } else {
      currentPair =
          wolfNextPoint(false, e, i, currentPair, npt, yLen, evolve, scaleMx);
    }
  }
  return lye;
}

std::vector<double> divergenceCurve(const std::vector<double> &x, int tau,
                                    int dim, int band, int jmax) {
  const int n = int(x.size());
  if (dim <= 0 || (dim - 1) * tau >= n)
    return {};
  const Embedding e = embed(x, tau, dim);
  const int m = e.m;
  if (m < 2)
    return {};
  // Nearest neighbour outside the temporal band, each unordered pair once;
  // strict `<` keeps the first minimum on ties, as the reference does.
  std::vector<int> bestIdx(m, -1);
  std::vector<double> bestD(m, kInf);
  for (int i = 0; i < m; ++i) {
    const double *pi = e.point(i);
    for (int k = i + band + 1; k < m; ++k) {
      const double *pk = e.point(k);
      double d = 0;
      for (int t = 0; t < dim; ++t) {
        const double v = pi[t] - pk[t];
        d += v * v;
      }
      if (d < bestD[i]) {
        bestD[i] = d;
        bestIdx[i] = k;
      }
      if (d < bestD[k]) {
        bestD[k] = d;
        bestIdx[k] = i;
      }
    }
  }
  std::vector<double> curve;
  const int last = std::min(jmax, m - 1);
  curve.reserve(last + 1);
  for (int j = 0; j <= last; ++j) {
    double sum = 0;
    int count = 0;
    for (int i = 0; i < m; ++i) {
      const int nn = bestIdx[i] < 0 ? i : bestIdx[i];
      if (i + j >= m || nn + j >= m)
        continue;
      const double d = e.dist(i + j, nn + j);
      if (d > 0.0) {
        sum += std::log(d);
        ++count;
      }
    }
    if (count == 0)
      break;
    curve.push_back(sum / count);
  }
  return curve;
}

std::pair<double, double> riseFit(const std::vector<double> &curve, double fs,
                                  double meanPeriod,
                                  std::array<double, 2> rise) {
  if (curve.size() < 2)
    return {kNaN, kNaN};
  const double start = curve[0];
  const double peakV = *std::max_element(curve.begin(), curve.end());
  const double climb = peakV - start;
  if (!(climb > 0.0))
    return {kNaN, kNaN};
  auto reach = [&](double frac) -> int {
    for (size_t j = 0; j < curve.size(); ++j)
      if (curve[j] >= start + frac * climb)
        return int(j);
    return -1;
  };
  const int lo = reach(rise[0]), hi = reach(rise[1]);
  if (lo < 0 || hi < 0)
    return {kNaN, kNaN};
  std::vector<double> time(curve.size());
  for (size_t i = 0; i < curve.size(); ++i)
    time[i] = double(i) / fs / meanPeriod;
  auto fit = [&](int a, int b) {
    return b > a ? regressionSlope(time.data() + a, curve.data() + a, b - a + 1)
                 : kNaN;
  };
  return {fit(lo, hi), fit(hi, int(curve.size()) - 1)};
}

double pythonRound(double v) {
  const double fl = std::floor(v);
  const double diff = v - fl;
  if (diff < 0.5)
    return fl;
  if (diff > 0.5)
    return fl + 1.0;
  return (qint64(fl) % 2 == 0) ? fl : fl + 1.0;
}

std::pair<double, double> rosensteinLye(const std::vector<double> &x, double fs,
                                        int tau, int dim, double meanPeriod,
                                        double horizon,
                                        std::array<double, 2> rise) {
  if (dim <= 0 || !(meanPeriod > 0.0))
    return {kNaN, kNaN};
  const int band = std::max((dim - 1) * tau, int(std::ceil(meanPeriod * fs)));
  const int jmax = int(pythonRound(horizon * meanPeriod * fs));
  return riseFit(divergenceCurve(x, tau, dim, band, jmax), fs, meanPeriod,
                 rise);
}

std::pair<double, double> rosensteinFixedWindow(const std::vector<double> &x,
                                                double fs, int tau, int dim,
                                                std::array<double, 4> slope,
                                                double meanPeriod) {
  const int n = int(x.size());
  if (dim <= 0 || (dim - 1) * tau >= n)
    return {kNaN, kNaN};
  const int m = n - (dim - 1) * tau;
  if (m < 2)
    return {kNaN, kNaN};
  const std::vector<double> curve =
      divergenceCurve(x, tau, dim, (dim - 1) * tau, m - 1);
  std::vector<double> ave(m, 0.0);
  std::copy(curve.begin(), curve.end(), ave.begin());
  int nz = 0;
  for (double v : ave)
    nz += v != 0.0;
  std::vector<double> time(m);
  for (int i = 0; i < m; ++i)
    time[i] = double(i) / fs / meanPeriod;
  auto fit = [&](int lo, int hi) {
    if (hi >= m || hi > nz || lo > hi)
      return kNaN;
    return regressionSlope(time.data() + lo, ave.data() + lo, hi - lo + 1);
  };
  auto idx = [&](double v) {
    return int(std::max(0.0, pythonRound(v * meanPeriod * fs)));
  };
  const int sLo = slope[0] == 0.0 ? 0 : idx(slope[0]);
  return {fit(sLo, idx(slope[1])), fit(idx(slope[2]), idx(slope[3]))};
}

double sampleEntropy(const std::vector<double> &x, int m, double r) {
  const size_t n = x.size();
  if (n <= size_t(m) + 1)
    return kNaN;
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= double(n);
  double s = 0;
  for (double v : x)
    s += (v - mean) * (v - mean);
  return sampleEntropyTol(x, m, r * std::sqrt(s / (double(n) - 1.0)));
}

double sampleEntropyTol(const std::vector<double> &x, int m, double tol) {
  const int n = int(x.size());
  if (n <= m + 1 || !(tol > 0.0))
    return kNaN;
  const int l = n - m;
  quint64 b = 0, a = 0;
  for (int i = 0; i < l; ++i)
    for (int j = i + 1; j < l; ++j) {
      double dm = 0;
      for (int k = 0; k < m; ++k)
        dm = std::max(dm, std::abs(x[i + k] - x[j + k]));
      if (dm <= tol) {
        b += 2;
        if (std::max(dm, std::abs(x[i + m] - x[j + m])) <= tol)
          a += 2;
      }
    }
  if (a == 0 || b == 0)
    return kNaN;
  return -std::log(double(a) / double(b));
}

double correlationDimension(const std::vector<double> &x, int tau, int de) {
  const int n = int(x.size());
  if (de <= 0 || (de - 1) * tau >= n)
    return 0.0;
  const int count = n - (de - 1) * tau;
  if (count <= 0)
    return 0.0;
  const Embedding e = embed(x, tau, de);
  const int bins = 200;
  const int k = de * tau;
  if (k + 1 >= count)
    return 0.0;
  const int pairCount = count - k - 1;
  std::vector<double> dists;
  size_t total = 0;
  for (int i = 0; i < pairCount; ++i)
    total += size_t(count - (i + k + 1));
  dists.reserve(total);
  double maxSq = 0, minSq = kInf;
  for (int i = 0; i < pairCount; ++i) {
    const double *pi = e.point(i);
    for (int j = i + k + 1; j < count; ++j) {
      const double *pj = e.point(j);
      double acc = 0;
      for (int t = 0; t < de; ++t) {
        const double d = pi[t] - pj[t];
        acc += d * d;
      }
      dists.push_back(acc);
      maxSq = std::max(maxSq, acc);
      minSq = std::min(minSq, acc);
    }
  }
  const double eps1 = std::sqrt(maxSq);
  double eps2 = std::sqrt(minSq);
  if (eps2 == 0.0)
    eps2 = std::numeric_limits<double>::epsilon();
  if (eps1 <= eps2)
    return 0.0;
  const double logE2 = std::log(eps2), logE1 = std::log(eps1);
  std::vector<double> epsilon(bins);
  for (int b = 0; b < bins; ++b)
    epsilon[b] =
        std::exp(logE2 + (logE1 - logE2) * double(b) / double(bins - 1));
  // Bin each distance at the first epsilon >= sqrt(sq), i.e. the first bin
  // whose edge squared is >= sq: sqrt is monotonic and correctly rounded.
  std::vector<double> ci(bins, 0.0);
  for (double sq : dists) {
    const double d = std::sqrt(sq);
    const auto it = std::lower_bound(epsilon.begin(), epsilon.end(), d);
    if (it != epsilon.end())
      ci[size_t(it - epsilon.begin())] += 1.0;
  }
  const double denom = double(count - k) * double(count - k);
  double cum = 0;
  std::vector<double> curve(bins, kNaN);
  for (int b = 0; b < bins; ++b) {
    cum += ci[b];
    if (cum > 0.0)
      curve[b] = std::log(cum / denom) + std::log(double(count - k - 1));
  }
  std::vector<double> xs, ys;
  double lo = kInf, hi = -kInf;
  for (int b = 0; b < bins; ++b)
    if (std::isfinite(curve[b])) {
      lo = std::min(lo, curve[b]);
      hi = std::max(hi, curve[b]);
    }
  if (!std::isfinite(lo))
    return 0.0;
  const double mid = (lo + hi) / 2.0, q = (hi - lo) / 4.0;
  for (int b = 0; b < bins; ++b)
    if (std::isfinite(curve[b]) && curve[b] > mid && curve[b] < mid + q) {
      xs.push_back(std::log(epsilon[b]));
      ys.push_back(curve[b]);
    }
  if (xs.size() < 2)
    return 0.0;
  return regressionSlope(xs.data(), ys.data(), int(xs.size()));
}

std::optional<int> amiTau(const std::vector<double> &x, int maxLag) {
  const int n = int(x.size());
  if (maxLag < 3 || n <= maxLag + 1)
    return std::nullopt;
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= n;
  double s = 0;
  double lo = kInf, hiV = -kInf;
  for (double v : x) {
    s += (v - mean) * (v - mean);
    lo = std::min(lo, v);
    hiV = std::max(hiV, v);
  }
  const double sd = std::sqrt(s / n), range = hiV - lo;
  if (!(sd > 0.0 && range > 0.0))
    return std::nullopt;
  const int bins =
      int(std::ceil(range / (3.49 * sd * std::pow(double(n), -1.0 / 3.0))));
  if (bins < 2)
    return std::nullopt;
  const double width =
      range / (double(bins) - std::numeric_limits<double>::epsilon());
  std::vector<int> y(n);
  for (int i = 0; i < n; ++i)
    y[i] = std::min(bins - 1, int((x[i] - lo) / width));
  const int overlap = n - maxLag;
  const double p = 1.0 / overlap;
  std::vector<double> pa(bins, 0.0), pb(bins), pab(size_t(bins) * bins);
  for (int i = 0; i < overlap; ++i)
    pa[y[i]] += p;
  std::vector<double> ami;
  ami.reserve(maxLag);
  for (int lag = 0; lag < maxLag; ++lag) {
    std::fill(pb.begin(), pb.end(), 0.0);
    std::fill(pab.begin(), pab.end(), 0.0);
    for (int i = 0; i < overlap; ++i) {
      pb[y[i + lag]] += p;
      pab[size_t(y[i]) * bins + y[i + lag]] += p;
    }
    double v = 0;
    for (int a = 0; a < bins; ++a)
      for (int b = 0; b < bins; ++b) {
        const double joint = pab[size_t(a) * bins + b];
        if (joint > 0.0)
          v += joint * std::log2(joint / (pa[a] * pb[b]));
      }
    ami.push_back(v);
  }
  for (int i = 1; i + 1 < maxLag; ++i)
    if (ami[i - 1] >= ami[i] && ami[i] <= ami[i + 1])
      return i;
  return std::nullopt;
}

std::optional<int> fnnDim(const std::vector<double> &x, int tau, int maxDim) {
  constexpr double rtol = 15.0, atol = 2.0;
  if (maxDim <= 0 || tau * maxDim + 2 > int(x.size()))
    return std::nullopt;
  const int n = int(x.size()) - tau * maxDim;
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= double(x.size());
  double s = 0;
  for (double v : x)
    s += (v - mean) * (v - mean);
  const double ra = std::sqrt(s / double(x.size()));
  if (!(ra > 0.0))
    return std::nullopt;
  std::vector<double> frac;
  for (int j = 0; j < maxDim; ++j) {
    const int d = j + 1;
    int falseCount = 0;
    for (int i = 0; i < n; ++i) {
      double best = kInf;
      int nn = i;
      for (int k = 0; k < n; ++k) {
        if (k == i)
          continue;
        double acc = 0;
        for (int t = 0; t < d; ++t) {
          const double e = x[i + t * tau] - x[k + t * tau];
          acc += e * e;
          if (acc >= best)
            break;
        }
        if (acc < best) {
          best = acc;
          nn = k;
        }
      }
      const double dist = std::sqrt(best);
      const double dz = x[i + d * tau] - x[nn + d * tau];
      if (std::abs(dz) > dist * rtol || std::sqrt(best + dz * dz) / ra > atol)
        ++falseCount;
    }
    frac.push_back(double(falseCount) / n);
    if (j >= 2 && frac[j - 2] > frac[j - 1] && frac[j - 1] < frac[j])
      return j;
    if (j >= 1 && std::abs(frac[j] - frac[j - 1]) <= 0.001)
      return j;
    if (frac[j] == 0.0)
      return j + 1;
  }
  return maxDim;
}

// --- a window
// --------------------------------------------------------------------

namespace {
const char *const kStatKeys[] = {"STA_LTA_Max",
                                 "STA_LTA_Mean",
                                 "STA_LTA_Median",
                                 "PEAK",
                                 "RMS",
                                 "SKEWNESS",
                                 "KURTOSIS",
                                 "ZCR",
                                 "DOMINANT_FREQ",
                                 "SPECTRAL_CENTROID",
                                 "LOW_FREQ_ENERGY",
                                 "HIGH_FREQ_ENERGY",
                                 "HJORTH_ACTIVITY",
                                 "HJORTH_MOBILITY",
                                 "HJORTH_COMPLEXITY",
                                 "PERMUTATION_ENTROPY"};
const char *const kChaosKeys[] = {"WOLF_LYE", "ROS_SHORT", "ROS_LONG",
                                  "SAMP_ENT", "CORR_DIM"};
const char kComponents[] = {'E', 'N', 'Z'};
} // namespace

QStringList featureNames(bool chaosOnly) {
  QStringList names;
  if (!chaosOnly)
    names << QStringLiteral("EN_CROSS_CORR") << QStringLiteral("EZ_CROSS_CORR")
          << QStringLiteral("NZ_CROSS_CORR");
  for (char c : kComponents) {
    if (!chaosOnly)
      for (const char *k : kStatKeys)
        names << QStringLiteral("%1_%2").arg(c).arg(QLatin1String(k));
    for (const char *k : kChaosKeys)
      names << QStringLiteral("%1_%2").arg(c).arg(QLatin1String(k));
  }
  names.sort();
  return names;
}

FeatureRow computeWindow(const std::vector<double> &e,
                         const std::vector<double> &n,
                         const std::vector<double> &z, const Config &cfg) {
  QMap<QString, double> result;
  if (!cfg.chaosOnly) {
    result.insert(QStringLiteral("EN_CROSS_CORR"), crossCorrelation(e, n));
    result.insert(QStringLiteral("EZ_CROSS_CORR"), crossCorrelation(e, z));
    result.insert(QStringLiteral("NZ_CROSS_CORR"), crossCorrelation(n, z));
  }
  const int winSize = cfg.winSize();
  std::vector<double> freqs(size_t(winSize) / 2 + 1);
  for (size_t i = 0; i < freqs.size(); ++i)
    freqs[i] = double(i) * cfg.fs / double(winSize);
  const std::vector<double> *segs[3] = {&e, &n, &z};
  for (int ci = 0; ci < 3; ++ci) {
    const char comp = kComponents[ci];
    const std::vector<double> &seg = *segs[ci];
    auto key = [&](const char *k) {
      return QStringLiteral("%1_%2").arg(comp).arg(QLatin1String(k));
    };
    const bool gappy = std::any_of(seg.begin(), seg.end(),
                                   [](double v) { return std::isnan(v); });
    if (gappy) {
      if (!cfg.chaosOnly)
        for (const char *k : kStatKeys)
          result.insert(key(k), kNaN);
      for (const char *k : kChaosKeys)
        result.insert(key(k), kNaN);
      continue;
    }
    const Moments mo = moments(seg);
    const double sd = std::sqrt(mo.var);
    std::vector<double> norm(seg.size());
    for (size_t i = 0; i < seg.size(); ++i)
      norm[i] = sd > 0 ? (seg[i] - mo.mean) / sd : seg[i];
    if (!cfg.chaosOnly) {
      const double skew = mo.var > 0 ? mo.m3 / std::pow(mo.var, 1.5) : kNaN;
      const double kurt = mo.var > 0 ? mo.m4 / (mo.var * mo.var) : kNaN;
      const std::vector<double> power = powerSpectrum(norm);
      const StaLta sl = staLta(norm, cfg.nsta(), cfg.nlta());
      const auto [centroid, lf, hf] =
          spectralCentroidAndEnergy(freqs, power, cfg.spectralSplitHz);
      const auto [act, mob, cx] = hjorth(seg);
      result.insert(key("STA_LTA_Max"), sl.max);
      result.insert(key("STA_LTA_Mean"), sl.mean);
      result.insert(key("STA_LTA_Median"), sl.median);
      result.insert(key("PEAK"), peak(seg));
      result.insert(key("RMS"), rms(seg));
      result.insert(key("SKEWNESS"), skew);
      result.insert(key("KURTOSIS"), kurt);
      result.insert(key("ZCR"), zeroCrossingRate(seg));
      result.insert(key("DOMINANT_FREQ"), dominantFrequency(power, freqs));
      result.insert(key("SPECTRAL_CENTROID"), centroid);
      result.insert(key("LOW_FREQ_ENERGY"), lf);
      result.insert(key("HIGH_FREQ_ENERGY"), hf);
      result.insert(key("HJORTH_ACTIVITY"), act);
      result.insert(key("HJORTH_MOBILITY"), mob);
      result.insert(key("HJORTH_COMPLEXITY"), cx);
      result.insert(key("PERMUTATION_ENTROPY"), permutationEntropy(seg));
    }
    const bool longEnough = int(norm.size()) >= cfg.chaosMinSamples;
    result.insert(key("WOLF_LYE"), longEnough
                                       ? wolfLye(norm, cfg.fs, cfg.chaosTau,
                                                 cfg.chaosDim, cfg.chaosEvolve)
                                       : kNaN);
    const auto ros = longEnough
                         ? rosensteinLye(norm, cfg.fs, cfg.chaosTau,
                                         cfg.chaosDim, cfg.chaosMeanPeriod,
                                         cfg.chaosRosHorizon, cfg.chaosRosRise)
                         : std::pair<double, double>{kNaN, kNaN};
    result.insert(key("ROS_SHORT"), ros.first);
    result.insert(key("ROS_LONG"), ros.second);
    result.insert(
        key("SAMP_ENT"),
        cfg.chaosSampentTol
            ? sampleEntropyTol(seg, cfg.chaosSampentM,
                               (*cfg.chaosSampentTol)[ci])
            : sampleEntropy(norm, cfg.chaosSampentM, cfg.chaosSampentR));
    result.insert(key("CORR_DIM"),
                  correlationDimension(norm, cfg.chaosTau, cfg.chaosDim));
  }
  FeatureRow row;
  for (const QString &name : featureNames(cfg.chaosOnly))
    row.append({name, result.value(name, kNaN)});
  return row;
}

} // namespace skg::fx
