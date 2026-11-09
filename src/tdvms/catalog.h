// The station and earthquake catalogs shipped as resources (see
// data/PROVENANCE.md), so a campaign can be planned offline. Port of
// tdvms/catalog.py. A refresh writes updated copies into the user's config
// dir, which are preferred over the embedded ones when present.
#pragma once

#include <QList>
#include <QString>

#include "tdvms/http.h"

namespace skg::tdvms {

struct CatalogStation {
  QString code, network;
  double latitude = 0, longitude = 0;
  QString devices; // of "HLN"
  QString key() const { return network + '.' + code; }
  bool requestable() const { return !devices.isEmpty(); }
};

struct CatalogEvent {
  QString id, time; // time: ISO UTC, seconds
  double magnitude = 0, latitude = 0, longitude = 0, depthKm = 0;
  QString place;
  QString date() const { return time.left(10); }
};

double distanceKm(double lat1, double lon1, double lat2, double lon2);

QList<CatalogStation> loadStations();
QList<CatalogEvent> loadEvents();
// Where refreshed copies live: <config>/stations.csv, <config>/events.csv.
QString stationsPath();
QString eventsPath();

// Refresh both from their sources (the portal, USGS). Throws
// std::runtime_error on failure; returns how many of each were written.
QPair<int, int> refreshCatalogs(Http &http);

// Parsing of the CSV text, public for tests.
QList<CatalogStation> parseStations(const QByteArray &csv);
QList<CatalogEvent> parseEvents(const QByteArray &csv);

} // namespace skg::tdvms
