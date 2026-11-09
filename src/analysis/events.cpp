#include "analysis/events.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "analysis/geo.h"
#include "fx/catalog.h"
#include "fx/csv.h"

namespace skg::analysis {

std::vector<Event> loadEvents(const QString &path,
                              double catalogUtcOffsetHours) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QStringList h = fx::splitCsv(rows.first());
  const int t = fx::column(h, {"Date", "DateTime", "Tarih", "Zaman", "time",
                               "origin_time"}),
            lat = fx::column(h, {"Latitude", "Enlem", "lat"}),
            lon = fx::column(h, {"Longitude", "Boylam", "lon"}),
            dep = fx::column(h, {"Depth", "Derinlik", "depth_km", "depth"}),
            mag = fx::column(h, {"Magnitude", "Buyukluk", "Büyüklük", "Mag",
                                 "ML", "Mw", "mag"}),
            typ = fx::column(h, {"Type", "Tip", "MagType", "mag_type"}),
            place = fx::column(
                h, {"Location", "Yer", "Lokasyon", "Place", "place"}),
            id = fx::column(h, {"EventID", "ID", "id", "event_id"});
  if (t < 0 || lat < 0 || lon < 0 || mag < 0)
    throw std::runtime_error(
        QStringLiteral(
            "%1 needs Date, Latitude, Longitude and Magnitude columns; its "
            "columns are %2")
            .arg(path, h.join(", "))
            .toStdString());
  std::vector<Event> out;
  out.reserve(size_t(rows.size()));
  for (int i = 1; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = fx::splitCsv(rows[i]);
    if (t >= r.size())
      continue;
    std::optional<double> epoch = fx::parseAfadTime(r[t]);
    if (!epoch) { // ISO 8601 as a second chance
      const QDateTime dt =
          QDateTime::fromString(r[t].trimmed(), Qt::ISODateWithMs);
      if (dt.isValid())
        epoch = double(dt.toUTC().toMSecsSinceEpoch()) / 1000.0;
    }
    const auto m = fx::number(r, mag), la = fx::number(r, lat),
               lo = fx::number(r, lon);
    if (!epoch || !m || !la || !lo)
      continue;
    Event e;
    e.epoch = *epoch - catalogUtcOffsetHours * 3600.0;
    e.lat = *la;
    e.lon = *lo;
    e.mag = *m;
    e.depthKm = fx::number(r, dep).value_or(0.0);
    e.type = typ >= 0 && typ < r.size() ? r[typ].trimmed() : QString();
    e.place = place >= 0 && place < r.size() ? r[place].trimmed() : QString();
    e.id = id >= 0 && id < r.size() ? r[id].trimmed() : QString();
    out.push_back(e);
  }
  if (out.empty())
    throw std::runtime_error(
        (path + " holds no readable events").toStdString());
  std::sort(out.begin(), out.end(),
            [](const Event &a, const Event &b) { return a.epoch < b.epoch; });
  return out;
}

std::vector<Station> loadStations(const QString &path) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QStringList h = fx::splitCsv(rows.first());
  const int code = fx::column(
                h, {"Code", "station", "sta", "Istasyon", "İstasyon"}),
            lat = fx::column(h, {"Latitude", "lat", "Enlem"}),
            lon = fx::column(h, {"Longitude", "lon", "Boylam"}),
            net = fx::column(h, {"Network", "net", "Ag", "Ağ"}),
            hgt = fx::column(h, {"Height", "Elevation", "Yukseklik"}),
            prov = fx::column(h, {"Province", "Il", "İl"}),
            dist = fx::column(h, {"District", "Ilce", "İlçe"});
  if (code < 0 || lat < 0 || lon < 0)
    throw std::runtime_error(
        QStringLiteral(
            "%1 needs code, latitude and longitude columns; its columns are %2")
            .arg(path, h.join(", "))
            .toStdString());
  std::vector<Station> out;
  for (int i = 1; i < rows.size(); ++i) {
    const QStringList r = fx::splitCsv(rows[i]);
    if (code >= r.size())
      continue;
    const auto la = fx::number(r, lat), lo = fx::number(r, lon);
    if (!la || !lo || r[code].trimmed().isEmpty())
      continue;
    Station s;
    s.code = r[code].trimmed().toUpper();
    s.lat = *la;
    s.lon = *lo;
    s.network =
        net >= 0 && net < r.size() ? r[net].trimmed().toUpper() : QString();
    s.heightM = fx::number(r, hgt).value_or(0.0);
    s.province = prov >= 0 && prov < r.size() ? r[prov].trimmed() : QString();
    s.district = dist >= 0 && dist < r.size() ? r[dist].trimmed() : QString();
    out.push_back(s);
  }
  return out;
}

