// What the two maps (the station map and the regions map) share: the
// projection from (lon, lat) to device pixels, a round length for the
// scale bar, and the five-pointed star.
#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include <cmath>
#include <numbers>

namespace skg::analysis {

// (lon, lat) -> pixels, equirectangular; the caller sizes `rect` with the
// cosine of the centre latitude.
struct MapView {
  QRectF rect;   // device
  QRectF extent; // lon/lat
  QPointF toPx(double lon, double lat) const {
    return {rect.left() + (lon - extent.left()) / extent.width() * rect.width(),
            rect.bottom() -
                (lat - extent.top()) / extent.height() * rect.height()};
  }
  QPointF toPx(const QPointF &lonLat) const {
    return toPx(lonLat.x(), lonLat.y());
  }
};

// 1, 2 or 5 times a power of ten, about a quarter of the map's width.
inline double niceScaleKm(double widthKm) {
  const double target = widthKm / 4.0;
  const double mag = std::pow(10.0, std::floor(std::log10(target)));
  for (double m : {1.0, 2.0, 5.0, 10.0})
    if (m * mag >= target)
      return m * mag;
  return 10 * mag;
}

// A star around `c` whose points reach 1.5 r and whose notches 0.65 r.
inline QPainterPath starPath(const QPointF &c, double r) {
  QPainterPath star;
  for (int i = 0; i < 10; ++i) {
    const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
    const double rr = (i % 2 == 0) ? r * 1.5 : r * 0.65;
    const QPointF q(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
    if (i == 0)
      star.moveTo(q);
    else
      star.lineTo(q);
  }
  star.closeSubpath();
  return star;
}

} // namespace skg::analysis
