#include "modules/study/runtext.h"

#include <QLocale>
#include <QTimeZone>

#include <cmath>

namespace skg {

QString RunWords::duration(qint64 s) {
  if (s < 0)
    return {};
  if (s < 60)
    return tr("%1 s").arg(s);
  const qint64 minutes = (s + 30) / 60;
  if (minutes < 60)
    return tr("%1 min").arg(minutes);
  const qint64 hours = minutes / 60, restMin = minutes % 60;
  if (hours < 24)
    return restMin ? tr("%1 h %2 min").arg(hours).arg(restMin)
                   : tr("%1 h").arg(hours);
  const qint64 days = hours / 24, restH = hours % 24;
  return restH ? tr("%1 d %2 h").arg(days).arg(restH) : tr("%1 d").arg(days);
}

QString RunWords::moment(const QDateTime &utc) {
  return utc.isValid()
             ? utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
             : QString();
}

QString RunWords::clock(const QDateTime &utc) {
  return utc.isValid() ? utc.toLocalTime().toString(QStringLiteral("HH:mm"))
                       : QString();
}

QString RunWords::zoneNote() {
  const int offset = QDateTime::currentDateTime().offsetFromUtc();
  const int minutes = std::abs(offset) / 60;
  const QString zone = QStringLiteral("UTC%1%2:%3")
                           .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                           .arg(minutes / 60, 2, 10, QLatin1Char('0'))
                           .arg(minutes % 60, 2, 10, QLatin1Char('0'));
  return tr("Times are this computer's local time (%1).").arg(zone);
}

QString RunWords::bytes(qint64 n) {
  const QLocale loc;
  if (n < 0)
    return {};
  const double v = double(n);
  if (v >= 1e9)
    return tr("%1 GB").arg(loc.toString(v / 1e9, 'f', v >= 1e10 ? 0 : 1));
  if (v >= 1e6)
    return tr("%1 MB").arg(loc.toString(v / 1e6, 'f', v >= 1e7 ? 0 : 1));
  if (v >= 1e3)
    return tr("%1 kB").arg(loc.toString(v / 1e3, 'f', 0));
  return tr("%1 B").arg(n);
}

} // namespace skg
