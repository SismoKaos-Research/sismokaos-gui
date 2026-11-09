#include "fx/dsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace skg::fx {

using C = std::complex<double>;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

namespace {
double response(const std::vector<std::array<double, 5>> &sections, double w) {
  const C z1(std::cos(w), -std::sin(w));
  const C z2 = z1 * z1;
  double r = 1.0;
  for (const auto &[b0, b1, b2, a1, a2] : sections) {
    const C num = C(b0) + z1 * b1 + z2 * b2;
    const C den = C(1.0) + z1 * a1 + z2 * a2;
    r *= std::abs(num / den);
  }
  return r;
}
} // namespace

BandPass::BandPass(int order, double fs, double lo, double hi) {
  if (order <= 0 || order % 2 != 0)
    throw std::invalid_argument("bandpass order must be even and positive");
  if (!(0.0 < lo && lo < hi && hi < fs / 2.0))
    throw std::invalid_argument("bandpass needs 0 < lo < hi < Nyquist");
  const double fs2 = 2.0 * fs;
  const double wl = fs2 * std::tan(std::numbers::pi * lo / fs);
  const double wh = fs2 * std::tan(std::numbers::pi * hi / fs);
  const double bw = wh - wl;
  const C wo2(wl * wh, 0.0);
  auto bilinear = [&](C s) { return (C(fs2) + s) / (C(fs2) - s); };
  for (int k = 0; k < order / 2; ++k) {
    const double theta = std::numbers::pi * (2 * k + 1 + order) / (2.0 * order);
    const C p = C(std::cos(theta), std::sin(theta)) * (bw / 2.0);
    const C root = std::sqrt(p * p - wo2);
    for (const C &s : {p + root, p - root}) {
      const C z = bilinear(s);
      sections_.push_back({1.0, 0.0, -1.0, -2.0 * z.real(), std::norm(z)});
    }
  }
  // Unit gain at the digital image of the analog centre frequency.
  const double w = 2.0 * std::atan2(std::sqrt(wl * wh), fs2);
  const double gain = 1.0 / response(sections_, w);
  for (int i = 0; i < 3; ++i)
    sections_[0][i] *= gain;
}

double BandPass::magnitude(double freq, double fs) const {
  return response(sections_, 2.0 * std::numbers::pi * freq / fs);
}

void BandPass::forward(std::vector<double> &x) const {
  double dc = x[0];
  for (const auto &[b0, b1, b2, a1, a2] : sections_) {
    const double g = (b0 + b1 + b2) / (1.0 + a1 + a2);
    double z1 = (b2 - a2 * g) * dc;
    double z0 = (b1 - a1 * g) * dc + z1;
    dc *= g;
    for (double &v : x) {
      const double u = v;
      const double y = b0 * u + z0;
      z0 = b1 * u - a1 * y + z1;
      z1 = b2 * u - a2 * y;
      v = y;
    }
  }
}

std::vector<double> BandPass::filtfilt(const std::vector<double> &x,
                                       int padlen) const {
  const size_t n = x.size();
  if (n == 0)
    return {};
  const size_t pad = std::min<size_t>(size_t(std::max(0, padlen)), n - 1);
  std::vector<double> ext;
  ext.reserve(n + 2 * pad);
  for (size_t i = pad; i >= 1; --i)
    ext.push_back(2.0 * x[0] - x[i]);
  ext.insert(ext.end(), x.begin(), x.end());
  for (size_t i = 1; i <= pad; ++i)
    ext.push_back(2.0 * x[n - 1] - x[n - 1 - i]);
  forward(ext);
  std::reverse(ext.begin(), ext.end());
  forward(ext);
  std::reverse(ext.begin(), ext.end());
  return std::vector<double>(ext.begin() + pad, ext.begin() + pad + n);
}

double settleSeconds(double freqmin) { return 5.0 / freqmin; }

// --- FFT
// ----------------------------------------------------------------------

namespace {
void fftPow2(std::vector<C> &a, bool inverse) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1)
      j ^= bit;
    j ^= bit;
    if (i < j)
      std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = 2 * std::numbers::pi / double(len) * (inverse ? 1 : -1);
    const C wlen(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      C w(1);
      for (size_t j = 0; j < len / 2; ++j) {
        const C u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
  if (inverse)
    for (C &v : a)
      v /= double(n);
}
} // namespace

void fft(std::vector<C> &a) {
  const size_t n = a.size();
  if (n <= 1)
    return;
  if ((n & (n - 1)) == 0) {
    fftPow2(a, false);
    return;
  }
  // Bluestein: X_k = conj(w_k) * sum_j (a_j conj(w_j)) w_{k-j}, w_j = exp(i pi
  // j^2 / n)
  size_t m = 1;
  while (m < 2 * n - 1)
    m <<= 1;
  std::vector<C> w(n), A(m), B(m);
  for (size_t j = 0; j < n; ++j) {
    const double ang = std::numbers::pi * double((j * j) % (2 * n)) / double(n);
    w[j] = C(std::cos(ang), std::sin(ang));
  }
  for (size_t j = 0; j < n; ++j)
    A[j] = a[j] * std::conj(w[j]);
  B[0] = w[0];
  for (size_t j = 1; j < n; ++j)
    B[j] = B[m - j] = w[j];
  fftPow2(A, false);
  fftPow2(B, false);
  for (size_t j = 0; j < m; ++j)
    A[j] *= B[j];
  fftPow2(A, true);
  for (size_t k = 0; k < n; ++k)
    a[k] = A[k] * std::conj(w[k]);
}

