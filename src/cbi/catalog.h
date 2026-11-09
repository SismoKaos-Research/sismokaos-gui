// The earthquake catalog the indicators run on: the AFAD export read by the
// analysis library, sorted by time, with the original magnitudes kept
// beside the homogenised ones. Port of indicators-forecast's catalog.py.
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <vector>

#include "analysis/events.h"

namespace skg::cbi {

struct Catalog {
  std::vector<analysis::Event> events; // sorted by epoch
  // Magnitudes before homogenisation (same size as events) or empty.
  std::vector<double> originalMagnitude;

  size_t size() const { return events.size(); }
  bool empty() const { return events.empty(); }
  // Events where keep[i] is true, in order.
  Catalog filtered(const std::vector<bool> &keep) const;
  std::vector<double> magnitudes() const;
  std::vector<double> epochs() const;
};

// Reads an AFAD-format CSV (Date dd/mm/YYYY HH:MM:SS, Latitude, Longitude,
// Magnitude, ...; plain or .gz) sorted by time. Throws std::runtime_error.
Catalog loadCatalog(const QString &path);
// The fork's three-line summary (count, time span, magnitude span).
QStringList describeCatalog(const Catalog &c, const QString &lang);

// Helpers shared by the library: UTC seconds <-> QDateTime, numpy's
// round-half-even to a step, and the ISO date text of an epoch.
QDateTime toDateTime(double epoch);
double toEpoch(const QDateTime &dt);
double roundToStep(double value, double step); // round(round(v/step)*step, 2)
double roundDecimals(double value, int decimals);
QString isoDate(double epoch);     // "2020-01-24"
QString isoDateTime(double epoch); // "2020-01-24 17:55:11"

} // namespace skg::cbi
