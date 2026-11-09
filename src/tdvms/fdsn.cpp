#include "tdvms/fdsn.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <QTimeZone>
#include <QUrlQuery>

#include <algorithm>

#include "tdvms/coverage.h"
#include <cmath>

namespace skg::tdvms {

const QString kKoeri = QStringLiteral("http://eida.koeri.boun.edu.tr");
const QString kAdoptedNote = QStringLiteral("on disk before this pull");

QString fdsnStatePath(const Campaign &c) {
  const QFileInfo fi(c.ledgerPath);
  if (fi.fileName() == QLatin1String("ledger.jsonl"))
    return fi.dir().filePath(QStringLiteral("fdsn.jsonl"));
  return fi.dir().filePath(c.name + QStringLiteral("_fdsn.jsonl"));
}

QString fdsnDefaultOut(const Campaign &c) {
  const QFileInfo fi(c.ledgerPath);
  if (fi.fileName() == QLatin1String("ledger.jsonl"))
    return fi.dir().filePath(QStringLiteral("fdsn_raw"));
  return fi.dir().filePath(c.name + QStringLiteral("_fdsn_raw"));
}

QString fdsnFileName(const QString &network, const QString &station,
                     const QDate &day, const QString &channels) {
  const QDate next = day.addDays(1);
  return QStringLiteral("%1_%2_%3_000000_%4_000000_%5.mseed")
      .arg(network, station, day.toString(QStringLiteral("ddMMyyyy")),
           next.toString(QStringLiteral("ddMMyyyy")), channels.left(2));
}

QMap<QString, double> dayHours(const QByteArray &data, const QDate &day) {
  const double lo =
      QDateTime(day, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch();
  QMap<QString, double> out;
  const ScanResult scanned = scanBytes(data);
  for (auto it = scanned.begin(); it != scanned.end(); ++it) {
    const double hours =
        spanLength(clipSpans(it->spans, lo, lo + 86400)) / 3600;
    out.insert(it.key().section('.', -1), std::round(hours * 100) / 100);
  }
  return out;
}

QList<QDate> daysBetween(const QDate &start, const QDate &end) {
  QList<QDate> out;
  for (QDate d = start; d < end; d = d.addDays(1))
    out << d;
  return out;
}

namespace {
QString dayStart(const QDate &d) {
  return d.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("T00:00:00");
}
} // namespace

int fdsnPlan(Ledger &ledger, const QString &station, const QList<QDate> &days,
             const QString &network, const QString &channels,
             const QString &service) {
  QList<QDate> sorted = days;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  int added = 0;
  ledger.edit([&](QList<Row> &rows) {
    QSet<QString> have;
    for (const Row &r : rows)
      have.insert(chunkId(r));
    for (const QDate &d : sorted) {
      const QString start = dayStart(d);
      if (have.contains(chunkId(station, start)))
        continue;
      Row row = Ledger::makeRow(station, start, dayStart(d.addDays(1)));
      row.insert("network", network);
      row.insert("channels", channels);
      row.insert("service", service);
      row.insert("hours", QJsonValue::Null);
      row.insert("path", QJsonValue::Null);
      rows.append(row);
      ++added;
    }
    return added > 0;
  });
  return added;
}

namespace {
QString dayFile(const QString &outDir, const Row &r) {
  const QDate day = QDate::fromString(str(r, "start").left(10), Qt::ISODate);
  return QDir(outDir).filePath(str(r, "station") + '/' +
                               fdsnFileName(str(r, "network"),
                                            str(r, "station"), day,
                                            str(r, "channels")));
}

QJsonObject hoursJson(const QMap<QString, double> &hours) {
  QJsonObject o;
  for (auto it = hours.begin(); it != hours.end(); ++it)
    o.insert(it.key(), it.value());
  return o;
}
} // namespace

int fdsnAdopt(Ledger &ledger, const QString &outDir) {
  int adopted = 0;
  for (const Row &r : ledger.inState({QStringLiteral("pending")})) {
    const QString path = dayFile(outDir, r);
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
      continue;
    const QByteArray data = f.readAll();
    const QDate day = QDate::fromString(str(r, "start").left(10), Qt::ISODate);
    ledger.update(chunkId(r), {{"state", "fetched"},
                               {"path", path},
                               {"bytes", double(data.size())},
                               {"hours", hoursJson(dayHours(data, day))},
                               {"fetched_at", nowStamp()},
                               {"note", kAdoptedNote}});
    ++adopted;
  }
  return adopted;
}

int fdsnReclaim(Ledger &ledger) {
  int n = 0;
  for (const Row &r : ledger.inState({QStringLiteral("claimed")})) {
    ledger.update(chunkId(r),
                  {{"state", "pending"}, {"claimed_at", QJsonValue::Null}});
    ++n;
  }
  return n;
}

int fdsnRetryFailed(Ledger &ledger, const QString &station) {
  int n = 0;
  for (const Row &r : ledger.inState({QStringLiteral("failed")}))
    if (str(r, "station") == station) {
      ledger.update(chunkId(r),
                    {{"state", "pending"}, {"note", QJsonValue::Null}});
      ++n;
    }
  return n;
}

QMap<QString, int> fdsnWork(Ledger &ledger, const QString &outDir, Http &http,
                            const FdsnOptions &opt, const Log &log,
                            const std::function<bool()> &shouldStop,
                            const std::function<void(int, int)> &progress) {
  auto say = [&](const QString &m) {
    if (log)
      log(m);
  };
  QMap<QString, int> counts;
  const int total = ledger.inState({QStringLiteral("pending")}).size();
  int done = 0;
  while (!(shouldStop && shouldStop())) {
    QList<Row> todo = ledger.inState({QStringLiteral("pending")});
    if (todo.isEmpty())
      break;
    std::sort(todo.begin(), todo.end(), [](const Row &a, const Row &b) {
      return std::make_pair(str(a, "start"), str(a, "station")) <
             std::make_pair(str(b, "start"), str(b, "station"));
    });
    const Row r = todo.first();
    const QString cid = chunkId(r);
    const QDate day = QDate::fromString(str(r, "start").left(10), Qt::ISODate);
    ledger.update(cid, {{"state", "claimed"},
                        {"claimed_at", nowStamp()},
                        {"attempts", intOf(r, "attempts") + 1}});
    if (progress)
      progress(done, total);

    QUrl url(str(r, "service").trimmed() +
             QStringLiteral("/fdsnws/dataselect/1/query"));
    while (url.toString().contains(QLatin1String("//fdsnws")))
      url = QUrl(url.toString().replace(QLatin1String("//fdsnws"),
                                        QLatin1String("/fdsnws")));
    QUrlQuery q;
    q.addQueryItem("network", str(r, "network"));
    q.addQueryItem("station", str(r, "station"));
    q.addQueryItem("location", "*");
    q.addQueryItem("channel", str(r, "channels"));
    q.addQueryItem("starttime", str(r, "start"));
    q.addQueryItem("endtime", str(r, "end"));
    q.addQueryItem("nodata", "404");
    url.setQuery(q);

    QByteArray data;
    QString error;
    bool noData = false;
    for (int attempt = 1; attempt <= opt.retries; ++attempt) {
      if (shouldStop && shouldStop())
        break;
      const HttpResult res = http.get(url, opt.timeoutMs);
      if (res.status == 204 || res.status == 404) {
        noData = true;
        error.clear();
        break;
      }
      if (res.ok()) {
        data = res.body;
        error.clear();
        if (data.isEmpty())
          noData = true;
        break;
      }
      error = (res.error.isEmpty() ? QStringLiteral("HTTP %1").arg(res.status)
                                   : res.error)
                  .left(200);
      say(QStringLiteral("  %1  %2, try %3/%4")
              .arg(cid, error)
              .arg(attempt)
              .arg(opt.retries));
      if (attempt < opt.retries) {
        const qint64 until =
            QDateTime::currentMSecsSinceEpoch() + qint64(opt.pauseMs) * attempt;
        while (QDateTime::currentMSecsSinceEpoch() < until &&
               !(shouldStop && shouldStop()))
          QThread::msleep(200);
      }
    }
    if (shouldStop && shouldStop() && data.isEmpty() && !noData) {
      ledger.update(cid,
                    {{"state", "pending"}, {"claimed_at", QJsonValue::Null}});
      break;
    }
    if (!error.isEmpty()) {
      ledger.update(
          cid,
          {{"state", "failed"}, {"fetched_at", nowStamp()}, {"note", error}});
      say(QStringLiteral("[FAILED] %1  %2").arg(cid, error));
    } else if (noData) {
      ledger.update(cid, {{"state", "nodata"},
                          {"fetched_at", nowStamp()},
                          {"note", "no data at source"}});
      say(QStringLiteral("[NODATA] %1").arg(cid));
    } else {
      const QString path = dayFile(outDir, r);
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile f(path);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(data);
        f.commit();
      }
      const auto hours = dayHours(data, day);
      ledger.update(cid, {{"state", "fetched"},
                          {"fetched_at", nowStamp()},
                          {"path", path},
                          {"bytes", double(data.size())},
                          {"hours", hoursJson(hours)},
                          {"note", QJsonValue::Null}});
      QStringList bits;
      for (auto it = hours.begin(); it != hours.end(); ++it)
        bits << QStringLiteral("%1 %2 h").arg(it.key()).arg(it.value(), 0, 'f',
                                                            1);
      say(QStringLiteral("[OK] %1  %2").arg(cid, bits.join("  ")));
    }
    const auto now = ledger.byId(cid);
    if (now)
      counts[str(*now, "state")] += 1;
    ++done;
  }
  if (progress)
    progress(done, total);
  return counts;
}

} // namespace skg::tdvms
