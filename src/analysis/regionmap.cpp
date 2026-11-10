#include "analysis/regionmap.h"

#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "analysis/geo.h"
#include "analysis/mapview.h"
#include "fx/csv.h"

static void initAnalysisResources() { Q_INIT_RESOURCE(analysis); }

namespace skg::analysis {

namespace {
QString tr(const QString &lang, const char *trText, const char *enText) {
  return lang == QLatin1String("tr") ? QString::fromUtf8(trText)
                                     : QString::fromUtf8(enText);
}

std::runtime_error fail(const QString &message) {
  return std::runtime_error(message.toStdString());
}

struct FaultStyle {
  const char *code;
  QColor color;
  double width;
  const char *tr, *en;
};
// MTA's activity classes, most recent rupture first.
const std::array<FaultStyle, 5> kFaultStyles{{
    {"DYK", QColor(0xD3, 0x2F, 0x2F), 1.6, "Deprem Yüzey Kırığı (DYK)",
     "Earthquake Surface Rupture (ESR)"},
    {"H-DYK", QColor(0xB7, 0x1C, 0x1C), 1.5, "Holosen + Yüzey Kırığı (H-DYK)",
     "Holocene + Surface Rupture (H-ESR)"},
    {"H", QColor(0xE6, 0x51, 0x00), 1.2, "Holosen Fayı (H)",
     "Holocene Fault (H)"},
    {"K-H", QColor(0xF5, 0x7C, 0x00), 1.0, "Kuvaterner-Holosen Fayı (K-H)",
     "Quaternary-Holocene Fault (Q-H)"},
    {"K", QColor(0xFF, 0x8F, 0x00), 0.9, "Kuvaterner Fayı (K)",
     "Quaternary Fault (Q)"},
}};
const QColor kFaultPlain(0xD3, 0x2F, 0x2F);
// Fault sources of a hazard model: muted, so they do not read as mapped
// active faults.
const QColor kFaultSource(0x8C, 0x6D, 0x5E);

const FaultStyle *faultStyle(const QString &activity) {
  for (const FaultStyle &s : kFaultStyles)
    if (activity == QLatin1String(s.code))
      return &s;
  return nullptr;
}

bool isTurkey(const QVariantMap &record) {
  const QString s = record.value(QStringLiteral("sovereignt")).toString() +
                    QLatin1Char(' ') +
                    record.value(QStringLiteral("SOVEREIGNT")).toString();
  return s.contains(QLatin1String("Turkey"), Qt::CaseInsensitive) ||
         s.contains(QString::fromUtf8("Türkiye"), Qt::CaseInsensitive);
}

QPainterPath polygonPath(const MapView &v, const Shape &s) {
  QPainterPath path;
  for (const auto &part : s.parts) {
    QPolygonF poly;
    poly.reserve(static_cast<qsizetype>(part.size()));
    for (const QPointF &q : part)
      poly << v.toPx(q);
    path.addPolygon(poly);
    path.closeSubpath();
  }
  return path;
}

double tickStep(double span) {
  for (double s : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0})
    if (span / s <= 9)
      return s;
  return 20.0;
}

QString tickText(double v) {
  QString s = QString::number(v, 'f', 2);
  while (s.endsWith(QLatin1Char('0')))
    s.chop(1);
  if (s.endsWith(QLatin1Char('.')))
    s.chop(1);
  return s;
}

struct LegendRow {
  enum class Kind { Dot, Star, Diamond, Line, Patch, City };
  Kind kind;
  QColor color, edge;
  double width = 1;
  QString label;
};
} // namespace

