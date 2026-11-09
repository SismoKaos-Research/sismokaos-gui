#include "tdvms/plan.h"

#include <QDateTime>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <stdexcept>

namespace skg::tdvms {

QString normaliseStation(const QString &code) {
  const int dot = code.lastIndexOf('.');
  return (dot >= 0 ? code.mid(dot + 1) : code).trimmed().toUpper();
}

namespace {
QDateTime naive(const QString &iso) {
  // Ledger times are naive local-less timestamps; treat them as UTC so day
  // arithmetic never crosses a DST change.
  QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
  if (!t.isValid())
    t = QDateTime::fromString(iso, QStringLiteral("yyyy-MM-dd"));
  if (!t.isValid())
    throw std::invalid_argument(("not an ISO date: " + iso).toStdString());
  t.setTimeZone(QTimeZone::utc());
  return t;
}

QString isoOf(const QDateTime &t) {
  // Python's datetime.isoformat() on a naive value: no zone suffix.
  return t.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}
} // namespace

QList<QPair<QString, QString>> planWindows(const QString &startIso,
                                           const QString &endIso, int chunkDays,
                                           int everyDays) {
  if (everyDays < 0)
    everyDays = chunkDays;
  if (chunkDays < 1)
    throw std::invalid_argument("a window is at least one day");
  if (everyDays < chunkDays)
    throw std::invalid_argument(
        QStringLiteral("%1-day windows every %2 days would overlap")
            .arg(chunkDays)
            .arg(everyDays)
            .toStdString());
  QDateTime t = naive(startIso);
  const QDateTime stop = naive(endIso);
  QList<QPair<QString, QString>> out;
  while (t < stop) {
    const QDateTime end = std::min(t.addDays(chunkDays), stop);
    out.append({isoOf(t), isoOf(end)});
    t = t.addDays(everyDays);
  }
  return out;
}

int enumerateChunks(Ledger &ledger, const QString &station,
                    const QString &startIso, const QString &endIso,
                    int chunkDays, int everyDays) {
  const auto spans = planWindows(startIso, endIso, chunkDays, everyDays);
  int added = 0;
  ledger.edit([&](QList<Row> &rows) {
    QSet<QString> have;
    for (const Row &r : rows)
      have.insert(chunkId(r));
    for (const auto &[s, e] : spans) {
      if (have.contains(chunkId(station, s)))
        continue;
      rows.append(Ledger::makeRow(station, s, e));
      ++added;
    }
    return added > 0;
  });
  return added;
}

} // namespace skg::tdvms
