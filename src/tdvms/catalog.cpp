#include "tdvms/catalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimeZone>
#include <QUrlQuery>

#include <cmath>
#include <stdexcept>

#include "tdvms/portal.h"
#include "tdvms/settings.h"
#include "text/text.h"
#include <QFileInfo>
#include <algorithm>

namespace skg::tdvms {

namespace {

const QString kEventsUrl =
    QStringLiteral("https://earthquake.usgs.gov/fdsnws/event/1/query");

// RFC 4180-ish: quoted fields with doubled quotes, no embedded newlines.
QList<QStringList> parseCsv(const QByteArray &csv) {
  QList<QStringList> rows;
  for (const QByteArray &rawLine : csv.split('\n')) {
    QString line = QString::fromUtf8(rawLine);
    if (line.endsWith('\r'))
      line.chop(1);
    if (line.trimmed().isEmpty())
      continue;
    QStringList fields;
    QString cur;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
      const QChar c = line[i];
      if (quoted) {
        if (c == '"') {
          if (i + 1 < line.size() && line[i + 1] == '"') {
            cur += '"';
            ++i;
          } else {
            quoted = false;
          }
        } else {
          cur += c;
        }
      } else if (c == '"') {
        quoted = true;
      } else if (c == ',') {
        fields << cur;
        cur.clear();
      } else {
        cur += c;
      }
    }
    fields << cur;
    rows << fields;
  }
  return rows;
}

QString csvField(const QString &v) {
  if (v.contains(',') || v.contains('"') || v.contains('\n'))
    return '"' + QString(v).replace('"', QStringLiteral("\"\"")) + '"';
  return v;
}

QByteArray readPreferring(const QString &userPath, const QString &resource) {
  QFile user(userPath);
  if (user.exists() && user.open(QIODevice::ReadOnly))
    return user.readAll();
  QFile res(resource);
  if (res.open(QIODevice::ReadOnly))
    return res.readAll();
  return {};
}

bool writeAtomically(const QString &path, const QByteArray &data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(data);
  return f.commit();
}

} // namespace

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * M_PI / 180, p2 = lat2 * M_PI / 180;
  const double dp = p2 - p1, dl = (lon2 - lon1) * M_PI / 180;
  const double a =
      std::sin(dp / 2) * std::sin(dp / 2) +
      std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  return 6371.0 * 2 * std::asin(std::sqrt(a));
}

QString stationsPath() { return configDir() + QStringLiteral("/stations.csv"); }
QString eventsPath() { return configDir() + QStringLiteral("/events.csv"); }

QList<CatalogStation> parseStations(const QByteArray &csv) {
  QList<CatalogStation> out;
  const auto rows = parseCsv(csv);
  if (rows.isEmpty())
    return out;
  const QStringList header = rows.first();
  auto col = [&](const char *name) {
    return header.indexOf(QLatin1String(name));
  };
  const int cCode = col("code"), cNet = col("network"), cLat = col("latitude"),
            cLon = col("longitude"), cDev = col("devices");
  for (int i = 1; i < rows.size(); ++i) {
    const QStringList &r = rows[i];
    if (r.size() <= std::max({cCode, cNet, cLat, cLon, cDev}))
      continue;
    out.append(
        {r[cCode], r[cNet], r[cLat].toDouble(), r[cLon].toDouble(), r[cDev]});
  }
  return out;
}

QList<CatalogEvent> parseEvents(const QByteArray &csv) {
  QList<CatalogEvent> out;
  const auto rows = parseCsv(csv);
  if (rows.isEmpty())
    return out;
  const QStringList header = rows.first();
  auto col = [&](const char *name) {
    return header.indexOf(QLatin1String(name));
  };
  const int cId = col("id"), cTime = col("time"), cMag = col("magnitude"),
            cLat = col("latitude"), cLon = col("longitude"),
            cDepth = col("depth_km"), cPlace = col("place");
  for (int i = 1; i < rows.size(); ++i) {
    const QStringList &r = rows[i];
    if (r.size() <= std::max({cId, cTime, cMag, cLat, cLon, cDepth, cPlace}))
      continue;
    // USGS writes "?" for the letters it cannot encode; they are put back.
    const double lat = r[cLat].toDouble(), lon = r[cLon].toDouble();
    out.append({r[cId], r[cTime], r[cMag].toDouble(), lat, lon,
                r[cDepth].toDouble(), text::repairPlace(r[cPlace], lat, lon)});
  }
  return out;
}