const std::vector<City> &eafzCities() {
  static const std::vector<City> cities{
      {37.00, 35.32, QStringLiteral("Adana"), true},
      {36.80, 34.63, QStringLiteral("Mersin"), true},
      {37.07, 36.24, QStringLiteral("Osmaniye"), false},
      {36.20, 36.16, QStringLiteral("Hatay"), false},
      {37.06, 37.38, QStringLiteral("Gaziantep"), true},
      {36.72, 37.12, QStringLiteral("Kilis"), false},
      {37.58, 36.94, QString::fromUtf8("Kahramanmaraş"), false},
      {37.76, 38.28, QString::fromUtf8("Adıyaman"), false},
      {37.16, 38.79, QString::fromUtf8("Şanlıurfa"), true},
      {38.35, 38.31, QStringLiteral("Malatya"), false},
      {38.67, 39.22, QString::fromUtf8("Elazığ"), false},
      {37.91, 40.22, QString::fromUtf8("Diyarbakır"), false},
      {39.11, 39.54, QStringLiteral("Tunceli"), false},
      {37.97, 34.68, QString::fromUtf8("Niğde"), true},
      {38.73, 35.49, QStringLiteral("Kayseri"), true},
      {38.62, 34.72, QString::fromUtf8("Nevşehir"), true},
  };
  return cities;
}

std::vector<City> loadCities(const QString &path) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  std::vector<City> out;
  int lat = 0, lon = 1, name = 2, align = 3;
  qsizetype first = 0;
  if (!rows.isEmpty()) {
    const QStringList h = fx::splitCsv(rows.first());
    if (!fx::number(h, 0)) { // a header line
      lat = fx::column(h, {"lat", "latitude", "enlem"});
      lon = fx::column(h, {"lon", "longitude", "boylam"});
      name = fx::column(h, {"name", "city", "sehir", "şehir", "il", "ad"});
      align = fx::column(h, {"align", "alignment", "side", "hiza"});
      if (lat < 0 || lon < 0 || name < 0)
        throw fail(QStringLiteral("%1 needs lat, lon and name columns; its "
                                  "columns are %2")
                       .arg(path, h.join(QStringLiteral(", "))));
      first = 1;
    }
  }
  for (qsizetype i = first; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = fx::splitCsv(rows[i]);
    const auto la = fx::number(r, lat), lo = fx::number(r, lon);
    if (!la || !lo || name >= r.size() || r[name].trimmed().isEmpty())
      continue;
    City c;
    c.lat = *la;
    c.lon = *lo;
    c.name = r[name].trimmed();
    // As the script: "right" aligns the text's right edge to the marker.
    c.labelLeft = align >= 0 && align < r.size() &&
                  r[align].trimmed().compare(QLatin1String("right"),
                                             Qt::CaseInsensitive) == 0;
    out.push_back(c);
  }
  if (out.empty())
    throw fail(QStringLiteral("%1 holds no city (lat, lon, name)").arg(path));
  return out;
}

std::vector<FaultLine> faultLines(const FaultIndex &faults) {
  std::vector<FaultLine> out;
  out.reserve(faults.faults().size());
  for (const Fault &f : faults.faults())
    out.push_back({f.line, f.activity.trimmed()});
  return out;
}

