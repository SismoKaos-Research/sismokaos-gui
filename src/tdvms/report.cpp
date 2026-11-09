#include "tdvms/report.h"

#include <algorithm>

namespace skg::tdvms {

const QStringList kStateOrder = {
    QStringLiteral("pending"),   QStringLiteral("claimed"),
    QStringLiteral("submitted"), QStringLiteral("fetched"),
    QStringLiteral("nodata"),    QStringLiteral("failed"),
    QStringLiteral("retired")};

std::optional<Turnaround> turnaround(const QList<Row> &rows) {
  QList<double> laps;
  for (const Row &r : rows) {
    const QDateTime sub = parseStamp(str(r, "submitted_at"));
    const QDateTime fet = parseStamp(str(r, "fetched_at"));
    if (!sub.isValid() || !fet.isValid())
      continue;
    const double minutes = sub.msecsTo(fet) / 60000.0;
    if (minutes < 0)
      continue; // inverted stamps: two unrelated events, not a turnaround
    laps << minutes;
  }
  if (laps.isEmpty())
    return std::nullopt;
  std::sort(laps.begin(), laps.end());
  return Turnaround{laps[laps.size() / 2], laps.first(), laps.last(),
                    int(laps.size())};
}

int StationCounts::total() const {
  int n = 0;
  for (int v : byState)
    n += v;
  return n;
}

bool StationCounts::neverReturnedData() const {
  return byState.value(QStringLiteral("fetched")) == 0 &&
         byState.value(QStringLiteral("nodata")) >= 8;
}

QList<StationCounts> perStation(const QList<Row> &rows) {
  QList<StationCounts> out;
  for (const Row &r : rows) {
    const QString s = str(r, "station");
    auto it = std::find_if(out.begin(), out.end(), [&](const StationCounts &c) {
      return c.station == s;
    });
    if (it == out.end()) {
      out.append(StationCounts{s, {}, 0});
      it = out.end() - 1;
    }
    it->byState[str(r, "state")] += 1;
    if (str(r, "state") == QLatin1String("fetched"))
      it->fetchedBytes += bytesOf(r);
  }
  return out;
}

namespace {
QString hours(std::optional<double> age) {
  return age ? QStringLiteral("%1 h").arg(*age / 3600, 0, 'f', 1)
             : QStringLiteral("unstamped");
}
QString minutes(std::optional<double> age) {
  return age ? QStringLiteral("%1 min").arg(*age / 60, 0, 'f', 0)
             : QStringLiteral("unstamped");
}
} // namespace

QStringList summarise(const Ledger &ledger, Pool *pool) {
  QStringList out;
  const QList<Row> rows = ledger.rows();
  if (rows.isEmpty()) {
    out << QStringLiteral("%1 is empty -- plan a station first")
               .arg(ledger.path());
    return out;
  }
  QMap<QString, QList<Row>> by;
  for (const Row &r : rows)
    by[str(r, "state")] << r;

  out << QStringLiteral("%1 chunk(s) in %2")
             .arg(rows.size())
             .arg(ledger.path());
  for (const QString &state : kStateOrder) {
    if (!by.contains(state))
      continue;
    qint64 bytes = 0;
    for (const Row &r : by[state])
      bytes += bytesOf(r);
    QString line =
        QStringLiteral("  %1 %2").arg(state, -10).arg(by[state].size(), 4);
    if (bytes)
      line += QStringLiteral("   %1 GB").arg(bytes / 1e9, 0, 'f', 1);
    out << line;
  }
  for (const Row &r : by.value(QStringLiteral("submitted")))
    out << QStringLiteral("  awaiting link: %1 (%2) -- %3")
               .arg(chunkId(r), str(r, "email"),
                    hours(ageSeconds(str(r, "submitted_at"))));
  for (const Row &r : by.value(QStringLiteral("claimed")))
    out << QStringLiteral("  STUCK in claimed: %1 (%2) -- %3; the run reclaims "
                          "this automatically")
               .arg(chunkId(r), str(r, "email"),
                    minutes(ageSeconds(str(r, "claimed_at"))));
  for (const Row &r : by.value(QStringLiteral("failed")))
    out << QStringLiteral("  FAILED: %1 -- %2").arg(chunkId(r), str(r, "note"));

  out << QString() << QStringLiteral("  per station:");
  for (const StationCounts &c : perStation(rows)) {
    QStringList detail;
    for (auto it = c.byState.begin(); it != c.byState.end(); ++it)
      detail << QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
    out << QStringLiteral("    %1 %2  %3 GB  %4%5")
               .arg(c.station, -6)
               .arg(c.total(), 3)
               .arg(c.fetchedBytes / 1e9, 5, 'f', 1)
               .arg(detail.join(' '),
                    c.neverReturnedData()
                        ? QStringLiteral("   <- never returned data")
                        : QString());
  }

  if (const auto lap = turnaround(rows))
    out << QString()
        << QStringLiteral("  turnaround  n=%1  median %2 min  range %3-%4 min")
               .arg(lap->n)
               .arg(lap->medianMin, 0, 'f', 0)
               .arg(lap->minMin, 0, 'f', 0)
               .arg(lap->maxMin, 0, 'f', 0);

  if (pool) {
    pool->refresh(ledger);
    out << QString()
        << QStringLiteral("  slots (%1 address(es)):").arg(pool->size());
    for (const Slot &s : pool->entries()) {
      QString where;
      if (s.holding) {
        const Row &r = *s.holding;
        QString stamp = str(r, "submitted_at");
        if (stamp.isEmpty())
          stamp = str(r, "claimed_at");
        where = QStringLiteral("%1 %2  %3")
                    .arg(str(r, "state"), chunkId(r), hours(ageSeconds(stamp)));
      } else if (s.remoteBusy()) {
        where = QStringLiteral("cooling down after a portal BUSY");
      } else {
        where = QStringLiteral("free");
      }
      out << QStringLiteral("    %1 %2").arg(s.email, -38).arg(where);
    }
    out << QStringLiteral("    %1 of %2 free")
               .arg(pool->free().size())
               .arg(pool->size());
  }
  return out;
}

} // namespace skg::tdvms
