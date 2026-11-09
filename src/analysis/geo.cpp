#include "analysis/geo.h"

#include <QStringList>

#include <cmath>
#include <limits>
#include <numbers>

namespace skg::analysis {

namespace {
constexpr double kDeg = std::numbers::pi / 180.0;
}

double haversineKm(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = (lat2 - lat1) * kDeg, dlon = (lon2 - lon1) * kDeg;
  const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                   std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg) *
                       std::sin(dlon / 2) * std::sin(dlon / 2);
  return 2.0 * kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(a)));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dl = (lon2 - lon1) * kDeg;
  const double y = std::sin(dl) * std::cos(p2);
  const double x =
      std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  return std::fmod(std::atan2(y, x) / kDeg + 360.0, 360.0);
}

QString compassDirection(double bearing, const QString &lang) {
  static const QStringList tr{
      QStringLiteral("Kuzey"), QStringLiteral("Kuzey Doğu"),
      QStringLiteral("Doğu"),  QStringLiteral("Güney Doğu"),
      QStringLiteral("Güney"), QStringLiteral("Güney Batı"),
      QStringLiteral("Batı"),  QStringLiteral("Kuzey Batı")};
  static const QStringList en{
      QStringLiteral("North"), QStringLiteral("North East"),
      QStringLiteral("East"),  QStringLiteral("South East"),
      QStringLiteral("South"), QStringLiteral("South West"),
      QStringLiteral("West"),  QStringLiteral("North West")};
  if (std::isnan(bearing))
    return lang == QLatin1String("tr") ? QStringLiteral("Bilinmiyor")
                                       : QStringLiteral("Unknown");
  const int i = int(std::fmod(bearing + 22.5, 360.0) / 45.0) % 8;
  return (lang == QLatin1String("tr") ? tr : en)[i];
}

int distanceClass(double km) {
  if (km <= 100)
    return 1;
  if (km <= 300)
    return 2;
  if (km <= 500)
    return 3;
  return 4;
}

LocalProjection::LocalProjection(double lat0, double lon0)
    : lat0_(lat0), lon0_(lon0), cosLat0_(std::cos(lat0 * kDeg)) {}

QPointF LocalProjection::toKm(double lat, double lon) const {
  return {(lon - lon0_) * kDeg * kEarthRadiusKm * cosLat0_,
          (lat - lat0_) * kDeg * kEarthRadiusKm};
}

QPointF LocalProjection::toLatLon(const QPointF &km) const {
  return {lat0_ + km.y() / (kEarthRadiusKm * kDeg),
          lon0_ + km.x() / (kEarthRadiusKm * kDeg *
                            (cosLat0_ == 0 ? 1e-12 : cosLat0_))};
}

NearestPoint nearestOnPolyline(double lat, double lon,
                               const std::vector<QPointF> &lonLat) {
  NearestPoint best;
  best.km = std::numeric_limits<double>::infinity();
  if (lonLat.empty())
    return best;
  const LocalProjection proj(lat, lon);
  if (lonLat.size() == 1) {
    best.km = haversineKm(lat, lon, lonLat[0].y(), lonLat[0].x());
    best.lat = lonLat[0].y();
    best.lon = lonLat[0].x();
    best.segment = 0;
    return best;
  }
  QPointF prev = proj.toKm(lonLat[0].y(), lonLat[0].x());
  for (size_t i = 1; i < lonLat.size(); ++i) {
    const QPointF cur = proj.toKm(lonLat[i].y(), lonLat[i].x());
    const QPointF d = cur - prev;
    const double len2 = d.x() * d.x() + d.y() * d.y();
    // The query point is the origin; project it onto the segment.
    double t = len2 > 0 ? -(prev.x() * d.x() + prev.y() * d.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF p = prev + d * t;
    const double km = std::hypot(p.x(), p.y());
    if (km < best.km) {
      best.km = km;
      const QPointF ll = proj.toLatLon(p);
      best.lat = ll.x();
      best.lon = ll.y();
      best.segment = int(i) - 1;
    }
    prev = cur;
  }
  return best;
}

} // namespace skg::analysis
