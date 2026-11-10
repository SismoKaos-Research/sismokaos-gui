#include "analysis/map.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "analysis/geo.h"
#include "analysis/mapview.h"

namespace skg::analysis {

namespace {
QString tr(const QString &lang, const char *trText, const char *enText) {
  return lang == QLatin1String("tr") ? QString::fromUtf8(trText)
                                     : QString::fromUtf8(enText);
}

using View = MapView;

QPolygonF circlePolygon(const View &v, double lat, double lon, double km) {
  QPolygonF poly;
  const double dLat = km / (kEarthRadiusKm * std::numbers::pi / 180.0);
  for (int i = 0; i <= 180; ++i) {
    const double a = i * 2.0 * std::numbers::pi / 180.0;
    const double la = lat + dLat * std::cos(a);
    const double lo =
        lon + dLat * std::sin(a) / std::cos(lat * std::numbers::pi / 180.0);
    poly << v.toPx(lo, la);
  }
  return poly;
}
} // namespace

QColor bandColor(int band) {
  switch (band) {
  case 0:
    return QColor(0xD6, 0x1E, 0x1E);
  case 1:
    return QColor(0xF0, 0x8C, 0x1E);
  case 2:
    return QColor(0xE6, 0xC8, 0x1E);
  case 3:
    return QColor(0x3C, 0xA0, 0x46);
  }
  return Qt::gray;
}

QRectF mapExtent(const MapSpec &spec) {
  const double lat = spec.station.lat, lon = spec.station.lon;
  double halfKm = spec.extentKm;
  if (halfKm <= 0) {
    halfKm = spec.ringsKm.empty()
                 ? 100.0
                 : *std::max_element(spec.ringsKm.begin(), spec.ringsKm.end());
    for (const EventAtStation &e : spec.events)
      halfKm = std::max(halfKm, e.km);
    halfKm += spec.paddingKm;
  }
  const double dLat = halfKm / (kEarthRadiusKm * std::numbers::pi / 180.0);
  const double dLon = dLat / std::cos(lat * std::numbers::pi / 180.0);
  return QRectF(QPointF(lon - dLon, lat - dLat),
                QPointF(lon + dLon, lat + dLat));
}

void drawMap(QPainter &p, const QRectF &rect, const MapSpec &spec) {
  const double px = std::min(rect.width(), rect.height()) / 600.0;
  QFont font = p.font();
  font.setPointSizeF(std::max(6.0, 8.0 * px * 1.3));
  p.setFont(font);
  const QFontMetricsF fm(font);
  // Keep the aspect of the extent: degrees of longitude are shorter.
  QRectF extent = mapExtent(spec);
  const double kmPerDeg = kEarthRadiusKm * std::numbers::pi / 180.0;
  const double cosLat = std::cos(spec.station.lat * std::numbers::pi / 180.0);
  double kmW = extent.width() * kmPerDeg * cosLat,
         kmH = extent.height() * kmPerDeg;
  QRectF area = rect.adjusted(44 * px, (spec.title.isEmpty() ? 10 : 30) * px,
                              -14 * px, -(2.4 * fm.height() + 10 * px));
  const double scale =
      std::min(area.width() / kmW, area.height() / kmH); // px per km
  const QSizeF size(kmW * scale, kmH * scale);
  area = QRectF(QPointF(area.center().x() - size.width() / 2,
                        area.center().y() - size.height() / 2),
                size);
  // Widen the extent to fill the frame exactly.
  kmW = area.width() / scale;
  kmH = area.height() / scale;
  const double lonSpan = kmW / kmPerDeg / cosLat, latSpan = kmH / kmPerDeg;
  extent = QRectF(QPointF(extent.center().x() - lonSpan / 2,
                          extent.center().y() - latSpan / 2),
                  QSizeF(lonSpan, latSpan));
  const View v{area, extent};

  p.fillRect(rect, Qt::white);
  p.save();
  p.setClipRect(area);
  p.fillRect(area, QColor(0xe8, 0xf4, 0xf8));
  // Provinces.
  if (spec.showProvinces && spec.provinces)
    for (const Shape &s : spec.provinces->shapes) {
      if (s.kind != Shape::Kind::Polygon || !s.bbox.intersects(extent))
        continue;
      QPainterPath path;
      for (const auto &part : s.parts) {
        QPolygonF poly;
        for (const QPointF &q : part)
          poly << v.toPx(q);
        path.addPolygon(poly);
        path.closeSubpath();
      }
      p.setPen(QPen(QColor(0x80, 0x80, 0x80), 0.6 * px));
      p.setBrush(QColor(0xf6, 0xf3, 0xea));
      p.drawPath(path);
    }
  // Faults.
  if (spec.showFaults && spec.faults) {
    p.setPen(QPen(QColor(0xB2, 0x10, 0x10), 1.4 * px));
    p.setBrush(Qt::NoBrush);
    for (const Fault *f : spec.faults->within(extent)) {
      QPolygonF poly;
      for (const QPointF &q : f->line)
        poly << v.toPx(q);
      p.drawPolyline(poly);
    }
  }
  // Rings.
  for (size_t i = 0; i < spec.ringsKm.size(); ++i) {
    const double km = spec.ringsKm[i];
    p.setPen(QPen(bandColor(int(i)), 1.2 * px, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(circlePolygon(v, spec.station.lat, spec.station.lon, km));
    const QPointF label = v.toPx(
        spec.station.lon,
        spec.station.lat + km / (kEarthRadiusKm * std::numbers::pi / 180.0));
    p.setPen(bandColor(int(i)));
    p.drawText(QRectF(label.x() - 40 * px, label.y() - fm.height() - 2 * px,
                      80 * px, fm.height()),
               Qt::AlignCenter, QStringLiteral("%1 km").arg(km));
  }
  // Events, smallest magnitudes first so the big ones stay visible.
  std::vector<const EventAtStation *> sorted;
  for (const EventAtStation &e : spec.events)
    sorted.push_back(&e);
  std::sort(sorted.begin(), sorted.end(),
            [](const EventAtStation *a, const EventAtStation *b) {
              return a->event.mag < b->event.mag;
            });
  for (const EventAtStation *e : sorted) {
    const double r = (2.0 + std::max(0.0, e->event.mag) * 1.6) * px;
    QColor c = bandColor(e->band);
    c.setAlphaF(0.75);
    p.setPen(QPen(Qt::black, 0.5 * px));
    p.setBrush(c);
    p.drawEllipse(v.toPx(e->event.lon, e->event.lat), r, r);
  }
  // Main event: a star and a label.
  if (spec.mainEvent) {
    const QPointF c =
        v.toPx(spec.mainEvent->event.lon, spec.mainEvent->event.lat);
    const double r = 9 * px;
    const QPainterPath star = starPath(c, r);
    p.setPen(QPen(Qt::black, 0.8 * px));
    p.setBrush(QColor(0xFF, 0xD7, 0x00));
    p.drawPath(star);
    QFont bf = font;
    bf.setBold(true);
    p.setFont(bf);
    const QString label =
        QStringLiteral("M%1 %2")
            .arg(spec.mainEvent->event.mag, 0, 'f', 1, QLatin1Char(' '))
            .arg(spec.mainEvent->event.place);
    p.setPen(Qt::black);
    p.drawText(
        QRectF(c.x() + r * 1.6, c.y() - fm.height() / 2, 200 * px, fm.height()),
        Qt::AlignVCenter, label);
    p.setFont(font);
  }
  // Station.
  {
    const QPointF c = v.toPx(spec.station.lon, spec.station.lat);
    QPolygonF tri;
    const double r = 7 * px;
    tri << QPointF(c.x(), c.y() - r) << QPointF(c.x() + r, c.y() + r * 0.8)
        << QPointF(c.x() - r, c.y() + r * 0.8);
    p.setPen(QPen(Qt::black, 0.8 * px));
    p.setBrush(QColor(0x1E, 0x50, 0xA0));
    p.drawPolygon(tri);
    QFont bf = font;
    bf.setBold(true);
    p.setFont(bf);
    p.drawText(
        QRectF(c.x() + r * 1.3, c.y() - fm.height() / 2, 120 * px, fm.height()),
        Qt::AlignVCenter, spec.station.code);
    p.setFont(font);
  }
  // Graticule.
  if (spec.showGraticule) {
    p.setPen(QPen(QColor(0x50, 0x50, 0x50, 90), 0.5 * px, Qt::DotLine));
    const double stepLon =
                     std::max(0.25, std::round(extent.width() / 5 * 4) / 4),
                 stepLat =
                     std::max(0.25, std::round(extent.height() / 5 * 4) / 4);
    for (double lon = std::ceil(extent.left() / stepLon) * stepLon;
         lon < extent.right(); lon += stepLon)
      p.drawLine(v.toPx(lon, extent.top()), v.toPx(lon, extent.bottom()));
    for (double lat = std::ceil(extent.top() / stepLat) * stepLat;
         lat < extent.bottom(); lat += stepLat)
      p.drawLine(v.toPx(extent.left(), lat), v.toPx(extent.right(), lat));
  }
  p.restore();
  // Frame and tick labels.
  p.setPen(QPen(Qt::black, 0.9 * px));
  p.setBrush(Qt::NoBrush);
  p.drawRect(area);
  if (spec.showGraticule) {
    const double stepLon =
                     std::max(0.25, std::round(extent.width() / 5 * 4) / 4),
                 stepLat =
                     std::max(0.25, std::round(extent.height() / 5 * 4) / 4);
    for (double lon = std::ceil(extent.left() / stepLon) * stepLon;
         lon < extent.right(); lon += stepLon)
      p.drawText(QRectF(v.toPx(lon, extent.top()).x() - 30 * px,
                        area.bottom() + 2 * px, 60 * px, fm.height()),
                 Qt::AlignCenter, QStringLiteral("%1°E").arg(lon, 0, 'f', 2));
    for (double lat = std::ceil(extent.top() / stepLat) * stepLat;
         lat < extent.bottom(); lat += stepLat)
      p.drawText(QRectF(rect.left(),
                        v.toPx(extent.left(), lat).y() - fm.height() / 2,
                        area.left() - rect.left() - 3 * px, fm.height()),
                 Qt::AlignRight | Qt::AlignVCenter,
                 QStringLiteral("%1°N").arg(lat, 0, 'f', 2));
  }
  // Scale bar, bottom left inside the frame.
  {
    const double kmPerPx = kmW / area.width();
    const double km = niceScaleKm(kmW);
    const double w = km / kmPerPx;
    const QPointF a(area.left() + 12 * px, area.bottom() - 14 * px);
    p.setPen(QPen(Qt::black, 1.6 * px));
    p.drawLine(a, QPointF(a.x() + w, a.y()));
    p.drawLine(a, QPointF(a.x(), a.y() - 4 * px));
    p.drawLine(QPointF(a.x() + w, a.y()), QPointF(a.x() + w, a.y() - 4 * px));
    p.drawText(QRectF(a.x(), a.y() - fm.height() - 5 * px, w, fm.height()),
               Qt::AlignCenter, QStringLiteral("%1 km").arg(km));
  }
  // North arrow, top right inside the frame.
  {
    const QPointF c(area.right() - 20 * px, area.top() + 26 * px);
    QPolygonF arrow;
    arrow << QPointF(c.x(), c.y() - 14 * px)
          << QPointF(c.x() + 6 * px, c.y() + 8 * px)
          << QPointF(c.x(), c.y() + 3 * px)
          << QPointF(c.x() - 6 * px, c.y() + 8 * px);
    p.setPen(QPen(Qt::black, 0.8 * px));
    p.setBrush(Qt::black);
    p.drawPolygon(arrow);
    p.drawText(QRectF(c.x() - 10 * px, c.y() + 9 * px, 20 * px, fm.height()),
               Qt::AlignCenter, QStringLiteral("N"));
  }
  // Legend of the bands, bottom right inside the frame.
  {
    const QStringList rows{"0-100 km", "100-300 km (M3+)", "300-500 km (M5+)",
                           "500-1000 km (M6+)"};
    double w = 0;
    for (const QString &r : rows)
      w = std::max(w, fm.horizontalAdvance(r));
    const double lh = fm.height() * 1.1;
    const QRectF box(area.right() - w - 26 * px,
                     area.bottom() - 4 * lh - 10 * px, w + 22 * px,
                     4 * lh + 6 * px);
    p.setPen(QPen(QColor(0xaa, 0xaa, 0xaa), 0.6 * px));
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawRect(box);
    for (int i = 0; i < 4; ++i) {
      p.setPen(QPen(Qt::black, 0.5 * px));
      p.setBrush(bandColor(i));
      p.drawEllipse(
          QPointF(box.left() + 9 * px, box.top() + 3 * px + i * lh + lh / 2),
          4 * px, 4 * px);
      p.setPen(Qt::black);
      p.drawText(QRectF(box.left() + 17 * px, box.top() + 3 * px + i * lh,
                        w + 4 * px, lh),
                 Qt::AlignVCenter, rows[i]);
    }
  }
  // Axis titles and the figure title.
  p.setPen(Qt::black);
  p.drawText(QRectF(area.left(), area.bottom() + fm.height() + 4 * px,
                    area.width(), fm.height()),
             Qt::AlignCenter, tr(spec.lang, "Boylam", "Longitude"));
  p.save();
  p.translate(rect.left() + fm.height() * 0.8, area.center().y());
  p.rotate(-90);
  p.drawText(
      QRectF(-area.height() / 2, -fm.height() / 2, area.height(), fm.height()),
      Qt::AlignCenter, tr(spec.lang, "Enlem", "Latitude"));
  p.restore();
  if (!spec.title.isEmpty()) {
    QFont tf = font;
    tf.setBold(true);
    tf.setPointSizeF(font.pointSizeF() * 1.2);
    p.setFont(tf);
    p.drawText(QRectF(rect.left(), rect.top() + 4 * px, rect.width(),
                      area.top() - rect.top() - 6 * px),
               Qt::AlignHCenter | Qt::AlignBottom, spec.title);
    p.setFont(font);
  }
}

} // namespace skg::analysis
