#include "analysis/faults.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace skg::analysis {

QString Fault::label() const {
  if (segment.isEmpty())
    return name;
  return QStringLiteral("%1 (%2)").arg(name, segment);
}

QString Fault::typeName(int code) {
  switch (code) {
  case 1:
    return QStringLiteral("DYK");
  case 2:
    return QStringLiteral("HF");
  case 3:
    return QStringLiteral("KF");
  case 4:
    return QStringLiteral("OKF");
  }
  return QStringLiteral("Kod:%1").arg(code);
}

namespace {
QString text(const QVariantMap &r, std::initializer_list<const char *> names) {
  for (const char *n : names) {
    const QVariant v = r.value(QLatin1String(n));
    if (v.isValid() && !v.toString().trimmed().isEmpty())
      return v.toString().trimmed();
  }
  return {};
}
double num(const QVariantMap &r, std::initializer_list<const char *> names) {
  for (const char *n : names) {
    const QVariant v = r.value(QLatin1String(n));
    if (v.isValid())
      return v.toDouble();
  }
  return 0.0;
}
QRectF boundsOf(const std::vector<QPointF> &pts) {
  if (pts.empty())
    return {};
  double x0 = pts[0].x(), x1 = x0, y0 = pts[0].y(), y1 = y0;
  for (const QPointF &p : pts) {
    x0 = std::min(x0, p.x());
    x1 = std::max(x1, p.x());
    y0 = std::min(y0, p.y());
    y1 = std::max(y1, p.y());
  }
  return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}
} // namespace

FaultIndex::FaultIndex(const ShapeLayer &layer) {
  for (size_t i = 0; i < layer.shapes.size(); ++i) {
    const Shape &s = layer.shapes[i];
    if (s.kind != Shape::Kind::PolyLine)
      continue;
    const QVariantMap &r = layer.records[i];
    for (const std::vector<QPointF> &part : s.parts) {
      if (part.size() < 2)
        continue;
      Fault f;
      f.name = text(r, {"FAYADI", "FAY_ADI", "NAME", "Name", "name"});
      f.segment = text(r, {"SEGMENTADI", "SEGMENT", "SEGMENT_AD"});
      f.typeCode = int(num(r, {"FAYTIPI", "FAY_TIPI", "TYPE"}));
      f.slip = text(r, {"KAYMA_TURU", "KAYMATURU", "SLIP"});
      f.lengthKm = num(r, {"UZUNLUK", "LENGTH"});
      f.activity = text(r, {"AKTIVITE"});
      f.description = text(r, {"ACIKLAMA"});
      f.kind = text(r, {"FAYCESIDI"});
      f.direction = text(r, {"GENEL_DOGR"});
      f.paleoseismology = text(r, {"PALEOSISMO"});
      f.line = part;
      f.bbox = boundsOf(part);
      faults_.push_back(std::move(f));
    }
  }
  build();
}

FaultIndex::FaultIndex(std::vector<Fault> faults) : faults_(std::move(faults)) {
  for (Fault &f : faults_)
    if (f.bbox.isNull())
      f.bbox = boundsOf(f.line);
  build();
}

void FaultIndex::build() {
  cells_.clear();
  if (faults_.empty())
    return;
  bbox_ = faults_[0].bbox;
  for (const Fault &f : faults_)
    bbox_ = bbox_.united(f.bbox);
  // Cells of about a quarter degree; a fault spans the cells its box covers.
  nx_ = std::max(1, int(std::ceil(bbox_.width() / 0.25)));
  ny_ = std::max(1, int(std::ceil(bbox_.height() / 0.25)));
  cells_.assign(size_t(nx_ * ny_), {});
  for (size_t i = 0; i < faults_.size(); ++i) {
    const QRectF &b = faults_[i].bbox;
    for (int y = cellY(b.top()); y <= cellY(b.bottom()); ++y)
      for (int x = cellX(b.left()); x <= cellX(b.right()); ++x)
        cells_[size_t(y * nx_ + x)].push_back(int(i));
  }
}

int FaultIndex::cellX(double lon) const {
  return std::clamp(int((lon - bbox_.left()) / (bbox_.width() / nx_)), 0,
                    nx_ - 1);
}
int FaultIndex::cellY(double lat) const {
  return std::clamp(int((lat - bbox_.top()) / (bbox_.height() / ny_)), 0,
                    ny_ - 1);
}

std::optional<NearestFault> FaultIndex::nearest(double lat, double lon) const {
  if (faults_.empty())
    return std::nullopt;
  // Widen the search ring until something is found, then one more ring, so a
  // fault just across a cell edge still wins over a farther one found first.
  const int cx = cellX(lon), cy = cellY(lat);
  const int maxRing = std::max(nx_, ny_);
  std::optional<NearestFault> best;
  std::set<int> seen;
  for (int ring = 0, extra = -1; ring <= maxRing && extra != 0; ++ring) {
    for (int y = cy - ring; y <= cy + ring; ++y)
      for (int x = cx - ring; x <= cx + ring; ++x) {
        if (x < 0 || y < 0 || x >= nx_ || y >= ny_)
          continue;
        if (std::abs(x - cx) != ring && std::abs(y - cy) != ring)
          continue;
        for (int i : cells_[size_t(y * nx_ + x)]) {
          if (!seen.insert(i).second)
            continue;
          const NearestPoint np =
              nearestOnPolyline(lat, lon, faults_[size_t(i)].line);
          if (!best || np.km < best->km) {
            NearestFault nf;
            nf.fault = &faults_[size_t(i)];
            nf.km = np.km;
            nf.lat = np.lat;
            nf.lon = np.lon;
            nf.azimuth = bearingDeg(np.lat, np.lon, lat, lon);
            best = nf;
          }
        }
      }
    if (best && extra < 0)
      extra = 1; // one more ring after the first hit
    else if (extra > 0)
      --extra;
  }
  return best;
}

std::vector<const Fault *> FaultIndex::within(const QRectF &lonLat) const {
  std::vector<const Fault *> out;
  for (const Fault &f : faults_)
    if (f.bbox.intersects(lonLat))
      out.push_back(&f);
  return out;
}

FaultIndex loadFaults(const QString &shapefile) {
  return FaultIndex(readShapefile(shapefile));
}

} // namespace skg::analysis
