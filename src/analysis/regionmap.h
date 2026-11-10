// The regions maps, drawn with QPainter: the epicentres (grey, or coloured
// by region with the region centres as stars) over country and province
// boundaries and the active fault lines when those layers are given, with
// city labels, graticule, scale bar, north arrow, legend and the boundary
// disclaimer. Port of the four maps of earthquake-clustering-eafz.
#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

#include "analysis/faults.h"
#include "analysis/regions.h"
#include "analysis/shapefile.h"

class QPainter;

namespace skg::analysis {

struct City {
  double lat = 0, lon = 0;
  QString name;
  bool labelLeft = false; // the label sits left of the marker
};
// The script's sixteen cities around the East Anatolian Fault Zone.
const std::vector<City> &eafzCities();
// A CSV of latitude, longitude and name (headers lat/latitude/enlem,
// lon/longitude/boylam, name/city/sehir/şehir/il, or the first three
// columns without a header). Throws std::runtime_error.
std::vector<City> loadCities(const QString &path);

struct FaultLine {
  std::vector<QPointF> line; // (lon, lat)
  QString activity;          // MTA's AKTIVITE: DYK, H-DYK, H, K-H, K; or empty
};
std::vector<FaultLine> faultLines(const FaultIndex &faults);

// Polygon and MultiPolygon features of a GeoJSON file as a shape layer;
// each record holds the feature's properties. Throws std::runtime_error.
ShapeLayer readGeoJsonPolygons(const QString &path);
// Turkey and its neighbours, clipped and simplified from Natural Earth
// (data/PROVENANCE.md); records carry "name" and "sovereignt".
const ShapeLayer &shippedCountries();

struct MapMarker {
  double lat = 0, lon = 0;
  QString label;
};

struct RegionMapSpec {
  QString lang = QStringLiteral("tr");
  QString title;
  const std::vector<Point2> *points = nullptr;
  const Regions *regions = nullptr; // null: every event in one colour
  const ShapeLayer *countries = nullptr;
  const ShapeLayer *provinces = nullptr;
  const std::vector<FaultLine> *faults = nullptr;
  // For a fault layer that is not an active fault map (the shipped EFSM20
  // fault sources): what the legend calls its lines, in the map's language.
  // Such lines are drawn muted. Empty: "Active Fault".
  QString faultsLabel;
  // The attribution a layer's licence asks for, printed under the map.
  QString credit;
  std::vector<City> cities;
  std::optional<MapMarker> marker;
  double padDeg = 0.4; // around the events
};

// Wong's colour-blind-safe palette for up to eight regions, a 20-colour
// one beyond.
QColor regionColor(int index, int count);
// The design size (12 x 10 inches at `dpi`).
QSize regionMapSize(int dpi = 100);
// The (lon, lat) rectangle of the events plus the padding.
QRectF regionMapExtent(const RegionMapSpec &spec);
void drawRegionMap(QPainter &p, const QRectF &rect, const RegionMapSpec &spec);

} // namespace skg::analysis
