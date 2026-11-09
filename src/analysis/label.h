// Earthquake labels for a feature table: for every window, the events whose
// origin time falls inside it, with magnitude, position, distance and
// direction from the station, the distance class and a 0/1 flag. Port of
// SCAP's label_earthquakes.py, reading the fx tables (Pencere_ID,
// Zaman_Dk as epoch minutes of the window end) as well as SCAP's own
// (YYYY_MM_DD_HH_wNN ids with Zaman_Dk minutes into the hour).
#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

#include "analysis/events.h"
#include "fx/npy.h"

namespace skg::analysis {

struct WindowSpan {
  double start = 0, end = 0; // UTC seconds, [start, end)
};

// The time span of row `row` of a table. `winSec` is used for fx tables
// (end - winSec) and as SCAP's fixed 200 s otherwise. nullopt when the row
// cannot be placed in time.
std::optional<WindowSpan> windowSpan(const fx::NpyTable &t, int row,
                                     double winSec);

struct LabelOptions {
  double winSec = 200;
  std::optional<double> stationLat,
      stationLon;                      // with these, distances and directions
  QString lang = QStringLiteral("tr"); // for the direction names
};

struct LabelSummary {
  int windows = 0, withEvents = 0;
  QStringList columnsAdded;
};

// Appends SCAP's columns (Zaman_Araligi, Magnitude, Latitude, Longitude,
// Distance_km, Mesafe_Derecesi, Yön_Bilgisi, Deprem_Sayisi, Etiket) to the
// table. Existing columns of those names are replaced.
LabelSummary labelWindows(fx::NpyTable &table, const std::vector<Event> &events,
                          const LabelOptions &opt);

// Writes a table as CSV (UTF-8 with BOM, like SCAP), NaN as empty cells.
bool writeCsv(const QString &path, const fx::NpyTable &table);
// Reads a CSV into a table: numeric columns become f8, the rest U strings.
fx::NpyTable readCsvTable(const QString &path); // throws std::runtime_error
// Loads .npy or .csv by extension.
fx::NpyTable loadTable(const QString &path);

} // namespace skg::analysis
