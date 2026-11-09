#include "tdvms/ledger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>

namespace skg::tdvms {

const QStringList kInFlight = {QStringLiteral("claimed"),
                               QStringLiteral("submitted")};
const QStringList kTerminal = {
    QStringLiteral("fetched"), QStringLiteral("nodata"),
    QStringLiteral("failed"), QStringLiteral("retired")};
const QStringList kStates =
    QStringList{QStringLiteral("pending")} + kInFlight + kTerminal;

QString chunkId(const QString &station, const QString &start) {
  return station + ':' + start;
}

QString chunkId(const Row &row) {
  return chunkId(str(row, "station"), str(row, "start"));
}

QString nowStamp() {
  // Python: datetime.now(timezone.utc).isoformat(timespec="seconds")
  return QDateTime::currentDateTimeUtc().toString(
             QStringLiteral("yyyy-MM-ddTHH:mm:ss")) +
         QStringLiteral("+00:00");
}

QDateTime parseStamp(const QString &stamp) {
  if (stamp.isEmpty())
    return {};
  QDateTime t = QDateTime::fromString(stamp, Qt::ISODateWithMs);
  if (!t.isValid())
    t = QDateTime::fromString(stamp, Qt::ISODate);
  if (!t.isValid())
    return {};
  if (t.timeSpec() == Qt::LocalTime && !stamp.contains('+') &&
      !stamp.endsWith('Z') && stamp.count('-') <= 2)
    t.setTimeZone(QTimeZone::utc()); // naive stamps are UTC, as in Python
  return t;
}

std::optional<double> ageSeconds(const QString &stamp,
                                 const QDateTime &reference) {
  const QDateTime t = parseStamp(stamp);
  if (!t.isValid())
    return std::nullopt;
  const QDateTime ref =
      reference.isValid() ? reference : QDateTime::currentDateTimeUtc();
  return t.msecsTo(ref) / 1000.0;
}

QString str(const Row &row, const char *key) {
  const QJsonValue v = row.value(QLatin1String(key));
  return v.isString() ? v.toString() : QString();
}

int intOf(const Row &row, const char *key, int fallback) {
  const QJsonValue v = row.value(QLatin1String(key));
  return v.isDouble() ? v.toInt() : fallback;
}

qint64 bytesOf(const Row &row) {
  const QJsonValue v = row.value(QLatin1String("bytes"));
  return v.isDouble() ? static_cast<qint64>(v.toDouble()) : 0;
}

Ledger::Ledger(QString path) : path_(std::move(path)) {}

bool Ledger::exists() const { return QFileInfo::exists(path_); }

QList<Row> Ledger::rows() const {
  QFile f(path_);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  QList<Row> out;
  while (!f.atEnd()) {
    const QByteArray line = f.readLine().trimmed();
    if (line.isEmpty())
      continue;
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (doc.isObject())
      out << doc.object();
  }
  return out;
}

std::optional<Row> Ledger::byId(const QString &cid) const {
  for (const Row &r : rows())
    if (chunkId(r) == cid)
      return r;
  return std::nullopt;
}

QList<Row> Ledger::inState(const QStringList &states) const {
  QList<Row> out;
  for (const Row &r : rows())
    if (states.contains(str(r, "state")))
      out << r;
  return out;
}

std::optional<Row> Ledger::heldBy(const QString &email) const {
  const QString e = email.trimmed().toLower();
  for (const Row &r : rows())
    if (kInFlight.contains(str(r, "state")) && str(r, "email").toLower() == e)
      return r;
  return std::nullopt;
}

bool Ledger::complete() const {
  const QList<Row> all = rows();
  if (all.isEmpty())
    return false;
  return std::all_of(all.begin(), all.end(), [](const Row &r) {
    return kTerminal.contains(str(r, "state"));
  });
}

QStringList Ledger::stations() const {
  QStringList seen;
  for (const Row &r : rows()) {
    const QString s = str(r, "station");
    if (!seen.contains(s))
      seen << s;
  }
  return seen;
}

QString Ledger::lockPath() const {
  // Not "<ledger>.lock": the Python tool flocks that file and writes nothing
  // into it, and QLockFile would misread it. The two tools must not run on
  // one ledger at the same time anyway.
  return path_ + QStringLiteral(".qlock");
}

bool Ledger::writeRows(const QList<Row> &rows) const {
  QSaveFile f(path_);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  for (const Row &r : rows) {
    f.write(QJsonDocument(r).toJson(QJsonDocument::Compact));
    f.write("\n");
  }
  return f.commit();
}

bool Ledger::edit(const std::function<bool(QList<Row> &)> &fn) {
  QDir().mkpath(QFileInfo(path_).absolutePath());
  QLockFile lock(lockPath());
  lock.setStaleLockTime(60 * 1000);
  if (!lock.lock())
    return false;
  QList<Row> all = rows();
  if (!fn(all))
    return true; // nothing to write
  return writeRows(all);
}

std::optional<Row> Ledger::update(const QString &cid,
                                  const QJsonObject &changes) {
  std::optional<Row> result;
  edit([&](QList<Row> &all) {
    for (Row &r : all) {
      if (chunkId(r) != cid)
        continue;
      for (auto it = changes.begin(); it != changes.end(); ++it)
        r.insert(it.key(), it.value());
      result = r;
      return true;
    }
    return false;
  });
  return result;
}

Row Ledger::makeRow(const QString &station, const QString &start,
                    const QString &end) {
  Row r;
  r.insert("station", station);
  r.insert("start", start);
  r.insert("end", end);
  r.insert("state", QStringLiteral("pending"));
  for (const char *k : {"email", "url", "bytes", "note", "claimed_at",
                        "submitted_at", "fetched_at"})
    r.insert(QLatin1String(k), QJsonValue::Null);
  r.insert("attempts", 0);
  return r;
}

std::pair<std::optional<Row>, std::optional<Row>>
Ledger::claim(const QString &email,
              const std::optional<QStringList> &stations) {
  std::optional<Row> taken, blocker;
  const QString e = email.trimmed().toLower();
  edit([&](QList<Row> &all) {
    for (const Row &r : all)
      if (kInFlight.contains(str(r, "state")) &&
          str(r, "email").toLower() == e) {
        blocker = r;
        return false;
      }
    Row *best = nullptr;
    for (Row &r : all) {
      if (str(r, "state") != QLatin1String("pending"))
        continue;
      if (stations && !stations->contains(str(r, "station")))
        continue;
      if (!best ||
          std::make_pair(str(r, "start"), str(r, "station")) <
              std::make_pair(str(*best, "start"), str(*best, "station")))
        best = &r;
    }
    if (!best)
      return false;
    best->insert("state", QStringLiteral("claimed"));
    best->insert("email", email);
    best->insert("claimed_at", nowStamp());
    taken = *best;
    return true;
  });
  return {taken, blocker};
}

std::optional<Row> Ledger::release(const QString &cid, const QString &note) {
  QJsonObject changes;
  changes.insert("state", QStringLiteral("pending"));
  changes.insert("note", note.isNull() ? QJsonValue(QJsonValue::Null)
                                       : QJsonValue(note));
  for (const char *k :
       {"email", "claimed_at", "submitted_at", "fetched_at", "url", "bytes"})
    changes.insert(QLatin1String(k), QJsonValue::Null);
  return update(cid, changes);
}

} // namespace skg::tdvms
