// Fault-zone geometries and the selection of catalog events near them: a
// GeoJSON of LineString features (property "strand" optional), the East
// Anatolian Fault Zone shipped with the library, and distances to the
// trace in a local equirectangular projection. Port of indicators-
// forecast's regions package.
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "cbi/catalog.h"

namespace skg::cbi {

struct FaultZone {
  QString name;
  struct Segment {
    double lon0, lat0, lon1, lat1;
  };
  std::vector<Segment> segments;
  QStringList strands; // the strands kept

  // Distance from (lon, lat) to the nearest trace segment, km.
  double distanceKm(double lon, double lat) const;
  bool empty() const { return segments.empty(); }
};

// The strands a GeoJSON file offers (empty when its features carry none).
QStringList zoneStrands(const QString &path); // throws
// The zone of a GeoJSON file; `strands` empty keeps every feature, an
// unknown strand throws std::runtime_error.
FaultZone loadFaultZone(const QString &path, const QStringList &strands = {});
// The shipped EAFZ (strands "main" and "northern").
QString eafzResource();
FaultZone eafz(const QStringList &strands = {});

// Events whose epicentre lies within bufferKm of the zone's trace.
Catalog selectNearFault(const Catalog &catalog, const FaultZone &zone,
                        double bufferKm);

} // namespace skg::cbi