ShapeLayer readGeoJsonPolygons(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw fail(QStringLiteral("cannot read %1").arg(path));
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (doc.isNull())
    throw fail(QStringLiteral("%1: %2").arg(path, err.errorString()));
  ShapeLayer layer;
  double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0,
         y1 = -x0;
  const QJsonArray features =
      doc.object().value(QStringLiteral("features")).toArray();
  for (const QJsonValue &fv : features) {
    const QJsonObject feature = fv.toObject();
    const QJsonObject geometry =
        feature.value(QStringLiteral("geometry")).toObject();
    const QString type = geometry.value(QStringLiteral("type")).toString();
    const QJsonArray coords =
        geometry.value(QStringLiteral("coordinates")).toArray();
    QList<QJsonArray> polygons;
    if (type == QLatin1String("Polygon"))
      polygons << coords;
    else if (type == QLatin1String("MultiPolygon"))
      for (const QJsonValue &pv : coords)
        polygons << pv.toArray();
    else
      continue;
    Shape s;
    s.kind = Shape::Kind::Polygon;
    double sx0 = std::numeric_limits<double>::infinity(), sy0 = sx0, sx1 = -sx0,
           sy1 = -sx0;
    for (const QJsonArray &polygon : polygons)
      for (const QJsonValue &rv : polygon) {
        std::vector<QPointF> ring;
        const QJsonArray ringArray = rv.toArray();
        ring.reserve(static_cast<size_t>(ringArray.size()));
        for (const QJsonValue &cv : ringArray) {
          const QJsonArray c = cv.toArray();
          if (c.size() < 2)
            continue;
          const QPointF q(c.at(0).toDouble(), c.at(1).toDouble());
          sx0 = std::min(sx0, q.x());
          sx1 = std::max(sx1, q.x());
          sy0 = std::min(sy0, q.y());
          sy1 = std::max(sy1, q.y());
          ring.push_back(q);
        }
        if (ring.size() >= 3)
          s.parts.push_back(std::move(ring));
      }
    if (s.parts.empty())
      continue;
    s.bbox = QRectF(QPointF(sx0, sy0), QPointF(sx1, sy1));
    x0 = std::min(x0, sx0);
    x1 = std::max(x1, sx1);
    y0 = std::min(y0, sy0);
    y1 = std::max(y1, sy1);
    layer.shapes.push_back(std::move(s));
    layer.records.push_back(
        feature.value(QStringLiteral("properties")).toObject().toVariantMap());
  }
  if (layer.shapes.empty())
    throw fail(QStringLiteral("%1 holds no polygon").arg(path));
  for (const QString &k : layer.records.front().keys())
    layer.fields << k;
  layer.bbox = QRectF(QPointF(x0, y0), QPointF(x1, y1));
  return layer;
}

const ShapeLayer &shippedCountries() {
  static const ShapeLayer layer = [] {
    initAnalysisResources();
    return readGeoJsonPolygons(QStringLiteral(":/analysis/countries.geojson"));
  }();
  return layer;
}

QColor regionColor(int index, int count) {
  static const std::array<QColor, 8> wong{
      QColor(0x00, 0x72, 0xB2), QColor(0xE6, 0x9F, 0x00),
      QColor(0x00, 0x9E, 0x73), QColor(0xCC, 0x79, 0xA7),
      QColor(0x56, 0xB4, 0xE9), QColor(0xD5, 0x5E, 0x00),
      QColor(0xF0, 0xE4, 0x42), QColor(0x00, 0x00, 0x00)};
  static const std::array<QColor, 20> many{
      QColor(0x1f, 0x77, 0xb4), QColor(0xae, 0xc7, 0xe8),
      QColor(0xff, 0x7f, 0x0e), QColor(0xff, 0xbb, 0x78),
      QColor(0x2c, 0xa0, 0x2c), QColor(0x98, 0xdf, 0x8a),
      QColor(0xd6, 0x27, 0x28), QColor(0xff, 0x98, 0x96),
      QColor(0x94, 0x67, 0xbd), QColor(0xc5, 0xb0, 0xd5),
      QColor(0x8c, 0x56, 0x4b), QColor(0xc4, 0x9c, 0x94),
      QColor(0xe3, 0x77, 0xc2), QColor(0xf7, 0xb6, 0xd2),
      QColor(0x7f, 0x7f, 0x7f), QColor(0xc7, 0xc7, 0xc7),
      QColor(0xbc, 0xbd, 0x22), QColor(0xdb, 0xdb, 0x8d),
      QColor(0x17, 0xbe, 0xcf), QColor(0x9e, 0xda, 0xe5)};
  if (index < 0)
    return Qt::gray;
  if (count <= 8)
    return wong[static_cast<size_t>(index) % wong.size()];
  return many[static_cast<size_t>(index) % many.size()];
}

QSize regionMapSize(int dpi) { return QSize(12 * dpi, 10 * dpi); }

QRectF regionMapExtent(const RegionMapSpec &spec) {
  if (!spec.points || spec.points->empty())
    return QRectF(QPointF(34, 36), QPointF(42, 40));
  double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0,
         y1 = -x0;
  for (const Point2 &q : *spec.points) {
    y0 = std::min(y0, q[0]);
    y1 = std::max(y1, q[0]);
    x0 = std::min(x0, q[1]);
    x1 = std::max(x1, q[1]);
  }
  return QRectF(QPointF(x0 - spec.padDeg, y0 - spec.padDeg),
                QPointF(x1 + spec.padDeg, y1 + spec.padDeg));
}

