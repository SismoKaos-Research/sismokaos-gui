#include "cbi/indicators.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "cbi/windows.h"

namespace skg::cbi {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kSecondsPerDay = 86400.0;
constexpr double kSecondsPerYear = 365.25 * kSecondsPerDay;
} // namespace

const std::vector<IndicatorInfo> &indicators() {
  static const std::vector<IndicatorInfo> list = {
      {QStringLiteral("T"), QStringLiteral("Geçen Süre (Gün)"),
       QStringLiteral("Elapsed time (days)"), QColor(0x80, 0x00, 0x80)},
      {QStringLiteral("Mmean_rectangular"),
       QStringLiteral("Ortalama Büyüklük - (M_mean)"),
       QStringLiteral("Mean magnitude (M_mean)"), QColor(0x00, 0x00, 0xff)},
      {QStringLiteral("Mmean_half_gauss"),
       QStringLiteral("Ortalama Büyüklük - (M_mean)"),
       QStringLiteral("Mean magnitude (M_mean)"), QColor(0x00, 0x00, 0xff)},
      {QStringLiteral("b_value"), QStringLiteral("b-değeri"),
       QStringLiteral("b-value"), QColor(0x00, 0x80, 0x00)},
      {QStringLiteral("a_value"), QStringLiteral("a-değeri (Yıllık)"),
       QStringLiteral("a-value (annual)"), QColor(0x00, 0x00, 0x00)},
      {QStringLiteral("Delta_M"), QStringLiteral("Büyüklük Açığı (ΔM)"),
       QStringLiteral("Magnitude deficit (ΔM)"), QColor(0xff, 0xa5, 0x00)},
      {QStringLiteral("dE_sqrt_rectangular"), QStringLiteral("E^(1/2)"),
       QStringLiteral("E^(1/2)"), QColor(0xa5, 0x2a, 0x2a)},
      {QStringLiteral("dE_sqrt_half_gauss"), QStringLiteral("E^(1/2) "),
       QStringLiteral("E^(1/2) "), QColor(0xa5, 0x2a, 0x2a)},
      {QStringLiteral("eta"), QStringLiteral("η-değeri (GR Sapması)"),
       QStringLiteral("η (deviation from GR)"), QColor(0xff, 0x00, 0xff)},
      {QStringLiteral("mu"),
       QStringLiteral("μ-değeri (Karakteristik Aralık - Gün)"),
       QStringLiteral("μ (characteristic interval, days)"),
       QColor(0x8b, 0x00, 0x00)},
  };
  return list;
}

QStringList indicatorColumns() {
  QStringList out;
  for (const IndicatorInfo &i : indicators())
    out << i.column;
  return out;
}

const IndicatorInfo *indicatorInfo(const QString &column) {
  for (const IndicatorInfo &i : indicators())
    if (i.column == column)
      return &i;
  return nullptr;
}

QStringList windowIndicatorColumns() {
  return {QStringLiteral("T"),
          QStringLiteral("b_value"),
          QStringLiteral("a_value"),
          QStringLiteral("Delta_M"),
          QStringLiteral("eta"),
          QStringLiteral("mu"),
          QStringLiteral("Event_Count"),
          QStringLiteral("Mmax_obs"),
          QStringLiteral("Mmean_rectangular"),
          QStringLiteral("dE_sqrt_rectangular"),
          QStringLiteral("Mmean_half_gauss"),
          QStringLiteral("dE_sqrt_half_gauss")};
}

double WindowIndicators::value(const QString &column) const {
  if (column == QLatin1String("T"))
    return T;
  if (column == QLatin1String("b_value"))
    return bValue;
  if (column == QLatin1String("a_value"))
    return aValue;
  if (column == QLatin1String("Delta_M"))
    return deltaM;
  if (column == QLatin1String("eta"))
    return eta;
  if (column == QLatin1String("mu"))
    return mu;
  if (column == QLatin1String("Event_Count"))
    return eventCount;
  if (column == QLatin1String("Mmax_obs"))
    return mmaxObs;
  if (column == QLatin1String("Mmean_rectangular"))
    return mmeanRectangular;
  if (column == QLatin1String("dE_sqrt_rectangular"))
    return dEsqrtRectangular;
  if (column == QLatin1String("Mmean_half_gauss"))
    return mmeanHalfGauss;
  if (column == QLatin1String("dE_sqrt_half_gauss"))
    return dEsqrtHalfGauss;
  return kNaN;
}

double magnitudeToEnergy(double magnitude) {
  return std::pow(10.0, kEnergyConstant + kEnergyMagnitudeCoeff * magnitude);
}