std::optional<Station> findStation(const std::vector<Station> &stations,
                                   const QString &code) {
  for (const Station &s : stations)
    if (s.code.compare(code.trimmed(), Qt::CaseInsensitive) == 0)
      return s;
  return std::nullopt;
}

std::vector<Event> selectEvents(const std::vector<Event> &events,
                                const EventQuery &q) {
  std::vector<Event> out;
  for (const Event &e : events) {
    if (q.startEpoch && e.epoch < *q.startEpoch)
      continue;
    if (q.endEpoch && e.epoch >= *q.endEpoch)
      continue;
    if (q.minMag && e.mag < *q.minMag)
      continue;
    if (q.maxMag && e.mag > *q.maxMag)
      continue;
    if (!q.placeContains.isEmpty() &&
        !e.place.contains(q.placeContains, Qt::CaseInsensitive))
      continue;
    if (q.centerLat && q.centerLon && q.radiusKm &&
        haversineKm(*q.centerLat, *q.centerLon, e.lat, e.lon) > *q.radiusKm)
      continue;
    out.push_back(e);
  }
  return out;
}

std::vector<StationDistance>
stationsWithin(const std::vector<Station> &stations, double lat, double lon,
               double radiusKm) {
  std::vector<StationDistance> out;
  for (const Station &s : stations) {
    const double km = haversineKm(lat, lon, s.lat, s.lon);
    if (km <= radiusKm)
      out.push_back({s, km, bearingDeg(lat, lon, s.lat, s.lon)});
  }
  std::sort(out.begin(), out.end(),
            [](const StationDistance &a, const StationDistance &b) {
              return a.km < b.km;
            });
  return out;
}

const std::vector<DistanceBand> &scapBands() {
  static const std::vector<DistanceBand> bands{
      {0, 100, 0.0, QStringLiteral("0-100 km")},
      {100, 300, 3.0, QStringLiteral("100-300 km (M3.0+)")},
      {300, 500, 5.0, QStringLiteral("300-500 km (M5.0+)")},
      {500, 1000, 6.0, QStringLiteral("500-1000 km (M6.0+)")}};
  return bands;
}

std::vector<EventAtStation>
eventsForStation(const std::vector<Event> &events, double lat, double lon,
                 const std::vector<DistanceBand> &bands) {
  std::vector<EventAtStation> out;
  for (const Event &e : events) {
    const double km = haversineKm(lat, lon, e.lat, e.lon);
    int band = -1;
    for (size_t b = 0; b < bands.size(); ++b) {
      const bool inside = b == 0 ? km <= bands[b].maxKm
                                 : km > bands[b].minKm && km <= bands[b].maxKm;
      if (inside && e.mag >= bands[b].minMag) {
        band = int(b);
        break;
      }
    }
    if (band >= 0)
      out.push_back({e, km, bearingDeg(lat, lon, e.lat, e.lon), band});
  }
  return out;
}

std::optional<EventAtStation>
mainEvent(const std::vector<EventAtStation> &events, double minMag) {
  std::optional<EventAtStation> best;
  for (const EventAtStation &e : events)
    if (e.event.mag >= minMag && (!best || e.event.mag > best->event.mag))
      best = e;
  return best;
}

QString eventFolderName(const Event &e) {
  const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
      qint64(std::llround(e.epoch * 1000.0)), QTimeZone::utc());
  QString place = e.place;
  place.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9ÇĞİÖŞÜçğıöşü]+")),
                QStringLiteral("_"));
  place.replace(QStringLiteral("("), QString())
      .replace(QStringLiteral(")"), QString());
  while (place.endsWith('_'))
    place.chop(1);
  return QStringLiteral("%1_M%2_%3")
      .arg(dt.toString(QStringLiteral("ddMMyyyy")))
      .arg(e.mag, 0, 'f', 1)
      .arg(place);
}

} // namespace skg::analysis
