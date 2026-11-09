// The AFAD earthquake catalog and station list, used to find the quiet
// stretches the chaos parameters are calibrated on. Port of sismokaos-cli's
// catalog.rs. The catalog's Date column is day-first and already UTC.
#pragma once

#include <QList>
#include <QString>

#include <optional>
#include <vector>

#include "fx/config.h"

namespace skg::fx {

struct CatalogEvent {
  double epoch, mag, lat, lon;
};

// dd/mm/YYYY HH:MM:SS[.fff] as UTC epoch seconds.
std::optional<double> parseAfadTime(const QString &s);
// Reads a CSV or .csv.gz with Date, Magnitude, Latitude, Longitude columns.
// Throws std::runtime_error.
std::vector<CatalogEvent> loadCatalog(const QString &path);
// The station's position from a code/latitude/longitude CSV. nullopt when
// absent; throws std::runtime_error when listed at several places.
std::optional<std::pair<double, double>> stationCoords(const QString &path,
                                                       const QString &station);
double haversineKm(double lat1, double lon1, double lat2, double lon2);
// Seconds after an origin time during which the data are not quiet: the
// larger of minPostSec and codaFactor times the Lee (1972) coda duration.
double postEventSec(double mag, const CalibrationConfig &opts);

// Merged, sorted intervals during which a station is not quiet.
class Exclusions {
public:
  Exclusions() = default;
  Exclusions(const std::vector<CatalogEvent> &events,
             std::pair<double, double> station, const CalibrationConfig &opts);
  int size() const { return int(starts_.size()); }
  bool overlaps(double a, double b) const; // [a, b) touches an interval
  int countWithin(double a, double b) const;

private:
  std::vector<double> starts_, ends_;
};

// gzip (RFC 1952) inflate, for the compressed catalog. Throws on error.
QByteArray gunzip(const QByteArray &gz);

} // namespace skg::fx
