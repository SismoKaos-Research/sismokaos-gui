// The starting values the pages share, in one place, so that the same
// quantity does not start at 150 km on one page and 200 km on the next.
// A page that offers one of these starts from the value here; what the user
// then sets is kept in the settings or in the study as before.
#pragma once

#include <QSettings>
#include <QString>

namespace skg::defaults {

// Stations offered around an epicentre (the add-case dialog, the case card).
inline constexpr double kStationRadiusKm = 150.0;
// The catalog zone of a case: a circle of this radius around the epicentre.
inline constexpr double kZoneRadiusKm = 100.0;
// A fault zone: events within this distance of the traces.
inline constexpr double kFaultBufferKm = 30.0;
// Where a place starts before the user gives one (eastern Anatolia).
inline constexpr double kCentreLat = 38.0;
inline constexpr double kCentreLon = 39.0;
// How long before the event a case is studied.
inline constexpr int kSpanDays = 365;
// The risk label of the indicator table: 1 for the days within kRiskLabelDays
// before an earthquake of at least kRiskLabelMag. Recipe keys "label_mag" and
// "label_days".
inline constexpr double kRiskLabelMag = 6.0;
inline constexpr int kRiskLabelDays = 90;
// A catalog whose magnitudes start above this (its floor, catalogs.h) is
// sparse: the quiet windows of a calibration then only avoid earthquakes of
// that size and larger, and small local ones sit inside them unseen.
inline constexpr double kSparseCatalogMag = 3.0;
// The indicators need events down to the recipe's completeness magnitude.
// A catalog whose floor is above Mc by more than this cannot give them.
inline constexpr double kMcFloorMargin = 0.5;
// A station with data on fewer days of the case's span than this share of
// it is used with a warning.
inline constexpr double kMinSpanCoverage = 0.10;
// How many cells of the Runs page run at the same time; the rest wait in
// the queue. Setting "runs/atOnce".
inline constexpr int kRunsAtOnce = 2;
inline constexpr char kRunsAtOnceSetting[] = "runs/atOnce";
// Screen-sized figures; print needs 300.
inline constexpr int kFigureDpi = 100;

// Figures carry text (axis titles, legends). It is in the language of the
// interface unless a recipe names one.
inline QString figureLanguage(const QSettings *settings) {
  const QString code =
      settings ? settings->value(QStringLiteral("ui/language")).toString()
               : QString();
  return code.isEmpty() ? QStringLiteral("en") : code;
}

} // namespace skg::defaults
