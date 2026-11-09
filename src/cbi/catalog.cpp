#include "cbi/catalog.h"

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace skg::cbi {

Catalog Catalog::filtered(const std::vector<bool> &keep) const {
  Catalog out;
  for (size_t i = 0; i < events.size() && i < keep.size(); ++i) {
    if (!keep[i])
      continue;
    out.events.push_back(events[i]);
    if (!originalMagnitude.empty())
      out.originalMagnitude.push_back(originalMagnitude[i]);
  }
  return out;
}

std::vector<double> Catalog::magnitudes() const {
  std::vector<double> m;
  m.reserve(events.size());
  for (const analysis::Event &e : events)
    m.push_back(e.mag);
  return m;
}

std::vector<double> Catalog::epochs() const {
  std::vector<double> t;
  t.reserve(events.size());
  for (const analysis::Event &e : events)
    t.push_back(e.epoch);
  return t;
}

Catalog loadCatalog(const QString &path) {
  Catalog c;
  c.events = analysis::loadEvents(path, 0.0);
  std::stable_sort(c.events.begin(), c.events.end(),
                   [](const analysis::Event &a, const analysis::Event &b) {
                     return a.epoch < b.epoch;
                   });
  return c;
}

QStringList describeCatalog(const Catalog &c, const QString &lang) {
  const bool tr = lang == QLatin1String("tr");
  if (c.empty())
    return {tr ? QStringLiteral("Toplam veri sayısı: 0")
               : QStringLiteral("Events: 0")};
  double lo = c.events.front().mag, hi = lo;
  for (const analysis::Event &e : c.events) {
    lo = std::min(lo, e.mag);
    hi = std::max(hi, e.mag);
  }
  const QString span = isoDateTime(c.events.front().epoch) +
                       QStringLiteral(" - ") +
                       isoDateTime(c.events.back().epoch);
  const QString mags = QStringLiteral("%1 - %2").arg(
      QString::number(lo, 'f', 2), QString::number(hi, 'f', 2));
  if (tr)
    return {QStringLiteral("Toplam veri sayısı: %1").arg(c.size()),
            QStringLiteral("Veri aralığı: %1").arg(span),
            QStringLiteral("Büyüklük aralığı: %1").arg(mags)};
  return {QStringLiteral("Events: %1").arg(c.size()),
          QStringLiteral("Time span: %1").arg(span),
          QStringLiteral("Magnitude span: %1").arg(mags)};
}

QDateTime toDateTime(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(epoch * 1000.0)),
                                        QTimeZone::utc());
}

double toEpoch(const QDateTime &dt) {
  return double(dt.toMSecsSinceEpoch()) / 1000.0;
}

double roundDecimals(double value, int decimals) {
  const double f = std::pow(10.0, decimals);
  return std::nearbyint(value * f) / f; // half to even, as numpy
}

double roundToStep(double value, double step) {
  return roundDecimals(std::nearbyint(value / step) * step, 2);
}

QString isoDate(double epoch) {
  return toDateTime(epoch).toString(QStringLiteral("yyyy-MM-dd"));
}

QString isoDateTime(double epoch) {
  return toDateTime(epoch).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace skg::cbi
