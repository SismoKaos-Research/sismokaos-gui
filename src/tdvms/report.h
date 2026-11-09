// Where the campaign stands, including the parts that are stuck. Port of
// tdvms/report.py (`tdvms status`), as lines of text for the GUI's status
// pane and as structured counts for its tables.
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

#include "tdvms/ledger.h"
#include "tdvms/slots.h"

namespace skg::tdvms {

extern const QStringList kStateOrder; // pending, claimed, ... retired

struct Turnaround {
  double medianMin, minMin, maxMin;
  int n;
};
// Median and range of submit-to-fetch in minutes over rows that have both
// stamps in order; nullopt when none do.
std::optional<Turnaround> turnaround(const QList<Row> &rows);

struct StationCounts {
  QString station;
  QMap<QString, int> byState;
  qint64 fetchedBytes = 0;
  int total() const;
  bool neverReturnedData() const; // 0 fetched and >= 8 no-data
};
QList<StationCounts> perStation(const QList<Row> &rows);

// The full `tdvms status` text. `pool` may be null.
QStringList summarise(const Ledger &ledger, Pool *pool = nullptr);

} // namespace skg::tdvms
