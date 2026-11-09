#include "tdvms/history.h"

#include <QDir>
#include <QFileInfo>

#include "tdvms/ledger.h"

namespace skg::tdvms {

QString StationRecord::kind() const {
  if (fetched == 0 && (nodata >= 3 || retired))
    return QStringLiteral("dead");
  if (nodata >= 4 && answered() > 0 && double(nodata) / answered() >= 0.5)
    return QStringLiteral("patchy");
  return {};
}

QString StationRecord::warning() const {
  const QString k = kind();
  if (k.isEmpty())
    return {};
  QStringList where = campaigns.values();
  where.sort();
  const QString span =
      silentFrom.isEmpty()
          ? QString()
          : QStringLiteral(" for %1..%2").arg(silentFrom, silentTo);
  if (k == QLatin1String("dead")) {
    const QString what =
        nodata
            ? QStringLiteral("%1 no-data answer(s)%2 and nothing ever fetched")
                  .arg(nodata)
                  .arg(span)
            : QStringLiteral("retired without ever returning data");
    return QStringLiteral("%1: %2 (%3)").arg(code, what, where.join(", "));
  }
  return QStringLiteral("%1: no data for %2 of %3 windows answered%4 (%5)")
      .arg(code)
      .arg(nodata)
      .arg(answered())
      .arg(span, where.join(", "));
}

QMap<QString, StationRecord> gatherHistory(const QList<Campaign> &campaigns) {
  QMap<QString, StationRecord> out;
  for (const Campaign &c : campaigns) {
    for (const Row &r : Ledger(c.ledgerPath).rows()) {
      const QString state = str(r, "state");
      if (!kTerminal.contains(state))
        continue;
      StationRecord &rec = out[str(r, "station")];
      rec.code = str(r, "station");
      if (state == QLatin1String("fetched")) {
        ++rec.fetched;
      } else if (state == QLatin1String("nodata")) {
        ++rec.nodata;
        const QString start = str(r, "start").left(10);
        const QString endRaw = str(r, "end");
        const QString end =
            (endRaw.isEmpty() ? str(r, "start") : endRaw).left(10);
        if (rec.silentFrom.isEmpty() || start < rec.silentFrom)
          rec.silentFrom = start;
        if (rec.silentTo.isEmpty() || end > rec.silentTo)
          rec.silentTo = end;
      } else if (state == QLatin1String("failed")) {
        ++rec.failed;
      } else {
        ++rec.retired;
      }
      rec.campaigns.insert(c.name);
    }
  }
  return out;
}

QList<Campaign> campaignsAround(const QString &ledgerPath) {
  const QFileInfo fi(ledgerPath);
  QStringList roots{fi.absolutePath(), QDir::currentPath()};
  const QDir parent = fi.dir();
  if (QFileInfo(parent.absolutePath()).dir().dirName() ==
      QLatin1String(kCampaignsDir))
    roots << QFileInfo(QFileInfo(parent.absolutePath()).absolutePath())
                 .absolutePath();
  QSet<QString> seen;
  QList<Campaign> out;
  for (const QString &root : roots)
    for (const Campaign &c : discoverCampaigns(root)) {
      const QString key = QFileInfo(c.ledgerPath).canonicalFilePath();
      if (!seen.contains(key)) {
        seen.insert(key);
        out << c;
      }
    }
  return out;
}

} // namespace skg::tdvms
