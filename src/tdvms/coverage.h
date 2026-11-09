// What the downloaded archives actually cover, and where the holes are.
// Port of tdvms/coverage.py: reads only miniSEED 2 record headers (start
// time, sample count, rate) straight out of the archives, never the samples.
// Results are cached per archive in <raw>/.coverage.json by size and mtime.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

#include <functional>
#include <optional>

#include "tdvms/ledger.h"

namespace skg::tdvms {

using Span = QPair<double, double>; // [start, end) in seconds since the epoch
using Spans = QList<Span>;

struct ChannelSpans {
  double rate = 0;
  Spans spans;
};
using ScanResult = QMap<QString, ChannelSpans>; // "NET.STA.LOC.CHA" -> spans

// Samples per second from the header's factor and multiplier (SEED 2.4).
double sampleRate(int factor, int multiplier);

// Incremental miniSEED 2 header scanner: feed() bytes in any chunking,
// finish() to get the merged spans. Throws std::runtime_error on something
// that is not miniSEED.
class MseedScanner {
public:
  void feed(const QByteArray &bytes);
  ScanResult finish();

private:
  QByteArray pending_;
  ScanResult spans_;
};
ScanResult scanBytes(const QByteArray &bytes);
// Every .mseed member of one archive, merged per channel.
ScanResult scanArchive(const QString &zipPath);

// Interval arithmetic on merged span lists.
Spans mergeSpans(Spans spans, double tol = 0.0);
Spans clipSpans(const Spans &spans, double lo, double hi);
Spans subtractSpans(const Spans &wanted, const Spans &have);
double spanLength(const Spans &spans);

struct Gap {
  double start, end;
  QString why; // "data gap", "no data at source", "not downloaded yet",
               // "unavailable"
  double seconds() const { return end - start; }
};

struct ChannelCoverage {
  QString cid;
  double rate = 0;
  double covered = 0; // seconds of the requested period present
  double requested = 0;
  Spans spans;
  QList<Gap> gaps;
  double fraction() const { return requested > 0 ? covered / requested : 0; }
  QString label() const; // "HHZ", or "00.HHZ" with a location code
};

struct WindowState {
  double start, end;
  QString state;
};

struct StationCoverage {
  QString code;
  QList<WindowState> windows; // sorted by start
  QList<ChannelCoverage> channels;
  Spans requested() const;
  Span span() const;
  QString stateAt(double t) const;
};

QString gapReason(const QString &state);

using Progress = std::function<void(int done, int total, const QString &name)>;
// The report for a ledger whose archives are filed under `raw`.
QList<StationCoverage> coverageReport(const Ledger &ledger, const QString &raw,
                                      const QStringList &stations = {},
                                      bool refresh = false,
                                      const Progress &progress = {});

// One cell per bucket across the station's requested time, laid end to end:
// kind is "full", "partial", "gap", "nodata", "pending" or "none".
struct TimelineCell {
  QString glyph;
  QString kind;
  double fraction;
};
QList<TimelineCell> timeline(const StationCoverage &station,
                             const ChannelCoverage *channel, int width);

QString formatSeconds(double s);
QString formatTime(double t);

} // namespace skg::tdvms
