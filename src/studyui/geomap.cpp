#include "studyui/geomap.h"

#include <QFontMetricsF>
#include <QHelpEvent>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

#include "analysis/mapview.h"
#include "analysis/regionmap.h"
#include "core/theme.h"
#include "core/ui.h"

namespace skg {

namespace {

constexpr double kKmPerDegLat = 110.574;
constexpr double kKmPerDegLon = 111.320; // at the equator
constexpr double kMinWidthKm = 8.0;
constexpr double kMaxWidthKm = 9000.0;

double cosDeg(double deg) { return std::cos(deg * std::numbers::pi / 180.0); }

QColor mix(const QColor &a, const QColor &b, double t) {
  return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                          float(a.greenF() + (b.greenF() - a.greenF()) * t),
                          float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QString degLabel(double v, double step, bool lon) {
  const int dec = step < 0.1 ? 2 : step < 1 ? 1 : 0;
  const QString hemi = lon ? (v < 0 ? QStringLiteral("W") : QStringLiteral("E"))
                           : (v < 0 ? QStringLiteral("S") : QStringLiteral("N"));
  return QStringLiteral("%1°%2").arg(std::abs(v), 0, 'f', dec).arg(hemi);
}

double markerRadius(const GeoMarker &m) {
  switch (m.kind) {
  case GeoMarker::Kind::Earthquake:
    return GeoMap::quakeRadius(m.magnitude);
  case GeoMarker::Kind::Reference:
    return 7.0;
  case GeoMarker::Kind::Station:
    return 6.5;
  case GeoMarker::Kind::Dot:
    return 2.2;
  }
  return 5.0;
}

QPolygonF triangle(const QPointF &q, double r) {
  return QPolygonF({q + QPointF(0, -r * 1.08), q + QPointF(r, r * 0.77),
                    q + QPointF(-r, r * 0.77)});
}

} // namespace

double GeoMap::quakeRadius(double magnitude) {
  if (!(magnitude > 0)) // also NaN
    return 6.0;
  return std::clamp(4.0 + (magnitude - 4.0) * 3.2, 4.0, 17.0);
}

void GeoMap::drawGlyph(QPainter &p, const GeoMarker &m, const QPointF &at,
                       double r, const Theme &theme) {
  const QColor halo = theme.color(QStringLiteral("surface2"));
  const QColor muted = theme.color(QStringLiteral("muted"));
  QColor ink = m.color.isValid() ? m.color : theme.color(QStringLiteral("text"));
  if (m.dim) {
    ink = muted;
    ink.setAlphaF(0.75f);
  }
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  switch (m.kind) {
  case GeoMarker::Kind::Earthquake:
    if (m.hollow) {
      p.setPen(QPen(halo, 3.6));
      p.setBrush(Qt::NoBrush);
      p.drawEllipse(at, r, r);
      p.setPen(QPen(ink, 1.5));
      p.drawEllipse(at, r, r);
    } else {
      p.setPen(QPen(halo, 2));
      p.setBrush(ink);
      p.drawEllipse(at, r, r);
    }
    break;
  case GeoMarker::Kind::Reference: {
    const QPolygonF d({at + QPointF(0, -r), at + QPointF(r, 0),
                       at + QPointF(0, r), at + QPointF(-r, 0)});
    p.setPen(QPen(halo, 4.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(halo);
    p.drawPolygon(d);
    p.setPen(QPen(ink, 2.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.drawPolygon(d);
    break;
  }
  case GeoMarker::Kind::Station: {
    const QPolygonF t = triangle(at, r);
    p.setPen(QPen(halo, 3.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(halo);
    p.drawPolygon(t);
    switch (m.state) {
    case GeoMarker::State::Used:
      p.setPen(Qt::NoPen);
      p.setBrush(ink);
      p.drawPolygon(t);
      break;
    case GeoMarker::State::Candidate:
      p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.setBrush(halo);
      p.drawPolygon(t);
      break;
    case GeoMarker::State::HasData:
      p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.setBrush(halo);
      p.drawPolygon(t);
      p.setPen(Qt::NoPen);
      p.setBrush(ink);
      p.drawPolygon(triangle(at + QPointF(0, r * 0.12), r * 0.45));
      break;
    case GeoMarker::State::NoData:
      p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.setBrush(halo);
      p.drawPolygon(t);
      p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(at + QPointF(-r * 1.1, r * 0.95),
                 at + QPointF(r * 1.1, -r * 0.95));
      break;
    case GeoMarker::State::Outside: {
      const QColor text = theme.color(QStringLiteral("text"));
      p.setPen(QPen(m.dim ? ink : text, 1.3, Qt::SolidLine, Qt::SquareCap,
                    Qt::MiterJoin));
      p.setBrush(theme.statusColor(QStringLiteral("warning")));
      p.drawPolygon(t);
      p.setPen(QPen(QColor(QStringLiteral("#1B2430")), 1.6, Qt::SolidLine,
                    Qt::RoundCap));
      p.drawLine(at + QPointF(0, -r * 0.35), at + QPointF(0, r * 0.2));
      p.drawPoint(at + QPointF(0, r * 0.52));
      break;
    }
    }
    break;
  }
  case GeoMarker::Kind::Dot: {
    QColor c = m.color.isValid() ? m.color : muted;
    c.setAlphaF(0.6f);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(at, r, r);
    break;
  }
  }
  p.restore();
}

GeoMap::GeoMap(Theme *theme, QWidget *parent) : QWidget(parent), theme_(theme) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(true);
  setFocusPolicy(Qt::ClickFocus);
  connect(theme_, &Theme::changed, this, [this] {
    invalidate();
    update();
  });
  setView(39.0, 35.5, 1900.0);
  moved_ = false;
}

void GeoMap::invalidate() {
  baseDirty_ = true;
  placedDirty_ = true;
}

void GeoMap::setContent(const GeoContent &content, bool refit) {
  content_ = content;
  if (refit)
    moved_ = false;
  if (!moved_)
    applyFit();
  invalidate();
  update();
}

void GeoMap::setPlaceholder(const QString &text) {
  placeholder_ = text;
  update();
}

void GeoMap::fit(bool strongOnly) {
  fitStrong_ = strongOnly;
  moved_ = false;
  applyFit();
  invalidate();
  update();
}

void GeoMap::setView(double lat, double lon, double widthKm) {
  lat0_ = std::clamp(lat, -85.0, 85.0);
  lon0_ = lon;
  const double w = std::max(1, width());
  pxPerKm_ = w / std::clamp(widthKm, kMinWidthKm, kMaxWidthKm);
  moved_ = true;
  invalidate();
  update();
}

double GeoMap::widthKm() const { return std::max(1, width()) / pxPerKm_; }

void GeoMap::applyFit() {
  double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
  const auto grow = [&](double lat, double lon, double km) {
    const double dLat = km / kKmPerDegLat;
    const double dLon = km / (kKmPerDegLon * std::max(0.2, cosDeg(lat)));
    minLat = std::min(minLat, lat - dLat);
    maxLat = std::max(maxLat, lat + dLat);
    minLon = std::min(minLon, lon - dLon);
    maxLon = std::max(maxLon, lon + dLon);
  };
  bool anyStrong = false;
  for (const GeoMarker &m : content_.markers)
    anyStrong = anyStrong || m.strong;
  const bool strong = fitStrong_ && anyStrong;
  for (const GeoMarker &m : content_.markers) {
    if (m.kind == GeoMarker::Kind::Dot)
      continue;
    if (strong && !m.strong)
      continue;
    grow(m.lat, m.lon, 0);
  }
  for (const GeoZone &z : content_.zones)
    if (!strong || !z.dotted)
      grow(z.lat, z.lon, z.radiusKm);
  for (const GeoZone &z : content_.coverage)
    grow(z.lat, z.lon, z.radiusKm);
  if (minLat > maxLat) {
    // Nothing to show yet: Turkey and its neighbours.
    lat0_ = 39.0;
    lon0_ = 35.5;
    pxPerKm_ = std::max(1, width()) / 1900.0;
    return;
  }
  lat0_ = (minLat + maxLat) / 2;
  lon0_ = (minLon + maxLon) / 2;
  const double wKm =
      std::max(40.0, (maxLon - minLon) * kKmPerDegLon * cosDeg(lat0_));
  const double hKm = std::max(40.0, (maxLat - minLat) * kKmPerDegLat);
  const double w = std::max(1, width()), h = std::max(1, height());
  // Room for the labels beside the outermost markers.
  pxPerKm_ = std::min((w - 110) / wKm, (h - 70) / hKm);
  if (!(pxPerKm_ > 0))
    pxPerKm_ = std::min(w / wKm, h / hKm) * 0.7;
  pxPerKm_ = std::clamp(pxPerKm_, w / kMaxWidthKm, w / kMinWidthKm);
}

QPointF GeoMap::toPx(double lat, double lon) const {
  const double kx = kKmPerDegLon * cosDeg(lat0_);
  return {width() / 2.0 + (lon - lon0_) * kx * pxPerKm_,
          height() / 2.0 - (lat - lat0_) * kKmPerDegLat * pxPerKm_};
}

QPointF GeoMap::toLatLon(const QPointF &px) const {
  const double kx = kKmPerDegLon * cosDeg(lat0_);
  return {lat0_ - (px.y() - height() / 2.0) / (kKmPerDegLat * pxPerKm_),
          lon0_ + (px.x() - width() / 2.0) / (kx * pxPerKm_)};
}

QColor GeoMap::ink(const GeoMarker &m) const {
  if (m.dim)
    return theme_->color(QStringLiteral("muted"));
  return m.color.isValid() ? m.color : theme_->color(QStringLiteral("text"));
}

// Where every marker and label goes. Markers of cases that would hide each
// other are moved apart; labels take the first free place around their
// marker, nearest first.
const std::vector<GeoMap::Placed> &GeoMap::placed() const {
  if (!placedDirty_)
    return placed_;
  placedDirty_ = false;
  const int n = int(content_.markers.size());
  placed_.assign(static_cast<size_t>(n), Placed{});
  const QRectF bounds = QRectF(rect()).adjusted(4, 4, -4, -4);
  const QRectF seen = QRectF(rect()).adjusted(-30, -30, 30, 30);

  std::vector<int> order; // strong, then plain, then dim
  for (int pass = 0; pass < 3; ++pass)
    for (int i = 0; i < n; ++i) {
      const GeoMarker &m = content_.markers[i];
      const int cls = m.strong ? 0 : m.dim ? 2 : 1;
      if (cls == pass)
        order.push_back(i);
    }

  std::vector<int> fixedCases;
  for (int i : order) {
    const GeoMarker &m = content_.markers[i];
    Placed &pl = placed_[static_cast<size_t>(i)];
    pl.truth = pl.at = toPx(m.lat, m.lon);
    pl.r = markerRadius(m);
    pl.hidden = m.showBelowKm > 0 && widthKm() > m.showBelowKm;
    if (pl.hidden)
      continue;
    // Only markers that stand for something named (a case) are moved off
    // each other; the anonymous earthquakes of a catalog stay where they
    // are, however many there are.
    const bool isCase = (m.kind == GeoMarker::Kind::Earthquake ||
                         m.kind == GeoMarker::Kind::Reference) &&
                        (m.strong || !m.label.isEmpty());
    if (!isCase || !seen.contains(pl.truth))
      continue;
    const auto clashes = [&](const QPointF &q) {
      for (int j : fixedCases) {
        const Placed &o = placed_[static_cast<size_t>(j)];
        if (QLineF(q, o.at).length() < (pl.r + o.r) * 0.85)
          return true;
      }
      return false;
    };
    if (clashes(pl.at)) {
      bool done = false;
      for (double ring = 1.0; ring < 3.1 && !done; ring += 1.0)
        for (double deg : {0.0, -45.0, 45.0, -90.0, 90.0, -135.0, 135.0, 180.0}) {
          double reach = 0;
          for (int j : fixedCases)
            reach = std::max(reach, placed_[static_cast<size_t>(j)].r);
          const double d = (pl.r + reach + 5.0) * ring;
          const double a = deg * std::numbers::pi / 180.0;
          const QPointF q = pl.truth + QPointF(std::cos(a) * d, std::sin(a) * d);
          if (!clashes(q)) {
            pl.at = q;
            done = true;
            break;
          }
        }
    }
    fixedCases.push_back(i);
  }

  // Obstacles: every marker that is not a faint dot.
  std::vector<QRectF> markerBoxes(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const GeoMarker &m = content_.markers[i];
    const Placed &pl = placed_[static_cast<size_t>(i)];
    if (m.kind == GeoMarker::Kind::Dot || pl.hidden || !seen.contains(pl.at))
      continue;
    markerBoxes[static_cast<size_t>(i)] =
        QRectF(pl.at.x() - pl.r - 1, pl.at.y() - pl.r - 1, 2 * pl.r + 2,
               2 * pl.r + 2);
  }
  std::vector<QRectF> labels;
  QFont plain = font();
  QFont bold = font();
  bold.setBold(true);
  static const QPointF dirs[] = {{1, 0}, {-1, 0}, {0, -1}, {0, 1},
                                 {1, -1}, {1, 1}, {-1, -1}, {-1, 1}};
  for (int i : order) {
    const GeoMarker &m = content_.markers[i];
    Placed &pl = placed_[static_cast<size_t>(i)];
    if (m.label.isEmpty() || pl.hidden || !QRectF(rect()).contains(pl.at))
      continue;
    const QFontMetricsF fm(m.strong ? bold : plain);
    const QSizeF size(fm.horizontalAdvance(m.label) + 6, fm.height() + 2);
    const auto rectFor = [&](const QPointF &dir, double gap) {
      const bool diag = dir.x() != 0 && dir.y() != 0;
      const double g = diag ? gap * 0.72 : gap;
      double x = pl.at.x() - size.width() / 2;
      double y = pl.at.y() - size.height() / 2;
      if (dir.x() > 0)
        x = pl.at.x() + g;
      else if (dir.x() < 0)
        x = pl.at.x() - g - size.width();
      if (dir.y() > 0)
        y = pl.at.y() + g;
      else if (dir.y() < 0)
        y = pl.at.y() - g - size.height();
      return QRectF(QPointF(x, y), size);
    };
    const auto free = [&](const QRectF &r) {
      if (!bounds.contains(r))
        return false;
      for (const QRectF &o : labels)
        if (o.intersects(r))
          return false;
      for (int j = 0; j < n; ++j)
        if (j != i && !markerBoxes[static_cast<size_t>(j)].isEmpty() &&
            markerBoxes[static_cast<size_t>(j)].intersects(r))
          return false;
      return true;
    };
    bool done = false;
    int ring = 0;
    for (double extra : {3.0, 16.0, 30.0, 46.0}) {
      for (const QPointF &dir : dirs) {
        const QRectF r = rectFor(dir, pl.r + extra);
        if (free(r)) {
          pl.label = r;
          pl.leader = ring > 0;
          done = true;
          break;
        }
      }
      if (done)
        break;
      ++ring;
    }
    if (!done && m.strong) {
      // No free place: beside the marker, kept inside the map.
      QRectF r = rectFor(dirs[0], pl.r + 3.0);
      r.moveLeft(std::clamp(r.left(), bounds.left(),
                            std::max(bounds.left(), bounds.right() - r.width())));
      r.moveTop(std::clamp(r.top(), bounds.top(),
                           std::max(bounds.top(), bounds.bottom() - r.height())));
      pl.label = r;
      done = true;
    }
    if (done)
      labels.push_back(pl.label);
  }
  return placed_;
}

QList<QRectF> GeoMap::labelRects() const {
  QList<QRectF> out;
  for (const Placed &pl : placed())
    if (!pl.label.isEmpty())
      out << pl.label;
  return out;
}

QPointF GeoMap::markerPx(int index) const {
  const auto &pl = placed();
  if (index < 0 || index >= int(pl.size()))
    return {};
  return pl[static_cast<size_t>(index)].at;
}

int GeoMap::hit(const QPointF &px) const {
  const auto &pl = placed();
  int best = -1;
  double bestScore = 1e18;
  for (int i = 0; i < int(pl.size()); ++i) {
    const GeoMarker &m = content_.markers[i];
    const Placed &q = pl[static_cast<size_t>(i)];
    if (q.hidden)
      continue;
    const double d = QLineF(px, q.at).length();
    const bool dot = m.kind == GeoMarker::Kind::Dot;
    if (d > std::max(q.r, 5.0) + 3.0)
      continue;
    // A real marker wins over a faint dot under the same pointer.
    const double score = d + (dot ? 1000.0 : 0.0) - (m.strong ? 0.5 : 0.0);
    if (score < bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

QString GeoMap::markerAt(const QPointF &px) const {
  const int i = hit(px);
  return i < 0 ? QString() : content_.markers[i].id;
}

void GeoMap::renderBase() {
  baseDirty_ = false;
  const qreal dpr = devicePixelRatioF();
  base_ = QPixmap(size() * dpr);
  base_.setDevicePixelRatio(dpr);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
  const QColor sea = col("surface2");
  base_.fill(sea);
  QPainter p(&base_);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r(rect());
  const QPointF tl = toLatLon(r.topLeft()), br = toLatLon(r.bottomRight());
  const double north = tl.x(), west = tl.y(), south = br.x(), east = br.y();

  // Land.
  try {
    const analysis::ShapeLayer &land = analysis::shippedCountries();
    const QColor fill = mix(sea, col("border"), 0.42);
    p.setPen(QPen(col("border"), 1.0));
    p.setBrush(fill);
    for (const analysis::Shape &s : land.shapes) {
      if (s.bbox.right() < west || s.bbox.left() > east ||
          s.bbox.bottom() < south || s.bbox.top() > north)
        continue;
      QPainterPath path;
      path.setFillRule(Qt::OddEvenFill);
      for (const auto &part : s.parts) {
        if (part.size() < 3)
          continue;
        QPolygonF poly;
        poly.reserve(qsizetype(part.size()));
        for (const QPointF &ll : part)
          poly << toPx(ll.y(), ll.x());
        path.addPolygon(poly);
        path.closeSubpath();
      }
      p.drawPath(path);
    }
  } catch (const std::exception &) {
    // No outline: the graticule still places the content.
  }

  // Graticule.
  const double across = east - west;
  double step = 20;
  for (double s : {0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0})
    if (across / s <= 7.5) {
      step = s;
      break;
    }
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.82);
  p.setFont(small);
  const QFontMetricsF fm(small);
  QColor line = col("grid");
  QColor labelCol = col("muted");
  const QColor haloC = [&] {
    QColor c = sea;
    c.setAlphaF(0.8f);
    return c;
  }();
  const auto tag = [&](const QRectF &box, const QString &text) {
    p.setPen(Qt::NoPen);
    p.setBrush(haloC);
    p.drawRoundedRect(box.adjusted(-2, 0, 2, 0), 2, 2);
    p.setPen(labelCol);
    p.drawText(box, Qt::AlignCenter, text);
  };
  for (double lon = std::ceil(west / step) * step; lon <= east; lon += step) {
    const double x = toPx(lat0_, lon).x();
    p.setPen(QPen(line, 1));
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    const QString text = degLabel(lon, step, true);
    const double w = fm.horizontalAdvance(text);
    if (x - w / 2 > r.left() + 4 && x + w / 2 < r.right() - 44)
      tag(QRectF(x - w / 2, r.top() + 5, w, fm.height()), text);
  }
  for (double lat = std::ceil(south / step) * step; lat <= north; lat += step) {
    const double y = toPx(lat, lon0_).y();
    p.setPen(QPen(line, 1));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    const QString text = degLabel(lat, step, false);
    const double w = fm.horizontalAdvance(text);
    if (y - fm.height() / 2 > r.top() + 22 &&
        y + fm.height() / 2 < r.bottom() - 30)
      tag(QRectF(r.right() - w - 8, y - fm.height() / 2, w, fm.height()), text);
  }

  // The shipped fault layer: a muted backdrop.
  faultNote_.clear();
  try {
    const cbi::FaultZone &faults = cbi::shippedFaults();
    // Recessive: a thin, pale line that the markers read over.
    QColor fc = mix(col("muted"), QColor(QStringLiteral("#A07A55")), 0.6);
    fc.setAlphaF(0.5f);
    p.setPen(QPen(fc, 1.0, Qt::SolidLine, Qt::RoundCap));
    for (const cbi::FaultZone::Segment &s : faults.segments) {
      if (std::max(s.lon0, s.lon1) < west || std::min(s.lon0, s.lon1) > east ||
          std::max(s.lat0, s.lat1) < south || std::min(s.lat0, s.lat1) > north)
        continue;
      p.drawLine(toPx(s.lat0, s.lon0), toPx(s.lat1, s.lon1));
    }
  } catch (const std::exception &e) {
    faultNote_ = QString::fromUtf8(e.what());
  }

  // What a set of discs covers, as one area.
  if (!content_.coverage.isEmpty()) {
    const QColor cc = content_.coverageColor.isValid() ? content_.coverageColor
                                                       : col("accent");
    const auto ellipse = [&](QPainter &q, const GeoZone &z) {
      const double ry = z.radiusKm * pxPerKm_;
      const double rx = ry * cosDeg(lat0_) / std::max(0.2, cosDeg(z.lat));
      q.drawEllipse(toPx(z.lat, z.lon), rx, ry);
    };
    QImage area(base_.size(), QImage::Format_ARGB32_Premultiplied);
    area.setDevicePixelRatio(dpr);
    area.fill(Qt::transparent);
    QImage edge = area;
    {
      QPainter q(&area);
      q.setRenderHint(QPainter::Antialiasing);
      q.setPen(Qt::NoPen);
      q.setBrush(cc);
      for (const GeoZone &z : content_.coverage)
        ellipse(q, z);
    }
    {
      QPainter q(&edge);
      q.setRenderHint(QPainter::Antialiasing);
      q.setBrush(Qt::NoBrush);
      q.setPen(QPen(cc, 3.0));
      for (const GeoZone &z : content_.coverage)
        ellipse(q, z);
      // Keep only the outer rim: the outline of the union.
      q.setCompositionMode(QPainter::CompositionMode_DestinationOut);
      q.setPen(Qt::NoPen);
      q.setBrush(Qt::black);
      for (const GeoZone &z : content_.coverage)
        ellipse(q, z);
    }
    p.setOpacity(0.13);
    p.drawImage(QPointF(0, 0), area);
    p.setOpacity(0.9);
    p.drawImage(QPointF(0, 0), edge);
    p.setOpacity(1.0);
  }
}

void GeoMap::paintEvent(QPaintEvent *) {
  if (baseDirty_ || base_.size() != size() * devicePixelRatioF())
    renderBase();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
  const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath frame;
  frame.addRoundedRect(r, 8, 8);
  p.setClipPath(frame);
  p.drawPixmap(0, 0, base_);

  // Traces a zone is measured from.
  for (const GeoTraces &t : content_.traces) {
    p.setPen(QPen(t.color.isValid() ? t.color : col("text"), 1.9, Qt::SolidLine,
                  Qt::RoundCap));
    for (const cbi::FaultZone::Segment &s : t.segments) {
      const QPointF a = toPx(s.lat0, s.lon0), b = toPx(s.lat1, s.lon1);
      if (QRectF(rect()).adjusted(-40, -40, 40, 40).intersects(
              QRectF(a, b).normalized().adjusted(-1, -1, 1, 1)))
        p.drawLine(a, b);
    }
  }
  // Zones.
  for (const GeoZone &z : content_.zones) {
    QColor c = z.color.isValid() ? z.color : col("text");
    if (z.dotted)
      c = col("muted");
    const double ry = z.radiusKm * pxPerKm_;
    const double rx = ry * cosDeg(lat0_) / std::max(0.2, cosDeg(z.lat));
    QColor fill = c;
    fill.setAlphaF(z.filled && !z.dotted ? 0.09f : 0.0f);
    p.setBrush(fill);
    p.setPen(z.dotted ? QPen(c, 1.0, Qt::DotLine) : QPen(c, 1.5, Qt::DashLine));
    p.drawEllipse(toPx(z.lat, z.lon), rx, ry);
  }

  // Markers: faint dots, context, the rest, then the strong ones.
  const auto &pl = placed();
  const QRectF seen = QRectF(rect()).adjusted(-20, -20, 20, 20);
  for (int pass = 0; pass < 4; ++pass)
    for (int i = 0; i < int(pl.size()); ++i) {
      const GeoMarker &m = content_.markers[i];
      const int cls = m.kind == GeoMarker::Kind::Dot ? 0
                      : m.dim                         ? 1
                      : m.strong                      ? 3
                                                      : 2;
      const Placed &q = pl[static_cast<size_t>(i)];
      if (cls != pass || q.hidden || !seen.contains(q.at))
        continue;
      if (q.at != q.truth) {
        p.setPen(QPen(ink(m), 1.0));
        p.drawLine(q.truth, q.at);
        p.setBrush(ink(m));
        p.drawEllipse(q.truth, 1.6, 1.6);
      }
      drawGlyph(p, m, q.at, q.r, *theme_);
    }
  // Labels, over a halo so that they read on any background.
  QFont plain = font();
  QFont bold = font();
  bold.setBold(true);
  QColor halo = col("surface2");
  halo.setAlphaF(0.86f);
  for (int i = 0; i < int(pl.size()); ++i) {
    const Placed &q = pl[static_cast<size_t>(i)];
    if (q.label.isEmpty())
      continue;
    const GeoMarker &m = content_.markers[i];
    if (q.leader) {
      // From the marker's rim to the nearest point of the label.
      const QPointF to(std::clamp(q.at.x(), q.label.left(), q.label.right()),
                       std::clamp(q.at.y(), q.label.top(), q.label.bottom()));
      p.setPen(QPen(col("muted"), 1.0));
      p.drawLine(q.at, to);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawRoundedRect(q.label, 3, 3);
    p.setFont(m.strong ? bold : plain);
    p.setPen(m.dim ? col("muted") : col("text"));
    p.drawText(q.label, Qt::AlignCenter, m.label);
  }

  // Scale bar.
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.82);
  p.setFont(small);
  const QFontMetricsF fm(small);
  {
    const double km = analysis::niceScaleKm(widthKm());
    const double len = km * pxPerKm_;
    const QPointF b(r.right() - 14, r.bottom() - 12);
    const QString text = tr("%1 km").arg(km, 0, 'f', km < 1 ? 1 : 0);
    const double tw = fm.horizontalAdvance(text);
    QRectF box(b.x() - std::max(len, tw) - 6, b.y() - fm.height() - 8,
               std::max(len, tw) + 12, fm.height() + 14);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawRoundedRect(box, 3, 3);
    p.setPen(QPen(col("text"), 1.4));
    p.drawLine(b, b - QPointF(len, 0));
    p.drawLine(b, b - QPointF(0, 4));
    p.drawLine(b - QPointF(len, 0), b - QPointF(len, 4));
    p.drawText(QRectF(b.x() - len, b.y() - fm.height() - 5, len, fm.height()),
               Qt::AlignCenter | Qt::TextDontClip, text);
  }
  // The attribution the fault layer's licence asks for, or that it is
  // missing.
  {
    const bool bad = !faultNote_.isEmpty();
    const QString text =
        bad ? tr("The fault layer is missing; hover for details")
            : tr("Fault sources: EFSM20 (hazard model), CC BY 4.0");
    const double room = r.width() - 150;
    if (room > 80) {
      const QString shown = fm.elidedText(text, Qt::ElideRight, room);
      const QRectF tb(r.left() + 8, r.bottom() - fm.height() - 7,
                      fm.horizontalAdvance(shown) + 6, fm.height() + 2);
      p.setPen(Qt::NoPen);
      p.setBrush(halo);
      p.drawRoundedRect(tb, 3, 3);
      p.setPen(bad ? col("text") : col("muted"));
      p.drawText(tb, Qt::AlignCenter, shown);
    }
  }
  if (content_.markers.isEmpty() && content_.zones.isEmpty() &&
      content_.coverage.isEmpty() && !placeholder_.isEmpty()) {
    p.setFont(font());
    const QFontMetricsF f2(font());
    const QRectF box = f2.boundingRect(r.adjusted(24, 24, -24, -24),
                                       Qt::AlignCenter | Qt::TextWordWrap,
                                       placeholder_)
                           .adjusted(-10, -6, 10, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawRoundedRect(box, 6, 6);
    p.setPen(col("muted"));
    p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
  }

  p.setClipping(false);
  p.setPen(QPen(col("border"), 1));
  p.setBrush(Qt::NoBrush);
  p.drawPath(frame);
}

void GeoMap::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  if (!moved_)
    applyFit();
  else if (e->oldSize().width() > 0)
    // Keep the same ground in view when the widget changes width.
    pxPerKm_ *= double(std::max(1, width())) / e->oldSize().width();
  invalidate();
}

void GeoMap::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton)
    return;
  pressed_ = true;
  dragging_ = false;
  pressAt_ = lastAt_ = e->position();
}

void GeoMap::mouseMoveEvent(QMouseEvent *e) {
  if (pressed_) {
    if (!dragging_ && QLineF(e->position(), pressAt_).length() > 4) {
      dragging_ = true;
      setCursor(Qt::ClosedHandCursor);
    }
    if (dragging_) {
      const QPointF d = e->position() - lastAt_;
      lastAt_ = e->position();
      const double kx = kKmPerDegLon * cosDeg(lat0_);
      lon0_ -= d.x() / (kx * pxPerKm_);
      lat0_ = std::clamp(lat0_ + d.y() / (kKmPerDegLat * pxPerKm_), -85.0, 85.0);
      moved_ = true;
      invalidate();
      update();
    }
    return;
  }
  const int i = hit(e->position());
  setCursor(i >= 0 && !content_.markers[i].id.isEmpty() ? Qt::PointingHandCursor
                                                        : Qt::ArrowCursor);
}

void GeoMap::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton || !pressed_)
    return;
  pressed_ = false;
  if (dragging_) {
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
    return;
  }
  const QString id = markerAt(e->position());
  if (!id.isEmpty())
    emit markerClicked(id);
}

void GeoMap::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton && markerAt(e->position()).isEmpty())
    fit(fitStrong_);
}

void GeoMap::wheelEvent(QWheelEvent *e) {
  const double steps = e->angleDelta().y() / 120.0;
  if (steps == 0) {
    e->ignore();
    return;
  }
  // The place under the pointer stays under the pointer.
  const QPointF at = e->position();
  const QPointF before = toLatLon(at);
  const double w = std::max(1, width());
  pxPerKm_ = std::clamp(pxPerKm_ * std::pow(1.25, steps), w / kMaxWidthKm,
                        w / kMinWidthKm);
  const QPointF after = toLatLon(at);
  lat0_ = std::clamp(lat0_ + (before.x() - after.x()), -85.0, 85.0);
  lon0_ += before.y() - after.y();
  moved_ = true;
  invalidate();
  update();
  e->accept();
}

void GeoMap::leaveEvent(QEvent *) { setCursor(Qt::ArrowCursor); }

bool GeoMap::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    auto *he = static_cast<QHelpEvent *>(e);
    const int i = hit(QPointF(he->pos()));
    if (i >= 0) {
      const GeoMarker &m = content_.markers[i];
      const QString text = m.tip.isEmpty() ? m.label : m.tip;
      const QPointF at = placed()[static_cast<size_t>(i)].at;
      if (!text.isEmpty()) {
        QToolTip::showText(he->globalPos(), text, this,
                           QRectF(at.x() - 12, at.y() - 12, 24, 24).toRect());
        return true;
      }
    }
    QToolTip::showText(
        he->globalPos(),
        ui::helpText(
            faultNote_.isEmpty()
                ? tr("Drag to move the map, turn the wheel to zoom, double-"
                     "click to show everything. The fault lines are "
                     "seismogenic fault sources of EFSM20, a hazard model: "
                     "simplified traces, not an active fault map.")
                : tr("The fault layer could not be read: %1").arg(faultNote_)),
        this);
    return true;
  }
  return QWidget::event(e);
}

// ---- legend ----

GeoLegend::GeoLegend(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Minimum);
  sp.setHeightForWidth(true);
  setSizePolicy(sp);
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void GeoLegend::setEntries(const QList<Entry> &entries) {
  entries_ = entries;
  updateGeometry();
  update();
}

int GeoLegend::lay(int width, QPainter *p) const {
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.9);
  const QFontMetricsF fm(f);
  const double rowH = std::max(22.0, fm.height() + 6);
  double x = 2, y = 0;
  if (p) {
    p->setFont(f);
    p->setRenderHint(QPainter::Antialiasing);
  }
  for (const Entry &e : entries_) {
    const bool text = e.glyph == Entry::Glyph::Text;
    const double glyphW = text ? 0 : 26;
    const double w = glyphW + fm.horizontalAdvance(e.text) + 16;
    if (x > 2 && x + w > width) {
      x = 2;
      y += rowH;
    }
    if (p) {
      const QPointF c(x + 11, y + rowH / 2);
      const QColor ink = e.color.isValid()
                             ? e.color
                             : theme_->color(QStringLiteral("muted"));
      switch (e.glyph) {
      case Entry::Glyph::Marker:
        GeoMap::drawGlyph(*p, e.marker, c,
                          e.marker.kind == GeoMarker::Kind::Earthquake
                              ? std::min(9.0, GeoMap::quakeRadius(
                                                  e.marker.magnitude))
                              : e.marker.kind == GeoMarker::Kind::Dot ? 2.2
                                                                      : 5.5,
                          *theme_);
        break;
      case Entry::Glyph::Zone:
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(ink, 1.5, Qt::DashLine));
        p->drawEllipse(c, 7, 7);
        break;
      case Entry::Glyph::Fault:
        p->setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(c + QPointF(-9, 3), c + QPointF(9, -3));
        break;
      case Entry::Glyph::Coverage: {
        QColor fill = ink;
        fill.setAlphaF(0.18f);
        p->setPen(QPen(ink, 1.5));
        p->setBrush(fill);
        p->drawRoundedRect(QRectF(c.x() - 9, c.y() - 6, 18, 12), 6, 6);
        break;
      }
      case Entry::Glyph::Text:
        break;
      }
      p->setPen(theme_->color(text ? QStringLiteral("muted")
                                   : QStringLiteral("text")));
      p->drawText(QRectF(x + glyphW, y, w - glyphW, rowH),
                  Qt::AlignLeft | Qt::AlignVCenter, e.text);
    }
    x += w;
  }
  return int(std::ceil(y + rowH));
}

int GeoLegend::heightForWidth(int w) const {
  return entries_.isEmpty() ? 0 : lay(w, nullptr);
}

QSize GeoLegend::sizeHint() const {
  return {360, heightForWidth(width() > 50 ? width() : 360)};
}

void GeoLegend::paintEvent(QPaintEvent *) {
  QPainter p(this);
  lay(width(), &p);
}

} // namespace skg
