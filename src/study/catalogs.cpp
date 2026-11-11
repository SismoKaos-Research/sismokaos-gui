#include "study/catalogs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fx/csv.h"
#include "tdvms/catalog.h"
#include "text/text.h"

namespace skg::study {

namespace {

struct Text {
  Q_DECLARE_TR_FUNCTIONS(skg::study::CatalogText)
};

const double kKmPerDegree = 111.195;
const double kPi = 3.14159265358979323846;

QString csvField(const QString &s) {
  if (!s.contains(',') && !s.contains('"') && !s.contains('\n'))
    return s;
  QString q = s;
  q.replace('"', QLatin1String("\"\""));
  return '"' + q + '"';
}

QString isoUtc(const QDateTime &t) {
  return t.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

} // namespace

BuiltInCatalog builtInCatalog() {
  BuiltInCatalog b;
  const QList<tdvms::CatalogEvent> events = tdvms::loadEvents();
  b.count = int(events.size());
  bool first = true;
  for (const tdvms::CatalogEvent &e : events) {
    const QDate d = QDate::fromString(e.date(), Qt::ISODate);
    if (first || e.magnitude < b.minMagnitude)
      b.minMagnitude = e.magnitude;
    if (d.isValid()) {
      if (!b.first.isValid() || d < b.first)
        b.first = d;
      if (!b.last.isValid() || d > b.last)
        b.last = d;
    }
    first = false;
  }
  const QFileInfo user(tdvms::eventsPath());
  b.refreshed = user.exists();
  // A refreshed copy carries its own date; the shipped one is as old as its
  // last earthquake at least, which is the honest thing to say about it.
  b.updated = b.refreshed ? user.lastModified().date() : b.last;
  return b;
}

std::vector<analysis::Event> builtInEvents() {
  std::vector<analysis::Event> out;
  for (const tdvms::CatalogEvent &c : tdvms::loadEvents()) {
    // The list's times are UTC and carry no zone: read as UTC, not as local
    // time relabelled (fx/csv.h).
    const QDateTime t = fx::isoUtcDateTime(c.time);
    if (!t.isValid())
      continue;
    analysis::Event e;
    e.epoch = double(t.toSecsSinceEpoch());
    e.lat = c.latitude;
    e.lon = c.longitude;
    e.depthKm = c.depthKm;
    e.mag = c.magnitude;
    e.place = c.place;
    e.id = c.id;
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(),
            [](const analysis::Event &a, const analysis::Event &b) {
              return a.epoch < b.epoch;
            });
  return out;
}

std::vector<analysis::Event> pickingEvents(const Study &study) {
  if (study.catalog.isEmpty())
    return builtInEvents();
  return analysis::loadEvents(study.catalogPath(),
                              study.catalogUtcOffsetHours);
}

QList<EventService> eventServices() {
  return {{QStringLiteral("usgs"), QStringLiteral("USGS (ComCat)"),
           QStringLiteral("https://earthquake.usgs.gov/fdsnws/event/1/query")}};
}

CatalogQuery queryForStudy(const Study &study, double padKm) {
  CatalogQuery q;
  if (study.cases.isEmpty())
    return q;
  q.serviceUrl = eventServices().first().url;
  int windowMonths = 0;
  for (const Recipe &r : study.recipes)
    if (r.tool == QLatin1String("indicators"))
      windowMonths = std::max(
          windowMonths,
          r.settings.value(QStringLiteral("window_months")).toInt());
  if (windowMonths <= 0)
    windowMonths = 12;
  bool first = true;
  for (const Case &c : study.cases) {
    const double reach =
        (c.zone.kind == Zone::Kind::Circle ? c.zone.radiusKm : 300.0) + padKm;
    const double dLat = reach / kKmPerDegree;
    const double dLon =
        reach / (kKmPerDegree *
                 std::max(0.2, std::cos(c.lat * kPi / 180.0)));
    const QDateTime from = c.spanStart().addMonths(-windowMonths);
    const QDateTime to = c.time.addDays(1);
    if (first) {
      q.minLat = c.lat - dLat;
      q.maxLat = c.lat + dLat;
      q.minLon = c.lon - dLon;
      q.maxLon = c.lon + dLon;
      q.from = from;
      q.to = to;
      first = false;
    } else {
      q.minLat = std::min(q.minLat, c.lat - dLat);
      q.maxLat = std::max(q.maxLat, c.lat + dLat);
      q.minLon = std::min(q.minLon, c.lon - dLon);
      q.maxLon = std::max(q.maxLon, c.lon + dLon);
      q.from = std::min(q.from, from);
      q.to = std::max(q.to, to);
    }
  }
  q.minLat = std::max(-90.0, std::floor(q.minLat * 10.0) / 10.0);
  q.maxLat = std::min(90.0, std::ceil(q.maxLat * 10.0) / 10.0);
  q.minLon = std::max(-180.0, std::floor(q.minLon * 10.0) / 10.0);
  q.maxLon = std::min(180.0, std::ceil(q.maxLon * 10.0) / 10.0);
  q.from = QDateTime(q.from.toUTC().date(), QTime(0, 0), QTimeZone::UTC);
  q.to = QDateTime(q.to.toUTC().date().addDays(1), QTime(0, 0), QTimeZone::UTC);
  return q;
}

CatalogSummary summarize(const std::vector<analysis::Event> &events) {
  CatalogSummary c;
  if (events.empty())
    return c;
  std::vector<double> mags;
  mags.reserve(events.size());
  double lo = events.front().epoch, hi = lo;
  for (const analysis::Event &e : events) {
    mags.push_back(e.mag);
    lo = std::min(lo, e.epoch);
    hi = std::max(hi, e.epoch);
  }
  std::sort(mags.begin(), mags.end());
  c.ok = true;
  c.events = int(events.size());
  c.smallest = mags.front();
  c.floor = std::round(mags[size_t(double(mags.size() - 1) * 0.05)] * 10.0) /
            10.0;
  c.first = QDateTime::fromSecsSinceEpoch(qint64(std::floor(lo)),
                                          QTimeZone::UTC)
                .date();
  c.last = QDateTime::fromSecsSinceEpoch(qint64(std::floor(hi)),
                                         QTimeZone::UTC)
               .date();
  return c;
}

CatalogSummary catalogSummary(const QString &path, double utcOffsetHours) {
  struct Entry {
    qint64 size = -1;
    qint64 mtime = 0;
    double offset = 0;
    CatalogSummary summary;
  };
  static QMutex mutex;
  static QHash<QString, Entry> cache;
  const QFileInfo fi(path);
  if (path.isEmpty() || !fi.exists())
    return {};
  const qint64 size = fi.size(),
               mtime = fi.lastModified().toMSecsSinceEpoch();
  {
    const QMutexLocker lock(&mutex);
    const auto it = cache.constFind(path);
    if (it != cache.constEnd() && it->size == size && it->mtime == mtime &&
        it->offset == utcOffsetHours)
      return it->summary;
  }
  CatalogSummary c;
  try {
    c = summarize(analysis::loadEvents(path, utcOffsetHours));
  } catch (const std::exception &) {
    c = {};
  }
  const QMutexLocker lock(&mutex);
  cache.insert(path, Entry{size, mtime, utcOffsetHours, c});
  return c;
}

std::vector<analysis::Event> parseFdsnText(const QByteArray &body, int *bad) {
  // EventID|Time|Latitude|Longitude|Depth/km|Author|Catalog|Contributor|
  // ContributorID|MagType|Magnitude|MagAuthor|EventLocationName
  std::vector<analysis::Event> out;
  int unread = 0;
  for (const QByteArray &raw : body.split('\n')) {
    const QString line = QString::fromUtf8(raw).trimmed();
    if (line.isEmpty() || line.startsWith('#'))
      continue;
    const QStringList f = line.split('|');
    if (f.size() < 11) {
      ++unread;
      continue;
    }
    // FDSN text times are UTC, with or without a "Z".
    const QDateTime t = fx::isoUtcDateTime(f[1]);
    bool okLat, okLon, okMag;
    const double lat = f[2].toDouble(&okLat), lon = f[3].toDouble(&okLon),
                 mag = f[10].toDouble(&okMag);
    if (!t.isValid() || !okLat || !okLon || !okMag) {
      ++unread;
      continue;
    }
    analysis::Event e;
    e.epoch = double(t.toUTC().toMSecsSinceEpoch()) / 1000.0;
    e.lat = lat;
    e.lon = lon;
    e.depthKm = f[4].toDouble();
    e.mag = mag;
    e.type = f[9].trimmed();
    // The service writes "?" for letters it cannot encode (text/text.h).
    e.place = text::repairPlace(f.size() > 12 ? f[12].trimmed() : QString(),
                                lat, lon);
    e.id = f[0].trimmed();
    out.push_back(e);
  }
  if (bad)
    *bad = unread;
  return out;
}

FetchResult fetchCatalog(tdvms::Http &http, const CatalogQuery &query,
                         const QString &outDir, const QString &serviceId,
                         const FetchHooks &hooks) {
  if (!query.valid())
    throw std::runtime_error(
        Text::tr("the catalog request has no period or no area").toStdString());
  auto say = [&hooks](const QString &m) {
    if (hooks.log)
      hooks.log(m);
  };
  auto stopping = [&hooks] { return hooks.shouldStop && hooks.shouldStop(); };
  FetchResult res;
  std::vector<analysis::Event> all;
  QSet<QString> seen;
  const qint64 begin = query.from.toSecsSinceEpoch(),
               end = query.to.toSecsSinceEpoch();
  const qint64 kDay = 86400;
  qint64 piece = 365 * kDay; // a year at a time to start with
  qint64 at = begin;
  int unread = 0;
  while (at < end) {
    if (stopping()) {
      res.stopped = true;
      return res;
    }
    const qint64 until = std::min(end, at + piece);
    QUrl url(query.serviceUrl);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("text"));
    q.addQueryItem(QStringLiteral("starttime"),
                   isoUtc(QDateTime::fromSecsSinceEpoch(at, QTimeZone::UTC)));
    q.addQueryItem(QStringLiteral("endtime"),
                   isoUtc(QDateTime::fromSecsSinceEpoch(until, QTimeZone::UTC)));
    q.addQueryItem(QStringLiteral("minlatitude"),
                   QString::number(query.minLat, 'f', 2));
    q.addQueryItem(QStringLiteral("maxlatitude"),
                   QString::number(query.maxLat, 'f', 2));
    q.addQueryItem(QStringLiteral("minlongitude"),
                   QString::number(query.minLon, 'f', 2));
    q.addQueryItem(QStringLiteral("maxlongitude"),
                   QString::number(query.maxLon, 'f', 2));
    q.addQueryItem(QStringLiteral("minmagnitude"),
                   QString::number(query.minMagnitude, 'f', 1));
    q.addQueryItem(QStringLiteral("orderby"), QStringLiteral("time-asc"));
    url.setQuery(q);
    if (hooks.progress)
      hooks.progress(double(at - begin) / double(end - begin),
                     QStringLiteral("%1 … %2")
                         .arg(QDateTime::fromSecsSinceEpoch(at, QTimeZone::UTC)
                                  .date()
                                  .toString(Qt::ISODate),
                              QDateTime::fromSecsSinceEpoch(until,
                                                            QTimeZone::UTC)
                                  .date()
                                  .toString(Qt::ISODate)));
    const tdvms::HttpResult r = http.get(url, 120000);
    ++res.requests;
    const QString bodyStart = QString::fromUtf8(r.body.left(400)).toLower();
    // "Too many": HTTP 413, or the 400 some services answer with when a
    // request matches more events than they return at once.
    const bool tooMany =
        r.status == 413 ||
        (r.status == 400 && (bodyStart.contains(QLatin1String("exceed")) ||
                             bodyStart.contains(QLatin1String("limit"))));
    if (tooMany) {
      if (piece <= kDay)
        throw std::runtime_error(
            Text::tr("the service refuses even one day of this request as "
                     "too large: raise the smallest magnitude or shrink the "
                     "area")
                .toStdString());
      piece = std::max(kDay, piece / 2);
      say(Text::tr("too many events at once: asking for %1 days at a time")
              .arg(piece / kDay));
      continue;
    }
    if (r.status == 204 || (r.ok() && r.body.trimmed().isEmpty())) {
      at = until;
      continue;
    }
    if (!r.ok())
      throw std::runtime_error(
          Text::tr("the event service did not answer the request (%1): %2")
              .arg(r.error.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status)
                                     : r.error,
                   url.toString())
              .toStdString());
    int bad = 0;
    const std::vector<analysis::Event> got = parseFdsnText(r.body, &bad);
    if (got.empty() && bad > 0)
      throw std::runtime_error(
          Text::tr("the answer of the service is not in the FDSN text "
                   "format: %1")
              .arg(QString::fromUtf8(r.body.left(120)))
              .toStdString());
    unread += bad;
    for (const analysis::Event &e : got) {
      const QString key =
          e.id.isEmpty() ? QStringLiteral("%1/%2/%3")
                               .arg(e.epoch, 0, 'f', 2)
                               .arg(e.lat, 0, 'f', 3)
                               .arg(e.lon, 0, 'f', 3)
                         : e.id;
      if (seen.contains(key))
        continue;
      seen.insert(key);
      all.push_back(e);
    }
    say(Text::tr("%1 to %2: %3 events")
            .arg(QDateTime::fromSecsSinceEpoch(at, QTimeZone::UTC)
                     .date()
                     .toString(Qt::ISODate),
                 QDateTime::fromSecsSinceEpoch(until, QTimeZone::UTC)
                     .date()
                     .toString(Qt::ISODate))
            .arg(got.size()));
    at = until;
  }
  if (stopping()) {
    res.stopped = true;
    return res;
  }
  if (all.empty())
    throw std::runtime_error(
        Text::tr("the service has no earthquakes for this period, area and "
                 "smallest magnitude")
            .toStdString());
  std::sort(all.begin(), all.end(),
            [](const analysis::Event &a, const analysis::Event &b) {
              return a.epoch < b.epoch;
            });