void drawRegionMap(QPainter &p, const QRectF &rect, const RegionMapSpec &spec) {
  const double u = rect.width() / 1200.0; // one design pixel
  auto font = [&](double size, bool bold = false, bool italic = false) {
    QFont f = p.font();
    f.setPixelSize(std::max(1, static_cast<int>(std::lround(size * u))));
    f.setBold(bold);
    f.setItalic(italic);
    return f;
  };
  const QString &lang = spec.lang;
  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect, Qt::white);

  const bool boundaries = spec.countries || spec.provinces;
  const int titleLines =
      spec.title.isEmpty()
          ? 0
          : 1 + static_cast<int>(spec.title.count(QLatin1Char('\n')));
  // Kilometres are square at the extent's centre latitude.
  QRectF extent = regionMapExtent(spec);
  const double kmPerDeg = kEarthRadiusKm * std::numbers::pi / 180.0;
  const double cosLat =
      std::cos(extent.center().y() * std::numbers::pi / 180.0);
  double kmW = extent.width() * kmPerDeg * cosLat,
         kmH = extent.height() * kmPerDeg;
  QRectF area = rect.adjusted(92 * u, (22 + 27 * titleLines) * u, -28 * u,
                              -(boundaries ? 104 : 78) * u);
  // Pixels per km; then the extent grows to fill the frame.
  const double scale = std::min(area.width() / kmW, area.height() / kmH);
  kmW = area.width() / scale;
  kmH = area.height() / scale;
  const double lonSpan = kmW / kmPerDeg / cosLat, latSpan = kmH / kmPerDeg;
  extent = QRectF(QPointF(extent.center().x() - lonSpan / 2,
                          extent.center().y() - latSpan / 2),
                  QSizeF(lonSpan, latSpan));
  const MapView v{area, extent};

  p.save();
  p.setClipRect(area);
  p.fillRect(area, spec.countries ? QColor(0xEA, 0xF2, 0xF7)
                                  : QColor(0xF8, 0xF8, 0xF8));
  // Countries: the neighbours, then Turkey on top.
  bool turkeyDrawn = false, neighboursDrawn = false;
  if (spec.countries)
    for (int pass = 0; pass < 2; ++pass)
      for (size_t i = 0; i < spec.countries->shapes.size(); ++i) {
        const Shape &s = spec.countries->shapes[i];
        const bool turkey = i < spec.countries->records.size() &&
                            isTurkey(spec.countries->records[i]);
        if (s.kind != Shape::Kind::Polygon || turkey != (pass == 1) ||
            !s.bbox.intersects(extent))
          continue;
        p.setPen(
            QPen(turkey ? QColor(0x44, 0x44, 0x44) : QColor(0x99, 0x99, 0x99),
                 (turkey ? 0.9 : 0.6) * u));
        p.setBrush(turkey ? QColor(0xFF, 0xF8, 0xE7)
                          : QColor(0xDC, 0xDC, 0xDC));
        p.drawPath(polygonPath(v, s));
        (turkey ? turkeyDrawn : neighboursDrawn) = true;
      }
  if (spec.provinces) {
    p.setPen(QPen(QColor(0xBB, 0xBB, 0xBB), 0.6 * u));
    p.setBrush(Qt::NoBrush);
    for (const Shape &s : spec.provinces->shapes)
      if (s.kind == Shape::Kind::Polygon && s.bbox.intersects(extent))
        p.drawPath(polygonPath(v, s));
  }
  // Graticule under the data.
  const double stepLon = tickStep(extent.width()),
               stepLat = tickStep(extent.height());
  p.setPen(QPen(QColor(0x60, 0x60, 0x60, 70), 0.6 * u, Qt::DashLine));
  for (double lon = std::ceil(extent.left() / stepLon) * stepLon;
       lon < extent.right(); lon += stepLon)
    p.drawLine(v.toPx(lon, extent.top()), v.toPx(lon, extent.bottom()));
  for (double lat = std::ceil(extent.top() / stepLat) * stepLat;
       lat < extent.bottom(); lat += stepLat)
    p.drawLine(v.toPx(extent.left(), lat), v.toPx(extent.right(), lat));
  // Faults, by activity class when the layer has one.
  std::array<bool, 5> classSeen{};
  bool plainFaultSeen = false;
  if (spec.faults) {
    p.setBrush(Qt::NoBrush);
    for (const FaultLine &f : *spec.faults) {
      const FaultStyle *style = faultStyle(f.activity);
      if (f.line.size() < 2)
        continue;
      // Skip lines wholly outside; a segment may cross without a vertex in.
      QPolygonF poly;
      double x0 = f.line.front().x(), x1 = x0, y0 = f.line.front().y(), y1 = y0;
      for (const QPointF &q : f.line) {
        x0 = std::min(x0, q.x());
        x1 = std::max(x1, q.x());
        y0 = std::min(y0, q.y());
        y1 = std::max(y1, q.y());
        poly << v.toPx(q);
      }
      if (x1 < extent.left() || x0 > extent.right() || y1 < extent.top() ||
          y0 > extent.bottom())
        continue;
      const QColor plain =
          spec.faultsLabel.isEmpty() ? kFaultPlain : kFaultSource;
      p.setPen(QPen(style ? style->color : plain,
                    (style ? style->width : 1.2) * 1.3 * u, Qt::SolidLine,
                    Qt::RoundCap, Qt::RoundJoin));
      p.drawPolyline(poly);
      if (style)
        classSeen[static_cast<size_t>(style - kFaultStyles.data())] = true;
      else
        plainFaultSeen = true;
    }
  }
  // Events.
  std::vector<int> colorOf; // region id -> colour index
  int regionCount = 0;
  if (spec.regions) {
    regionCount = static_cast<int>(spec.regions->regions.size());
    for (int i = 0; i < regionCount; ++i) {
      const int id = spec.regions->regions[static_cast<size_t>(i)].id;
      if (id >= static_cast<int>(colorOf.size()))
        colorOf.resize(static_cast<size_t>(id) + 1, -1);
      colorOf[static_cast<size_t>(id)] = i;
    }
  }
  if (spec.points) {
    p.setPen(Qt::NoPen);
    const bool coloured =
        spec.regions && spec.regions->regionOf.size() == spec.points->size();
    const double r = (coloured ? 1.7 : 1.4) * u;
    QColor grey(0x80, 0x80, 0x80);
    grey.setAlphaF(0.3);
    for (size_t i = 0; i < spec.points->size(); ++i) {
      QColor c = grey;
      if (coloured) {
        const int id = spec.regions->regionOf[i];
        c = regionColor(id >= 0 && id < static_cast<int>(colorOf.size())
                            ? colorOf[static_cast<size_t>(id)]
                            : -1,
                        regionCount);
        c.setAlphaF(0.4);
      }
      p.setBrush(c);
      p.drawEllipse(v.toPx((*spec.points)[i][1], (*spec.points)[i][0]), r, r);
    }
  }
  // Region centres.
  if (spec.regions) {
    p.setPen(QPen(Qt::white, 0.8 * u));
    p.setBrush(Qt::black);
    for (const RegionSummary &s : spec.regions->regions)
      p.drawPath(starPath(v.toPx(s.center[1], s.center[0]), 7 * u));
  }
  // The marker of interest.
  if (spec.marker) {
    const QPointF c = v.toPx(spec.marker->lon, spec.marker->lat);
    const double r = 7 * u;
    QPolygonF d;
    d << QPointF(c.x(), c.y() - r) << QPointF(c.x() + r, c.y())
      << QPointF(c.x(), c.y() + r) << QPointF(c.x() - r, c.y());
    p.setPen(QPen(Qt::white, 0.8 * u));
    p.setBrush(QColor(0xE0, 0x10, 0x10));
    p.drawPolygon(d);
  }
  // Cities.
  {
    const QFont cf = font(11.5, true);
    p.setFont(cf);
    const QFontMetricsF fm(cf);
    for (const City &c : spec.cities) {
      if (!extent.contains(QPointF(c.lon, c.lat)))
        continue;
      const QPointF q = v.toPx(c.lon, c.lat);
      p.setPen(QPen(Qt::white, 0.9 * u));
      p.setBrush(QColor(0x11, 0x11, 0x11));
      p.drawEllipse(q, 3.4 * u, 3.4 * u);
      const double w = fm.horizontalAdvance(c.name) + 5 * u, h = fm.height();
      const QRectF box(c.labelLeft ? q.x() - 7 * u - w : q.x() + 7 * u,
                       q.y() - h / 2, w, h);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 255, 255, 184));
      p.drawRoundedRect(box, 2 * u, 2 * u);
      p.setPen(QColor(0x11, 0x11, 0x11));
      p.drawText(box, Qt::AlignCenter, c.name);
    }
  }
  // Scale bar, lower left.
  {
    const QFont sf = font(12, true);
    p.setFont(sf);
    const QFontMetricsF fm(sf);
    const double km = niceScaleKm(area.width() / scale * 0.7);
    const double w = km * scale;
    const QPointF a(area.left() + 0.05 * area.width(),
                    area.bottom() - 0.04 * area.height());
    p.setPen(QPen(Qt::black, 2.6 * u, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(a, QPointF(a.x() + w, a.y()));
    p.setPen(QPen(Qt::black, 2.0 * u));
    p.drawLine(QPointF(a.x(), a.y() - 5 * u), QPointF(a.x(), a.y() + 5 * u));
    p.drawLine(QPointF(a.x() + w, a.y() - 5 * u),
               QPointF(a.x() + w, a.y() + 5 * u));
    p.drawText(QRectF(a.x() - 40 * u, a.y() - fm.height() - 7 * u, w + 80 * u,
                      fm.height()),
               Qt::AlignCenter, QStringLiteral("%1 km").arg(km));
  }
  // North arrow, upper right.
  {
    const QFont nf = font(16, true);
    p.setFont(nf);
    const QFontMetricsF fm(nf);
    const QPointF c(area.left() + 0.94 * area.width(),
                    area.top() + 0.13 * area.height());
    const double len = 0.06 * area.height();
    p.setPen(QPen(Qt::black, 2.2 * u, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(c, QPointF(c.x(), c.y() - len));
    QPolygonF head;
    head << QPointF(c.x(), c.y() - len - 6 * u)
         << QPointF(c.x() - 6 * u, c.y() - len + 7 * u)
         << QPointF(c.x() + 6 * u, c.y() - len + 7 * u);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);
    p.drawPolygon(head);
    p.setPen(Qt::black);
    p.drawText(QRectF(c.x() - 20 * u, c.y() - len - 8 * u - fm.height(), 40 * u,
                      fm.height()),
               Qt::AlignCenter, QStringLiteral("N"));
  }
  // Legend, lower right.
  {
    std::vector<LegendRow> rows;
    using K = LegendRow::Kind;
    if (spec.regions) {
      for (int i = 0; i < regionCount; ++i)
        rows.push_back(
            {K::Dot, regionColor(i, regionCount), QColor(), 1,
             QStringLiteral("%1 %2")
                 .arg(tr(lang, "Bölge", "Region"))
                 .arg(spec.regions->regions[static_cast<size_t>(i)].id)});
      rows.push_back({K::Star, Qt::black, QColor(), 1,
                      tr(lang, "Küme Merkezi", "Cluster Center")});
    } else if (spec.points) {
      rows.push_back({K::Dot, QColor(0x88, 0x88, 0x88), QColor(), 1,
                      QStringLiteral("%1  (N = %L2)")
                          .arg(tr(lang, "Depremler", "Earthquakes"))
                          .arg(static_cast<qlonglong>(spec.points->size()))});
    }
    if (spec.marker && !spec.marker->label.isEmpty())
      rows.push_back({K::Diamond, QColor(0xE0, 0x10, 0x10), QColor(), 1,
                      spec.marker->label});
    if (!spec.regions && !spec.cities.empty())
      rows.push_back(
          {K::City, Qt::black, QColor(), 1, tr(lang, "Şehir", "City")});
    for (size_t i = 0; i < kFaultStyles.size(); ++i)
      if (classSeen[i])
        rows.push_back({K::Line, kFaultStyles[i].color, QColor(),
                        std::max(kFaultStyles[i].width, 1.2),
                        tr(lang, kFaultStyles[i].tr, kFaultStyles[i].en)});
    if (plainFaultSeen)
      rows.push_back(
          {K::Line, spec.faultsLabel.isEmpty() ? kFaultPlain : kFaultSource,
           QColor(), 1.4,
           spec.faultsLabel.isEmpty() ? tr(lang, "Diri Fay", "Active Fault")
                                      : spec.faultsLabel});
    if (turkeyDrawn)
      rows.push_back({K::Patch, QColor(0xFF, 0xF8, 0xE7),
                      QColor(0x44, 0x44, 0x44), 1,
                      tr(lang, "Türkiye", "Turkey")});
    if (neighboursDrawn)
      rows.push_back({K::Patch, QColor(0xDC, 0xDC, 0xDC),
                      QColor(0x99, 0x99, 0x99), 1,
                      tr(lang, "Komşu Ülkeler", "Neighboring Countries")});
    if (!rows.empty()) {
      const QFont lf = font(11), tf = font(11.5, true);
      const QFontMetricsF fm(lf), tfm(tf);
      const QString heading = spec.regions
                                  ? tr(lang, "Alt Bölgeler", "Sub-regions")
                                  : tr(lang, "Lejant", "Legend");
      double w = tfm.horizontalAdvance(heading);
      for (const LegendRow &r : rows)
        w = std::max(w, 26 * u + fm.horizontalAdvance(r.label));
      const double lh = fm.height() * 1.18;
      const QRectF box(area.right() - w - 30 * u,
                       area.bottom() - (double(rows.size()) + 1) * lh - 22 * u,
                       w + 18 * u, (double(rows.size()) + 1) * lh + 10 * u);
      p.setPen(QPen(QColor(0xBB, 0xBB, 0xBB), 0.8 * u));
      p.setBrush(QColor(255, 255, 255, 235));
      p.drawRoundedRect(box, 3 * u, 3 * u);
      p.setFont(tf);
      p.setPen(Qt::black);
      p.drawText(QRectF(box.left(), box.top() + 4 * u, box.width(), lh),
                 Qt::AlignCenter, heading);
      p.setFont(lf);
      for (size_t i = 0; i < rows.size(); ++i) {
        const LegendRow &r = rows[i];
        const double y = box.top() + 4 * u + (double(i) + 1) * lh + lh / 2;
        const QPointF c(box.left() + 16 * u, y);
        p.setPen(Qt::NoPen);
        p.setBrush(r.color);
        switch (r.kind) {
        case K::Dot:
          p.drawEllipse(c, 4.5 * u, 4.5 * u);
          break;
        case K::City:
          p.setPen(QPen(Qt::white, 0.9 * u));
          p.drawEllipse(c, 4 * u, 4 * u);
          break;
        case K::Star:
          p.drawPath(starPath(c, 4.6 * u));
          break;
        case K::Diamond: {
          QPolygonF d;
          d << QPointF(c.x(), c.y() - 5 * u) << QPointF(c.x() + 5 * u, c.y())
            << QPointF(c.x(), c.y() + 5 * u) << QPointF(c.x() - 5 * u, c.y());
          p.drawPolygon(d);
          break;
        }
        case K::Line:
          p.setPen(QPen(r.color, r.width * 1.5 * u));
          p.drawLine(QPointF(c.x() - 8 * u, y), QPointF(c.x() + 8 * u, y));
          break;
        case K::Patch:
          p.setPen(QPen(r.edge, 0.8 * u));
          p.drawRect(QRectF(c.x() - 8 * u, y - 5 * u, 16 * u, 10 * u));
          break;
        }
        p.setPen(Qt::black);
        p.drawText(QRectF(box.left() + 32 * u, y - lh / 2, w, lh),
                   Qt::AlignVCenter | Qt::AlignLeft, r.label);
      }
    }
  }
  p.restore(); // the clip

  // Frame, ticks, axis titles.
  p.setPen(QPen(Qt::black, 1.0 * u));
  p.setBrush(Qt::NoBrush);
  p.drawRect(area);
  {
    const QFont tf = font(13);
    p.setFont(tf);
    const QFontMetricsF fm(tf);
    for (double lon = std::ceil(extent.left() / stepLon) * stepLon;
         lon < extent.right(); lon += stepLon) {
      const double x = v.toPx(lon, extent.top()).x();
      p.drawLine(QPointF(x, area.bottom()), QPointF(x, area.bottom() + 4 * u));
      p.drawText(QRectF(x - 40 * u, area.bottom() + 6 * u, 80 * u, fm.height()),
                 Qt::AlignCenter, tickText(lon));
    }
    for (double lat = std::ceil(extent.top() / stepLat) * stepLat;
         lat < extent.bottom(); lat += stepLat) {
      const double y = v.toPx(extent.left(), lat).y();
      p.drawLine(QPointF(area.left() - 4 * u, y), QPointF(area.left(), y));
      p.drawText(QRectF(area.left() - 66 * u, y - fm.height() / 2, 58 * u,
                        fm.height()),
                 Qt::AlignRight | Qt::AlignVCenter, tickText(lat));
    }
    const QFont af = font(16);
    p.setFont(af);
    const QFontMetricsF afm(af);
    const double axisY = area.bottom() + 8 * u + fm.height();
    p.drawText(QRectF(area.left(), axisY, area.width(), afm.height()),
               Qt::AlignCenter, tr(lang, "Boylam (°D)", "Longitude (°E)"));
    p.save();
    p.translate(area.left() - 74 * u, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-area.height() / 2, -afm.height() / 2, area.height(),
                      afm.height()),
               Qt::AlignCenter, tr(lang, "Enlem (°K)", "Latitude (°N)"));
    p.restore();
    if (!spec.credit.isEmpty()) {
      // Beside the axis title, at the right edge of the map.
      const QFont cf = font(10.5, false, true);
      p.setFont(cf);
      p.setPen(QColor(0x44, 0x44, 0x44));
      const QFontMetricsF cfm(cf);
      const double half =
          afm.horizontalAdvance(tr(lang, "Boylam (°D)", "Longitude (°E)")) / 2;
      const QRectF box(area.center().x() + half + 12 * u, axisY,
                       area.width() / 2 - half - 12 * u, afm.height());
      p.drawText(box, Qt::AlignRight | Qt::AlignVCenter,
                 cfm.elidedText(spec.credit, Qt::ElideRight, box.width()));
      p.setPen(Qt::black);
    }
    if (boundaries) {
      p.setFont(font(11.5, false, true));
      p.setPen(QColor(0x44, 0x44, 0x44));
      p.drawText(
          QRectF(rect.left(), axisY + afm.height() + 4 * u, rect.width(),
                 afm.height()),
          Qt::AlignCenter,
          tr(lang,
             "Harita çizgileri çalışma alanlarını gösterir; kabul edilmiş "
             "ulusal sınırları yansıtmayabilir.",
             "Map lines delineate study areas and do not necessarily depict "
             "accepted national boundaries."));
    }
  }
  if (!spec.title.isEmpty()) {
    p.setFont(font(18, true));
    p.setPen(Qt::black);
    p.drawText(QRectF(rect.left(), rect.top(), rect.width(),
                      area.top() - rect.top() - 8 * u),
               Qt::AlignHCenter | Qt::AlignBottom, spec.title);
  }
  p.restore();
}

} // namespace skg::analysis