WindowIndicators computeWindowIndicators(const std::vector<double> &magnitudes,
                                         const std::vector<double> &epochs,
                                         double mc, double halfGaussSigma,
                                         double magBin) {
  WindowIndicators r;
  r.T = r.bValue = r.aValue = r.deltaM = r.eta = r.mu = kNaN;
  r.mmaxObs = r.mmeanRectangular = r.dEsqrtRectangular = kNaN;
  r.mmeanHalfGauss = r.dEsqrtHalfGauss = kNaN;
  const int n = int(std::min(magnitudes.size(), epochs.size()));
  r.eventCount = n;
  if (n == 0)
    return r;
  const size_t un = static_cast<size_t>(n);

  const double tSeconds = epochs[un - 1] - epochs[0];
  const double tYears = tSeconds / kSecondsPerYear;

  // Gutenberg-Richter b-value (Aki maximum likelihood) and annual a-value.
  std::vector<double> aboveMc;
  for (size_t i = 0; i < un; ++i)
    if (magnitudes[i] >= mc)
      aboveMc.push_back(magnitudes[i]);
  double b = kNaN;
  if (!aboveMc.empty()) {
    const double mean = std::accumulate(aboveMc.begin(), aboveMc.end(), 0.0) /
                        double(aboveMc.size());
    const double excess = mean - (mc - magBin / 2);
    if (excess > 0)
      b = 1.0 / (std::log(10.0) * excess);
  }
  double a = kNaN;
  if (tYears > 0 && !aboveMc.empty())
    a = std::log10(double(aboveMc.size()) / tYears) + b * mc;

  // Magnitude deficit: observed vs. GR-expected maximum magnitude.
  const double mmaxObs =
      *std::max_element(magnitudes.begin(), magnitudes.begin() + n);
  const double mmaxExp = a / b;

  // Rate of square-root seismic energy release.
  std::vector<double> sqrtEnergy(un);
  double sumSqrtE = 0;
  for (size_t i = 0; i < un; ++i) {
    sqrtEnergy[i] = std::sqrt(magnitudeToEnergy(magnitudes[i]));
    sumSqrtE += sqrtEnergy[i];
  }
  const double dEsqrt = tSeconds > 0 ? sumSqrtE / tSeconds : kNaN;

  // eta: mean squared deviation from the GR line over the unique magnitudes
  // above Mc.
  double eta = kNaN;
  std::vector<double> unique = aboveMc;
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  if (unique.size() > 1) {
    double ss = 0;
    for (double m : unique) {
      int count = 0;
      for (size_t i = 0; i < un; ++i)
        if (magnitudes[i] >= m)
          ++count;
      const double diff = std::log10(double(count)) - (a - b * m);
      ss += diff * diff;
    }
    eta = ss / double(unique.size() - 1);
  }

  // mu: mean interval between characteristic events.
  double muSeconds = kNaN;
  std::vector<double> charTimes;
  for (size_t i = 0; i < un; ++i)
    if (magnitudes[i] >= mc + kCharacteristicOffset)
      charTimes.push_back(epochs[i]);
  if (charTimes.size() > 1)
    muSeconds =
        (charTimes.back() - charTimes.front()) / double(charTimes.size() - 1);

  // Half-Gauss weighted variants; weights scaled to mean 1 so the weighted
  // energy sum stays on the rectangular scale.
  std::vector<double> w = halfGaussWindow(n, halfGaussSigma);
  const double wsum = std::accumulate(w.begin(), w.end(), 0.0);
  for (double &v : w)
    v *= double(n) / wsum;
  double wm = 0, wE = 0, ws = 0, meanM = 0;
  for (size_t i = 0; i < un; ++i) {
    wm += w[i] * magnitudes[i];
    wE += w[i] * sqrtEnergy[i];
    ws += w[i];
    meanM += magnitudes[i];
  }

  r.T = tSeconds / kSecondsPerDay;
  r.bValue = b;
  r.aValue = a;
  r.deltaM = mmaxObs - mmaxExp;
  r.eta = eta;
  r.mu = muSeconds / kSecondsPerDay;
  r.mmaxObs = mmaxObs;
  r.mmeanRectangular = meanM / double(n);
  r.dEsqrtRectangular = dEsqrt;
  r.mmeanHalfGauss = wm / ws;
  r.dEsqrtHalfGauss = tSeconds > 0 ? wE / tSeconds : kNaN;
  return r;
}

} // namespace skg::cbi