QList<CatalogStation> loadStations() {
  return parseStations(readPreferring(
      stationsPath(), QStringLiteral(":/tdvms/data/stations.csv")));
}

QList<CatalogEvent> loadEvents() {
  return parseEvents(
      readPreferring(eventsPath(), QStringLiteral(":/tdvms/data/events.csv")));
}

QPair<int, int> refreshCatalogs(Http &http) {
  TdvmsPortal portal(&http, 60000);
  QMap<QString, CatalogStation> stations;
  for (const PortalStation &s : portal.allStations()) {
    if (!s.hasPosition)
      continue;
    CatalogStation c{s.code, s.network, std::round(s.latitude * 1e5) / 1e5,
                     std::round(s.longitude * 1e5) / 1e5, s.devices()};
    stations.insert(c.key(), c);
  }
  QByteArray csv = "code,network,latitude,longitude,devices\n";
  QList<CatalogStation> sorted = stations.values();
  std::sort(sorted.begin(), sorted.end(),
            [](const CatalogStation &a, const CatalogStation &b) {
              return std::make_pair(a.network, a.code) <
                     std::make_pair(b.network, b.code);
            });
  for (const CatalogStation &s : sorted)
    csv += QStringLiteral("%1,%2,%3,%4,%5\n")
               .arg(csvField(s.code), csvField(s.network),
                    QString::number(s.latitude, 'g', 10),
                    QString::number(s.longitude, 'g', 10), s.devices)
               .toUtf8();
  if (!writeAtomically(stationsPath(), csv))
    throw std::runtime_error("could not write " + stationsPath().toStdString());

  QUrl url(kEventsUrl);
  QUrlQuery q;
  q.addQueryItem("format", "geojson");
  q.addQueryItem("starttime", "1990-01-01");
  q.addQueryItem("minmagnitude", "5.0");
  q.addQueryItem("orderby", "time-asc");
  q.addQueryItem("minlatitude", "34.5");
  q.addQueryItem("maxlatitude", "43.0");
  q.addQueryItem("minlongitude", "25.0");
  q.addQueryItem("maxlongitude", "45.5");
  url.setQuery(q);
  const HttpResult r = http.get(url, 120000);
  if (!r.ok())
    throw std::runtime_error(
        QStringLiteral("USGS event service: %1")
            .arg(r.error.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status)
                                   : r.error)
            .toStdString());
  const QJsonArray features =
      QJsonDocument::fromJson(r.body).object().value("features").toArray();
  QByteArray ev = "id,time,magnitude,latitude,longitude,depth_km,place\n";
  int n = 0;
  for (const QJsonValue &f : features) {
    const QJsonObject o = f.toObject();
    const QJsonObject p = o.value("properties").toObject();
    const QJsonArray coords =
        o.value("geometry").toObject().value("coordinates").toArray();
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(
        qint64(p.value("time").toDouble()), QTimeZone::utc());
    ev += QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n")
              .arg(o.value("id").toString(),
                   when.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")),
                   QString::number(p.value("mag").toDouble(), 'g', 4),
                   QString::number(
                       std::round(coords.at(1).toDouble() * 1e4) / 1e4, 'g', 8),
                   QString::number(
                       std::round(coords.at(0).toDouble() * 1e4) / 1e4, 'g', 8),
                   QString::number(
                       std::round(coords.at(2).toDouble() * 10) / 10, 'g', 6),
                   csvField(p.value("place").toString()))
              .toUtf8();
    ++n;
  }
  if (!writeAtomically(eventsPath(), ev))
    throw std::runtime_error("could not write " + eventsPath().toStdString());
  return {sorted.size(), n};
}

} // namespace skg::tdvms
