// Fault-zone geometries and the selection of catalog events near them: a
// GeoJSON of LineString features (property "strand" optional), the East
// Anatolian Fault Zone and the fault layer of the maps (both shipped as data
// files beside the program), and distances to the trace in a local
// equirectangular projection. Port of indicators-
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
// ---- data files shipped beside the program ----
// The fault files (data/faults in the source tree) carry their own licences
// (CC BY-SA 4.0 and CC BY 4.0), so they are installed as files next to the
// program instead of being compiled in. They are looked for, in order, in
// $SKG_DATA_DIR, <program dir>/data, <program dir>/../share/sismokaos-gui/
// data, <program dir>/../Resources/data (macOS bundle) and the source tree
// the program was built from.
QStringList dataDirs();
// The first existing `name` in dataDirs(); throws std::runtime_error naming
// every place searched when there is none.
QString dataFile(const QString &name);

// The EAFZ zone definition (GEM GAF-DB, strands "main" and "northern"):
// catalog events are selected by their distance to these traces.
QString eafzPath(); // throws when the file is not installed
FaultZone eafz(const QStringList &strands = {});

// The fault layer drawn on maps when the user has loaded none: seismogenic
// fault sources of Turkey and its neighbours (EFSM20, a hazard model; not an
// active fault map). It has no strands. Read once; throws when the file is
// not installed, so callers can say that the layer is missing.
QString shippedFaultsPath();
const FaultZone &shippedFaults();
// What to call that layer in a legend, and the attribution its licence asks
// for, in the figure language ("tr" or anything else for English).
QString shippedFaultsLabel(const QString &lang);
QString shippedFaultsCredit(const QString &lang);

// Events whose epicentre lies within bufferKm of the zone's trace.
Catalog selectNearFault(const Catalog &catalog, const FaultZone &zone,
                        double bufferKm);

} // namespace skg::cbi