  QByteArray csv =
      "Date,Latitude,Longitude,Depth,Type,Magnitude,Location,EventID\n";
  std::vector<double> mags;
  mags.reserve(all.size());
  for (const analysis::Event &e : all) {
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(
        qint64(std::llround(e.epoch * 1000.0)), QTimeZone::UTC);
    csv += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8\n")
               .arg(t.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")),
                    QString::number(e.lat, 'f', 4),
                    QString::number(e.lon, 'f', 4),
                    QString::number(e.depthKm, 'f', 1), csvField(e.type),
                    QString::number(e.mag, 'f', 1), csvField(e.place),
                    csvField(e.id))
               .toUtf8();
    mags.push_back(e.mag);
  }
  std::sort(mags.begin(), mags.end());
  res.events = int(all.size());
  res.minMagnitudeFound = mags.front();
  res.lowMagnitude =
      std::round(mags[std::min(mags.size() - 1, mags.size() / 100)] * 10.0) /
      10.0;

  if (!QDir().mkpath(outDir))
    throw std::runtime_error(
        Text::tr("could not create %1").arg(outDir).toStdString());
  const QString base =
      QStringLiteral("catalog_%1_%2_%3")
          .arg(serviceId.isEmpty() ? QStringLiteral("fdsn") : serviceId,
               query.from.toUTC().date().toString(QStringLiteral("yyyyMMdd")),
               query.to.toUTC().date().toString(QStringLiteral("yyyyMMdd")));
  res.csvPath = QDir(outDir).filePath(base + QStringLiteral(".csv"));
  res.provenancePath =
      QDir(outDir).filePath(base + QStringLiteral(".provenance.json"));
  QSaveFile out(res.csvPath);
  if (!out.open(QIODevice::WriteOnly) || out.write(csv) != csv.size() ||
      !out.commit())
    throw std::runtime_error(
        Text::tr("could not write %1").arg(res.csvPath).toStdString());

