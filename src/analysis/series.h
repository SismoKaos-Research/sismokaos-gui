// A feature column as a time series, cut the ways SCAP's figures need it:
// per UTC day (minutes into the day), all days end to end, and a window of
// days centred on the main event. Port of data_processing.py and the date
// arithmetic of earthquake_processing.py.
#pragma once

#include <QDate>
#include <QString>

#include <optional>
#include <vector>

#include "fx/npy.h"

namespace skg::analysis {

struct MetricSeries {
  QString column;
  std::vector<double> epoch; // window end, UTC seconds
  std::vector<double> value; // NaN where the window has none
  bool empty() const { return epoch.empty(); }
};

// The column `name` of a table against the window ends. Rows that cannot be
// placed in time are skipped. Throws std::invalid_argument for a missing
// column.
MetricSeries metricSeries(const fx::NpyTable &table, const QString &name,
                          double winSec = 200);
// The numeric columns of a table that are not ids, times or labels: what
// can be plotted.
QStringList plottableColumns(const fx::NpyTable &table);

// The UTC days the series touches, sorted.
std::vector<QDate> daysOf(const MetricSeries &s);

struct DaySlice {
  QDate day;
  std::vector<double> minutes; // into the day
  std::vector<double> value;   // NaN rows dropped
};
DaySlice daySlice(const MetricSeries &s, const QDate &day);

// Positive and negative areas of a day's trace, in value-minutes (trapezoids).
struct Areas {
  double positive = 0, negative = 0;
};
Areas areasOf(const DaySlice &d);

// SCAP's window of `windowDays` around the event's day, clipped to the days
// available: (n-1)/2 days before, the rest after.
std::vector<QDate> centredDays(const QDate &eventDay,
                               const std::vector<QDate> &available,
                               int windowDays);

// A y range for a column: SCAP's fixed ranges for its five metrics, else
// symmetric around zero for signed data and 0..max otherwise, padded.
struct YRange {
  double min = 0, max = 1;
};
std::optional<YRange> scapRange(const QString &column);
YRange autoRange(const MetricSeries &s);

// SCAP's display names for its five metrics; the column itself otherwise.
QString metricTitle(const QString &column, const QString &lang);
QString monthName(int month, const QString &lang);

} // namespace skg::analysis
