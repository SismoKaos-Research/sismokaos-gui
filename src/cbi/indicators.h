// The seismic indicators of one time window: elapsed time, mean magnitude
// (rectangular and half-Gauss weighted), Aki/Utsu b-value and annual
// a-value, magnitude deficit, square-root energy release rate, eta
// (deviation from the Gutenberg-Richter line) and mu (mean interval of
// characteristic events). Port of indicators-forecast's indicators.py,
// which fixed the original tool's zeros for undefined values (NaN here).
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <vector>

namespace skg::cbi {

constexpr double kEnergyConstant = 11.8;
constexpr double kEnergyMagnitudeCoeff = 1.5;
// Characteristic events are those with M >= Mc + this.
constexpr double kCharacteristicOffset = 1.0;
// Magnitude rounding of AFAD catalogs; the b-value uses Mc - bin/2.
constexpr double kDefaultMagBin = 0.1;

struct IndicatorInfo {
  QString column;
  QString labelTr, labelEn;
  QColor color;
  QString label(const QString &lang) const {
    return lang == QLatin1String("tr") ? labelTr : labelEn;
  }
};
// The indicators that are plotted and smoothed, in display order.
const std::vector<IndicatorInfo> &indicators();
QStringList indicatorColumns();
const IndicatorInfo *indicatorInfo(const QString &column);

struct WindowIndicators {
  double T = 0; // elapsed time between the first and last event, days
  double bValue = 0, aValue = 0, deltaM = 0, eta = 0, mu = 0;
  int eventCount = 0;
  double mmaxObs = 0;
  double mmeanRectangular = 0, dEsqrtRectangular = 0;
  double mmeanHalfGauss = 0, dEsqrtHalfGauss = 0;

  // Value by output column name ("b_value", "Event_Count", ...).
  double value(const QString &column) const;
};
// The output columns in the fork's order.
QStringList windowIndicatorColumns();

double magnitudeToEnergy(double magnitude);

// Indicators of one window; `magnitudes` and `epochs` sorted by time.
// Undefined indicators are NaN; an empty window gives NaN everywhere but
// Event_Count.
WindowIndicators computeWindowIndicators(const std::vector<double> &magnitudes,
                                         const std::vector<double> &epochs,
                                         double mc, double halfGaussSigma = 0.4,
                                         double magBin = kDefaultMagBin);

} // namespace skg::cbi