  QJsonObject p;
  p.insert(QStringLiteral("what"),
           QStringLiteral("earthquake catalog downloaded by the user of this "
                          "study from an FDSN event service; subject to that "
                          "service's terms; not part of the application"));
  p.insert(QStringLiteral("service"), query.serviceUrl);
  p.insert(QStringLiteral("service_id"), serviceId);
  QJsonObject qq;
  qq.insert(QStringLiteral("starttime"), isoUtc(query.from));
  qq.insert(QStringLiteral("endtime"), isoUtc(query.to));
  qq.insert(QStringLiteral("minlatitude"), query.minLat);
  qq.insert(QStringLiteral("maxlatitude"), query.maxLat);
  qq.insert(QStringLiteral("minlongitude"), query.minLon);
  qq.insert(QStringLiteral("maxlongitude"), query.maxLon);
  qq.insert(QStringLiteral("minmagnitude"), query.minMagnitude);
  qq.insert(QStringLiteral("format"), QStringLiteral("text"));
  p.insert(QStringLiteral("query"), qq);
  p.insert(QStringLiteral("retrieved_utc"),
           isoUtc(QDateTime::currentDateTimeUtc()));
  p.insert(QStringLiteral("requests"), res.requests);
  p.insert(QStringLiteral("events"), res.events);
  p.insert(QStringLiteral("lines_not_read"), unread);
  p.insert(QStringLiteral("smallest_magnitude"), res.minMagnitudeFound);
  p.insert(QStringLiteral("times"), QStringLiteral("UTC"));
  p.insert(QStringLiteral("encoding"), QStringLiteral("UTF-8"));
  p.insert(QStringLiteral("place_names"),
           QStringLiteral("letters the service replaced by \"?\" were put "
                          "back from GeoNames (CC BY 4.0) where exactly one "
                          "place near the earthquake fits"));
  p.insert(QStringLiteral("file"), QFileInfo(res.csvPath).fileName());
  p.insert(QStringLiteral("sha256"),
           QString::fromLatin1(
               QCryptographicHash::hash(csv, QCryptographicHash::Sha256)
                   .toHex()));
  p.insert(QStringLiteral("application"),
           QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                       QCoreApplication::applicationVersion()));
  QSaveFile prov(res.provenancePath);
  const QByteArray json = QJsonDocument(p).toJson(QJsonDocument::Indented);
  if (!prov.open(QIODevice::WriteOnly) || prov.write(json) != json.size() ||
      !prov.commit())
    throw std::runtime_error(
        Text::tr("could not write %1").arg(res.provenancePath).toStdString());
  say(Text::tr("%1 events in %2 requests, written to %3")
          .arg(res.events)
          .arg(res.requests)
          .arg(res.csvPath));
  if (hooks.progress)
    hooks.progress(1.0, QString());
  return res;
}

} // namespace skg::study
