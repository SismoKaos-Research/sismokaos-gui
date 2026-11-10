#include "tdvms/guide.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <stdexcept>

namespace skg::tdvms {

const char *const kStudyDownloadsDir = "downloads";
const char *const kStudyDownloadName = "waveforms";

namespace {

const CatalogStation *find(const QString &code,
                           const QList<CatalogStation> &stations) {
  // A requestable entry wins over one without an instrument of the same code.
  const CatalogStation *any = nullptr;
  for (const CatalogStation &s : stations)
    if (s.code.compare(code, Qt::CaseInsensitive) == 0) {
      if (s.requestable())
        return &s;
      if (!any)
        any = &s;
    }
  return any;
}

QDate dayOf(const QString &iso) {
  return QDate::fromString(iso.left(10), Qt::ISODate);
}

// The days a row's window touches: [start, end), end exclusive unless it
// carries a time of day.
bool covers(const Row &row, const QDate &day) {
  const QDate from = dayOf(str(row, "start"));
  QDate to = dayOf(str(row, "end"));
  if (!from.isValid() || !to.isValid())
    return false;
  const QString end = str(row, "end");
  if (end.size() > 10 && end.mid(11, 8) != QLatin1String("00:00:00"))
    to = to.addDays(1);
  return day >= from && day < to;
}

QString isoOf(const QDate &d) {
  return d.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("T00:00:00");
}

} // namespace

Source defaultSource(const QString &code,
                     const QList<CatalogStation> &portalStations) {
  const CatalogStation *s = find(code, portalStations);
  return s && s->requestable() ? Source::Portal : Source::Fdsn;
}

QString portalNetwork(const QString &code,
                      const QList<CatalogStation> &portalStations) {
  const CatalogStation *s = find(code, portalStations);
  return s ? s->network : QString();
}

QString studyDownloadsRoot(const QString &studyDir) {
  return QDir(studyDir).filePath(QLatin1String(kStudyDownloadsDir));
}

Campaign studyDownload(const QString &studyDir, bool create) {
  const QString root = studyDownloadsRoot(studyDir);
  const QString home = QDir(root).filePath(QLatin1String(kCampaignsDir) +
                                           QLatin1Char('/') +
                                           QLatin1String(kStudyDownloadName));
  const QString ledger = QDir(home).filePath(QStringLiteral("ledger.jsonl"));
  if (!QFileInfo::exists(ledger)) {
    if (!create)
      return {};
    if (!QDir().mkpath(QDir(home).filePath(QStringLiteral("raw"))))
      throw std::runtime_error(
          ("cannot create " + QDir::toNativeSeparators(home)).toStdString());
    // A download is found by its list of requests; start it empty.
    QFile f(ledger);
    if (!f.open(QIODevice::WriteOnly))
      throw std::runtime_error(
          ("cannot create " + QDir::toNativeSeparators(ledger)).toStdString());
  }
  return campaignForLedger(ledger);
}

QList<QDate> daysNotRequested(const QList<QDate> &days, const QList<Row> &rows,
                              const QString &station) {
  static const QSet<QString> dead = {QStringLiteral("failed"),
                                     QStringLiteral("retired")};
  QList<Row> live;
  for (const Row &r : rows)
    if (str(r, "station").compare(station, Qt::CaseInsensitive) == 0 &&
        !dead.contains(str(r, "state")))
      live << r;
  QList<QDate> out;
  for (const QDate &d : days) {
    const bool taken = std::any_of(live.begin(), live.end(), [&d](const Row &r) {
      return covers(r, d);
    });
    if (!taken)
      out << d;
  }
  return out;
}

QList<QPair<QString, QString>> windowsForDays(const QDate &gridStart,
                                              const QDate &gridEnd,
                                              const QList<QDate> &days,
                                              int chunkDays) {
  QList<QPair<QString, QString>> out;
  if (!gridStart.isValid() || !gridEnd.isValid() || chunkDays < 1)
    return out;
  for (QDate from = gridStart; from < gridEnd; from = from.addDays(chunkDays)) {
    const QDate to = std::min(from.addDays(chunkDays), gridEnd);
    const bool wanted = std::any_of(days.begin(), days.end(),
                                    [&](const QDate &d) {
                                      return d >= from && d < to;
                                    });
    if (wanted)
      out.append({isoOf(from), isoOf(to)});
  }
  return out;
}

RequestProgress progressOf(const QList<Row> &rows, const QString &station) {
  RequestProgress p;
  for (const Row &r : rows) {
    if (str(r, "station").compare(station, Qt::CaseInsensitive) != 0)
      continue;
    const QString state = str(r, "state");
    if (state == QLatin1String("retired"))
      continue;
    ++p.total;
    if (state == QLatin1String("pending"))
      ++p.waiting;
    else if (state == QLatin1String("claimed") ||
             state == QLatin1String("submitted"))
      ++p.underWay;
    else if (state == QLatin1String("fetched")) {
      ++p.fetched;
      p.bytes += bytesOf(r);
    } else if (state == QLatin1String("nodata"))
      ++p.noData;
    else if (state == QLatin1String("failed")) {
      ++p.failed;
      const QString note = str(r, "note");
      p.failures << str(r, "start").left(10) +
                        (note.isEmpty() ? QString()
                                        : QStringLiteral(": ") + note);
    }
  }
  p.failures.sort();
  return p;
}

QStringList stationsOf(const QList<Row> &rows) {
  QStringList out;
  for (const Row &r : rows) {
    const QString s = str(r, "station");
    if (!s.isEmpty() && !out.contains(s))
      out << s;
  }
  return out;
}

qint64 estimateBytes(const QList<Row> &rows, int daysToCome,
                     qint64 fallbackPerDay) {
  qint64 bytes = 0;
  qint64 days = 0;
  for (const Row &r : rows) {
    if (str(r, "state") != QLatin1String("fetched") || bytesOf(r) <= 0)
      continue;
    const QDate from = dayOf(str(r, "start")), to = dayOf(str(r, "end"));
    if (!from.isValid() || !to.isValid() || to <= from)
      continue;
    bytes += bytesOf(r);
    days += from.daysTo(to);
  }
  const qint64 perDay = days > 0 ? bytes / days : fallbackPerDay;
  return perDay * std::max(0, daysToCome);
}

int retryFailed(Ledger &ledger, const QString &station) {
  int n = 0;
  for (const Row &r : ledger.inState({QStringLiteral("failed")}))
    if (str(r, "station").compare(station, Qt::CaseInsensitive) == 0) {
      ledger.release(chunkId(r));
      ++n;
    }
  return n;
}

} // namespace skg::tdvms
