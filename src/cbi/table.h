// Catalog -> per-window indicator table: which events to analyse (fault
// zone, start date, homogenisation, declustering), the sliding windows and
// their indicators, the large-event markers and the pre-event risk label,
// trailing moving averages and Bollinger bands, and the CSV the originals
// write (same columns, pandas number formatting). Port of indicators-
// forecast's pipeline.py, events.py and smoothing.py.
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

#include "cbi/catalog.h"
#include "cbi/indicators.h"
#include "cbi/windows.h"

namespace skg::cbi {

constexpr double kLargeEventMag = 5.0;
constexpr double kMajorEventMag = 6.0;
// Extra distance around the zone used when declustering, so aftershocks of
// events just outside the buffer are still recognised (the Gardner-Knopoff
// window of an M7.7 is ~86 km).
constexpr double kDeclusterPadKm = 100.0;
// The Risk_Durumu values, part of the CSV format.
QString riskNear(); // "3 aydan az"
QString riskFar();  // "3 aydan fazla"

struct IndicatorTable {
  std::vector<QDateTime> start, end;       // [start, end), UTC
  QStringList xLabel;                      // "2018-06 - 2018-12"
  QStringList columns;                     // numeric columns, in file order
  std::vector<std::vector<double>> values; // columns.size() x rows
  QStringList risk;                        // Risk_Durumu per row, or empty

  int rows() const { return int(start.size()); }
  bool empty() const { return start.empty(); }
  int columnIndex(const QString &name) const;
  const std::vector<double> *column(const QString &name) const;
  std::vector<double> *column(const QString &name);
  // Appends the column, or replaces it when the name exists.
  void addColumn(const QString &name, std::vector<double> v);
};

struct CatalogSelection {
  enum class Zone { None, Eafz, GeoJson, Circle };
  Zone zone = Zone::Eafz;
  QString zoneFile;    // Zone::GeoJson
  QStringList strands; // Eafz/GeoJson; empty = every strand
  double bufferKm = 30.0;
  double centerLat = 38.0, centerLon = 38.0, radiusKm = 100.0; // Circle
  std::optional<QDateTime> start; // drop earlier events
  bool homogenize = false;        // pre-2014 AFAD magnitudes -> ML/Mw
  QString conversionFile;         // empty = the shipped AFAD map
  bool decluster = false;         // keep Gardner-Knopoff mainshocks only

  // Short name used in output file names: "eafz30km_hom_dc", "all", ...
  QString tag() const;
  // Applies the selection; `log` gets one line per step. Throws
  // std::runtime_error (zone file, conversion file).
  Catalog apply(const Catalog &catalog, QStringList *log = nullptr) const;
};

struct IndicatorConfig {
  double mc = 1.3;
  Duration window{12, QLatin1Char('M')};
  Duration step{1, QLatin1Char('M')};
  double halfGaussSigma = 0.4;
  double magBin = kDefaultMagBin;
};

// One row per window over a time-sorted catalog, with the first-appearance
// markers of M >= 5 events attached.
IndicatorTable computeIndicatorTable(const Catalog &catalog,
                                     const IndicatorConfig &cfg);
// Mmax_obs_first_5_to_6 / Mmax_obs_first_6_plus: each M >= 5 event is
// assigned to the first window containing it; a window holds the largest
// such newly appearing event per band, else 0.
void markFirstAppearance(IndicatorTable &table, const Catalog &catalog);
// Risk_Durumu: riskNear() when a target event occurs 0..days days after the
// window's end.
void labelPreEventWindows(IndicatorTable &table,
                          const std::vector<double> &targetEpochs,
                          int days = 90);
// <col>_MA<window>: trailing mean over the last `window` rows (NaNs skipped).
void addMovingAverage(IndicatorTable &table, int window,
                      const QStringList &columns = indicatorColumns());
// <col>_BB_MA, _BB_Upper, _BB_Lower, _BB_Width (sample std, trailing).
void addBollingerBands(IndicatorTable &table, int window = 20, double k = 2.0,
                       const QStringList &columns = indicatorColumns());

// UTF-8 with BOM, pandas' number formatting (NaN empty). Returns false when
// the file cannot be written.
bool writeTableCsv(const IndicatorTable &table, const QString &path);
IndicatorTable readTableCsv(const QString &path); // throws std::runtime_error
// pandas' float repr: the shortest text that reads back to the value.
QString pandasNumber(double v);

} // namespace skg::cbi
