// The campaign's only state: one JSON line per (station, window) chunk.
// A port of tdvms/ledger.py. A chunk is identified by "STATION:START", never
// by date alone; every write is a read-modify-write under a lock; the file is
// replaced atomically. Rows are kept as JSON objects so fields the GUI does not
// know (the FDSN ledger's network/channels/hours/path) survive a round trip.
//
// States:  pending -> claimed -> submitted -> fetched | nodata | failed
//          claimed/submitted -> pending (reclaimed), pending -> retired
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

namespace skg::tdvms {

using Row = QJsonObject;

extern const QStringList kInFlight; // claimed, submitted
extern const QStringList kTerminal; // fetched, nodata, failed, retired
extern const QStringList kStates;

// "ELBA:2025-09-09T00:00:00"
QString chunkId(const QString &station, const QString &start);
QString chunkId(const Row &row);
// UTC, seconds resolution, "+00:00" suffix: the stamp format the Python tool
// writes, so both can read one ledger.
QString nowStamp();
// Seconds since an ISO stamp; nullopt when missing or unparseable (an unknown
// age must not look like a fresh one).
std::optional<double> ageSeconds(const QString &stamp,
                                 const QDateTime &reference = {});
QDateTime parseStamp(const QString &stamp);
// A zone-less time, "yyyy-MM-dd" or "yyyy-MM-ddTHH:mm:ss[.zzz]", read as
// UTC. It is put together from its date and its time of day: reading it as
// local time first and relabelling it shifts every time that falls in the
// hour a local clock skips when summer time begins. Invalid when the text is
// not such a time.
QDateTime naiveUtc(const QString &text);
// The same from separate date and time texts in the given formats
// ("ddMMyyyy", "HHmmss").
QDateTime naiveUtc(const QString &date, const QString &dateFormat,
                   const QString &time, const QString &timeFormat);

// Field helpers: JSON null and a missing key both read as empty.
QString str(const Row &row, const char *key);
int intOf(const Row &row, const char *key, int fallback = 0);
qint64 bytesOf(const Row &row);

class Ledger {
public:
  explicit Ledger(QString path);

  const QString &path() const { return path_; }
  bool exists() const;

  // --- queries (no lock) ---------------------------------------------------
  QList<Row> rows() const; // file order; empty when the file is missing
  std::optional<Row> byId(const QString &cid) const;
  QList<Row> inState(const QStringList &states) const;
  std::optional<Row> heldBy(const QString &email) const;
  bool complete() const; // every chunk terminal; an empty ledger is not
  QStringList stations() const;

  // --- mutation (each takes the lock) --------------------------------------
  // Applies `changes` to one chunk against the current on-disk rows; nullopt
  // when the chunk is not there. A QJsonValue::Null in `changes` clears.
  std::optional<Row> update(const QString &cid, const QJsonObject &changes);
  // Takes the OLDEST pending chunk (by start, then station) for `email`.
  // Returns {row, blocker}: blocker is the chunk the address already holds,
  // in which case nothing was claimed; both empty when nothing is pending.
  std::pair<std::optional<Row>, std::optional<Row>>
  claim(const QString &email, const std::optional<QStringList> &stations = {});
  // Back to pending with every trace of the attempt cleared (stamps, url,
  // bytes, email); `note` may be null.
  std::optional<Row> release(const QString &cid,
                             const QString &note = QString());
  // Runs `edit` on the current rows under the lock; writes them back when it
  // returns true. For plan/adopt/fdsn.
  bool edit(const std::function<bool(QList<Row> &)> &edit);

  static Row makeRow(const QString &station, const QString &start,
                     const QString &end);

private:
  bool writeRows(const QList<Row> &rows) const;
  QString lockPath() const;

  QString path_;
};

} // namespace skg::tdvms
