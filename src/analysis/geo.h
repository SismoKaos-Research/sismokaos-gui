// Geometry on the sphere for the analysis tools: distances, bearings,
// compass directions, a local flat projection, and the distance from a
// point to a polyline. Port of the helpers spread over SCAP's catalog.py
// and label_earthquakes.py.
#pragma once

#include <QPointF>
#include <QString>

#include <vector>

namespace skg::analysis {

constexpr double kEarthRadiusKm = 6371.0;

double haversineKm(double lat1, double lon1, double lat2, double lon2);
// Initial bearing from point 1 to point 2, degrees clockwise from north.
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
// One of eight compass directions ("Kuzey Doğu" / "North East"), as SCAP
// labels them: bearing rounded to the nearest 45 degrees.
QString compassDirection(double bearingDeg, const QString &lang);
// Distance class 1-4: <=100, <=300, <=500, >500 km.
int distanceClass(double km);

// Equirectangular projection around a reference point: kilometres east and
// north. Accurate to a fraction of a percent within a few hundred km.
class LocalProjection {
public:
  LocalProjection(double lat0, double lon0);
  QPointF toKm(double lat, double lon) const; // (east, north)
  QPointF toLatLon(const QPointF &km) const;  // (lat, lon)

private:
  double lat0_, lon0_, cosLat0_;
};

struct NearestPoint {
  double km = 0;
  double lat = 0, lon = 0;
  int segment = -1; // index of the polyline segment holding the point
};

// The closest point of a (lon, lat) polyline to (lat, lon). Empty polylines
// give km = infinity.
NearestPoint nearestOnPolyline(double lat, double lon,
                               const std::vector<QPointF> &lonLat);

} // namespace skg::analysis
