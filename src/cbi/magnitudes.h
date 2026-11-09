// Homogenising catalog magnitudes across changes in reporting practice.
// AFAD switched from Md to Ml around the 2011 Van earthquake and to ML/Mw
// on 2013-11-20; each earlier period is mapped onto the 2014-2019 reference
// by matching annual exceedance rates of declustered events (frequency-
// magnitude matching, cf. Tormann, Wiemer & Hauksson 2010). Also the
// maximum-curvature magnitude of completeness. Port of indicators-
// forecast's magnitudes.py.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

#include "cbi/catalog.h"

namespace skg::cbi {

struct PeriodMap {
  std::optional<QDateTime> start; // inclusive; nullopt = open-ended
  QDateTime end;                  // exclusive
  std::vector<double> mFrom;      // knots on the period's scale, increasing
  std::vector<double> mTo;        // matching reference magnitudes

  // Piecewise linear between the knots, a constant shift beyond them.
  double convert(double m) const;
  bool contains(double epoch) const;
};

struct MagnitudeConversion {
  std::vector<PeriodMap> periods;
  QDateTime referenceStart, referenceEnd;
  double magBin = 0.1;
  QString note;

  // Converts the magnitudes of each period and re-bins them; the originals
  // go to Catalog::originalMagnitude.
  void apply(Catalog &catalog) const;

  QJsonObject toJson() const;
  static MagnitudeConversion fromJson(const QJsonObject &o); // throws
  static MagnitudeConversion load(const QString &path);      // throws
  bool save(const QString &path) const;
  // The conversion shipped with the library (data/afad_magnitude_map.json).
  static MagnitudeConversion afadDefault();
};

// Magnitude of completeness: the modal magnitude bin plus a correction
// (+0.2 after Woessner & Wiemer 2005).
double maxCurvatureMc(const std::vector<double> &magnitudes,
                      double magBin = 0.1, double correction = 0.2);

// Knots (m_period, m_reference) with equal exceedance rates, from the
// events of `period` and `reference` (each [start, end) as epochs). Only
// magnitudes above both periods' Mc with at least minCount exceeding events
// in both are used. Returns empty vectors when nothing overlaps.
std::pair<std::vector<double>, std::vector<double>>
fitRateMatching(const Catalog &catalog, std::pair<double, double> period,
                std::pair<double, double> reference, int minCount = 20,
                double magBin = 0.1);

// The AFAD reporting periods and the reference the fork calibrates on.
struct AfadPeriods {
  std::vector<std::pair<std::optional<QDateTime>, QDateTime>> periods;
  std::vector<std::pair<QDateTime, QDateTime>> calibration;
  std::pair<QDateTime, QDateTime> reference;
};
AfadPeriods afadPeriods();

// Fits the conversion for every AFAD period on a (national) catalog,
// declustered first. `log` gets the knots; throws when a period is empty.
MagnitudeConversion calibrateAfadConversion(const Catalog &national,
                                            int minCount = 20,
                                            QStringList *log = nullptr);

} // namespace skg::cbi
