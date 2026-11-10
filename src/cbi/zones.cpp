#include "cbi/zones.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

#include "cbi/decluster.h"

namespace skg::cbi {

double FaultZone::distanceKm(double lon, double lat) const {
  const double kx = kKmPerDegLonEquator * std::cos(lat * M_PI / 180.0);
  double best = std::numeric_limits<double>::infinity();
  for (const Segment &s : segments) {
    const double ax = (s.lon0 - lon) * kx, ay = (s.lat0 - lat) * kKmPerDegLat;
    const double dx = (s.lon1 - s.lon0) * kx,
                 dy = (s.lat1 - s.lat0) * kKmPerDegLat;
    const double den = dx * dx + dy * dy;
    const double t =
        den > 0 ? std::clamp(-(ax * dx + ay * dy) / den, 0.0, 1.0) : 0.0;
    best = std::min(best, std::hypot(ax + t * dx, ay + t * dy));
  }
  return best;
}

namespace {
QJsonObject readGeoJson(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot read " + path).toStdString());
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error((path + ": " + err.errorString()).toStdString());
  const QJsonObject o = doc.object();
  if (!o.contains(QStringLiteral("features")))
    throw std::runtime_error(
        (path + " is not a GeoJSON FeatureCollection").toStdString());
  return o;
}

void addLine(FaultZone &z, const QJsonArray &coords) {
  for (int i = 0; i + 1 < coords.size(); ++i) {
    const QJsonArray a = coords[i].toArray(), b = coords[i + 1].toArray();
    if (a.size() < 2 || b.size() < 2)
      continue;
    z.segments.push_back(
        {a[0].toDouble(), a[1].toDouble(), b[0].toDouble(), b[1].toDouble()});
  }
}
} // namespace

QStringList zoneStrands(const QString &path) {
  QStringList out;
  for (const QJsonValue &v :
       readGeoJson(path).value(QStringLiteral("features")).toArray()) {
    const QString s = v.toObject()
                          .value(QStringLiteral("properties"))
                          .toObject()
                          .value(QStringLiteral("strand"))
                          .toString();
    if (!s.isEmpty() && !out.contains(s))
      out << s;
  }
  return out;
}

FaultZone loadFaultZone(const QString &path, const QStringList &strands) {
  const QJsonObject o = readGeoJson(path);
  const QStringList available = zoneStrands(path);
  for (const QString &s : strands)
    if (!available.contains(s))
      throw std::runtime_error(
          QStringLiteral("unknown strand %1; the file has %2")
              .arg(s, available.join(QStringLiteral(", ")))
              .toStdString());
  FaultZone z;
  z.name = o.value(QStringLiteral("name")).toString();
  z.strands = strands.isEmpty() ? available : strands;
  for (const QJsonValue &v : o.value(QStringLiteral("features")).toArray()) {
    const QJsonObject f = v.toObject();
    const QString strand = f.value(QStringLiteral("properties"))
                               .toObject()
                               .value(QStringLiteral("strand"))
                               .toString();
    if (!strands.isEmpty() && !strands.contains(strand))
      continue;
    const QJsonObject g = f.value(QStringLiteral("geometry")).toObject();
    const QString type = g.value(QStringLiteral("type")).toString();
    const QJsonArray coords = g.value(QStringLiteral("coordinates")).toArray();
    if (type == QLatin1String("LineString"))
      addLine(z, coords);
    else if (type == QLatin1String("MultiLineString"))
      for (const QJsonValue &line : coords)
        addLine(z, line.toArray());
  }
  if (z.segments.empty())
    throw std::runtime_error(
        (path + " holds no LineString features").toStdString());
  return z;
}

QStringList dataDirs() {
  QStringList dirs;
  const QString env = qEnvironmentVariable("SKG_DATA_DIR");
  if (!env.isEmpty())
    dirs << env;
  // applicationDirPath() needs the application object; the libraries are
  // also used without one (tests, tools).
  if (QCoreApplication::instance()) {
    const QString app = QCoreApplication::applicationDirPath();
    if (!app.isEmpty())
      dirs << app + QStringLiteral("/data")
           << app + QStringLiteral("/../share/sismokaos-gui/data")
           << app + QStringLiteral("/../Resources/data");
  }
#ifdef SKG_SOURCE_DATA_DIR
  dirs << QStringLiteral(SKG_SOURCE_DATA_DIR);
#endif
  for (QString &d : dirs)
    d = QDir::cleanPath(d);
  dirs.removeDuplicates();
  return dirs;
}

QString dataFile(const QString &name) {
  const QStringList dirs = dataDirs();
  for (const QString &d : dirs) {
    const QString path = QDir(d).filePath(name);
    if (QFileInfo(path).isFile())
      return path;
  }
  throw std::runtime_error(
      QStringLiteral("the data file %1 was not found; it is shipped beside "
                     "the program and was looked for in: %2")
          .arg(name, dirs.join(QStringLiteral("; ")))
          .toStdString());
}

QString eafzPath() { return dataFile(QStringLiteral("eafz.geojson")); }

FaultZone eafz(const QStringList &strands) {
  return loadFaultZone(eafzPath(), strands);
}

QString shippedFaultsPath() {
  return dataFile(QStringLiteral("faults_tr.geojson"));
}

const FaultZone &shippedFaults() {
  // Read once, and only once it has been found: a failed lookup is retried
  // on the next call instead of being remembered.
  static QMutex mutex;
  static std::optional<FaultZone> zone;
  const QMutexLocker lock(&mutex);
  if (!zone)
    zone = loadFaultZone(shippedFaultsPath());
  return *zone;
}

QString shippedFaultsLabel(const QString &lang) {
  return lang == QLatin1String("tr")
             ? QString::fromUtf8(
                   "Sismojenik fay kaynakları, EFSM20 (tehlike modeli)")
             : QStringLiteral(
                   "Seismogenic fault sources, EFSM20 (hazard model)");
}

QString shippedFaultsCredit(const QString &lang) {
  return lang == QLatin1String("tr")
             ? QString::fromUtf8("Fay kaynakları: EFSM20, Basili vd. (2022), "
                                 "INGV, CC BY 4.0")
             : QStringLiteral("Fault sources: EFSM20, Basili et al. (2022), "
                              "INGV, CC BY 4.0");
}

Catalog selectNearFault(const Catalog &catalog, const FaultZone &zone,
                        double bufferKm) {
  if (zone.empty())
    return catalog;
  double lonMin = 1e9, lonMax = -1e9, latMin = 1e9, latMax = -1e9;
  for (const FaultZone::Segment &s : zone.segments) {
    lonMin = std::min({lonMin, s.lon0, s.lon1});
    lonMax = std::max({lonMax, s.lon0, s.lon1});
    latMin = std::min({latMin, s.lat0, s.lat1});
    latMax = std::max({latMax, s.lat0, s.lat1});
  }
  // Cheap bounding-box prefilter before the exact distance.
  double maxAbsLat = 0;
  for (const analysis::Event &e : catalog.events)
    maxAbsLat = std::max(maxAbsLat, std::abs(e.lat));
  const double padLat = bufferKm / kKmPerDegLat;
  const double padLon =
      bufferKm / (kKmPerDegLonEquator *
                  std::cos(std::min(89.0, maxAbsLat) * M_PI / 180.0));
  std::vector<bool> keep(catalog.size(), false);
  for (size_t i = 0; i < catalog.size(); ++i) {
    const analysis::Event &e = catalog.events[i];
    if (e.lon < lonMin - padLon || e.lon > lonMax + padLon ||
        e.lat < latMin - padLat || e.lat > latMax + padLat)
      continue;
    keep[i] = zone.distanceKm(e.lon, e.lat) <= bufferKm;
  }
  return catalog.filtered(keep);
}

} // namespace skg::cbi
