// Earthquake and station catalogs for the analysis tools: the AFAD event
// export (Date, Latitude, Longitude, Depth, Type, Magnitude, Location),
// station lists, and the searches SCAP's catalog page offers: by date,
// magnitude and place; stations near an event; events near a station by
// SCAP's distance bands; the main event of a period.
#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace skg::analysis {

struct Event {
  double epoch = 0; // UTC seconds
  double lat = 0, lon = 0;
  double depthKm = 0;
  double mag = 0;
  QString type;  // ML, Mw, ...
  QString place; // "Sivrice (Elazig)"
  QString id;
};

struct Station {
  QString code, network;
  double lat = 0, lon = 0;
  double heightM = 0;
  QString province, district;
};

// Reads the AFAD CSV (plain or .gz). The export's times are taken as UTC
// plus `catalogUtcOffsetHours` (SCAP keeps one catalog in Turkey time,
// +3, and one in UTC). Throws std::runtime_error.
std::vector<Event> loadEvents(const QString &path,
                              double catalogUtcOffsetHours = 0.0);
// Code, latitude, longitude (and network, height, province, district when
// present), as the AFAD station list or tdvms's embedded one. Throws.
std::vector<Station> loadStations(const QString &path);
std::optional<Station> findStation(const std::vector<Station> &stations,
                                   const QString &code);

struct EventQuery {
  std::optional<double> startEpoch, endEpoch; // [start, end)
  std::optional<double> minMag, maxMag;
  QString placeContains; // case-insensitive
  std::optional<double> centerLat, centerLon, radiusKm;
};
std::vector<Event> selectEvents(const std::vector<Event> &events,
                                const EventQuery &q);

struct StationDistance {
  Station station;
  double km = 0;
  double bearing = 0; // from the reference point to the station
};
std::vector<StationDistance>
stationsWithin(const std::vector<Station> &stations, double lat, double lon,
               double radiusKm);

// SCAP's bands: which magnitudes count at which distance from the station.
struct DistanceBand {
  double minKm, maxKm, minMag;
  QString name;
};
const std::vector<DistanceBand> &
scapBands(); // 0-100 all, 100-300 M3+, 300-500 M5+, 500-1000 M6+

struct EventAtStation {
  Event event;
  double km = 0;
  double bearing = 0; // station -> event
  int band = -1;      // index into the bands, -1 when none applies
};
// Events relevant to a station: within some band, with distance and bearing.
std::vector<EventAtStation>
eventsForStation(const std::vector<Event> &events, double lat, double lon,
                 const std::vector<DistanceBand> &bands = scapBands());
// The largest event of magnitude >= minMag among those, or nullopt.
std::optional<EventAtStation>
mainEvent(const std::vector<EventAtStation> &events, double minMag = 5.0);

// "24012020_M6.8_Sivrice__Elazig": SCAP's folder name for an event.
QString eventFolderName(const Event &e);

} // namespace skg::analysis