std::vector<double> powerSpectrum(const std::vector<double> &x) {
  const size_t n = x.size();
  std::vector<C> buf(n);
  for (size_t i = 0; i < n; ++i)
    buf[i] = C(x[i], 0.0);
  fft(buf);
  std::vector<double> p(n / 2 + 1);
  for (size_t k = 0; k < p.size(); ++k)
    p[k] = std::norm(buf[k]) / double(n);
  return p;
}

// --- gaps and trends
// -----------------------------------------------------------

std::vector<bool> interpolateGaps(std::vector<double> &data) {
  const size_t n = data.size();
  std::vector<bool> filled(n, false);
  size_t i = 0;
  while (i < n) {
    if (!std::isnan(data[i])) {
      ++i;
      continue;
    }
    const size_t start = i;
    while (i < n && std::isnan(data[i]))
      ++i;
    const size_t end = i;
    if (start == 0 || end == n)
      continue; // edge gap: no bracketing sample, left as NaN
    const size_t left = start - 1, right = end;
    const double y0 = data[left], y1 = data[right];
    const double span = double(right - left);
    // Catmull-Rom tangents as a slope per sample scaled into t in [0, 1].
    const double m0 =
        left > 0 ? ((y1 - data[left - 1]) / (span + 1.0)) * span : y1 - y0;
    const double m1 = right + 1 < n
                          ? ((data[right + 1] - y0) / (span + 1.0)) * span
                          : y1 - y0;
    for (size_t k = start; k < end; ++k) {
      const double t = double(k - left) / span, t2 = t * t, t3 = t2 * t;
      data[k] = (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * m0 +
                (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * m1;
      filled[k] = true;
    }
  }
  return filled;
}

void detrendLinear(std::vector<double> &data) {
  const size_t n = data.size();
  if (n < 2)
    return;
  double sumY = 0, sumX = 0, count = 0;
  for (size_t i = 0; i < n; ++i)
    if (!std::isnan(data[i])) {
      sumY += data[i];
      sumX += double(i);
      count += 1;
    }
  if (count < 2) {
    const double mean = sumY / std::max(count, 1.0);
    for (double &v : data)
      if (!std::isnan(v))
        v -= mean;
    return;
  }
  const double meanY = sumY / count, meanX = sumX / count;
  double sxy = 0, sxx = 0;
  for (size_t i = 0; i < n; ++i)
    if (!std::isnan(data[i])) {
      const double dx = double(i) - meanX;
      sxy += dx * (data[i] - meanY);
      sxx += dx * dx;
    }
  const double slope =
      std::abs(sxx) < std::numeric_limits<double>::epsilon() ? 0.0 : sxy / sxx;
  const double intercept = meanY - slope * meanX;
  for (size_t i = 0; i < n; ++i)
    if (!std::isnan(data[i]))
      data[i] -= intercept + slope * double(i);
}

StaLta staLta(const std::vector<double> &x, int nsta, int nlta) {
  const int n = int(x.size());
  if (n <= nlta || nsta <= 0 || nlta <= 0 || nsta > nlta)
    return {kNaN, kNaN, kNaN};
  // cft[i] for i >= nlta-1 with cumulative sums, as obspy's classic_sta_lta;
  // the features read cft[nlta..].
  std::vector<double> cum(n + 1, 0.0);
  for (int i = 0; i < n; ++i)
    cum[i + 1] = cum[i] + x[i] * x[i];
  std::vector<double> cft;
  cft.reserve(n - nlta);
  const double tiny = std::numeric_limits<double>::min();
  for (int i = nlta; i < n; ++i) {
    const double sta = (cum[i + 1] - cum[i + 1 - nsta]) / nsta;
    double lta = (cum[i + 1] - cum[i + 1 - nlta]) / nlta;
    if (lta < tiny)
      lta = tiny;
    cft.push_back(sta / lta);
  }
  if (cft.empty())
    return {kNaN, kNaN, kNaN};
  StaLta out{};
  out.max = *std::max_element(cft.begin(), cft.end());
  double sum = 0;
  for (double v : cft)
    sum += v;
  out.mean = sum / double(cft.size());
  std::vector<double> sorted = cft;
  std::sort(sorted.begin(), sorted.end());
  const size_t mid = sorted.size() / 2;
  out.median =
      sorted.size() % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0;
  return out;
}

} // namespace skg::fx
