#include "analysis/series.h"

#include <QDateTime>
#include <QSet>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "analysis/label.h"

namespace skg::analysis {

namespace {
QDate dayOf(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(epoch * 1000.0)),
                                        QTimeZone::utc())
      .date();
}
double dayStart(const QDate &d) {
  return double(
             QDateTime(d, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch()) /
         1000.0;
}
} // namespace

MetricSeries metricSeries(const fx::NpyTable &table, const QString &name,
                          double winSec) {
  int col = -1;
  for (int i = 0; i < table.fields.size(); ++i)
    if (table.fields[i].name == name)
      col = i;
  if (col < 0 || table.fields[col].dtype.contains('U'))
    throw std::invalid_argument(
        ("no numeric column named " + name).toStdString());
  MetricSeries s;
  s.column = name;
  for (int r = 0; r < table.rows; ++r) {
    const auto span = windowSpan(table, r, winSec);
    if (!span)
      continue;
    s.epoch.push_back(span->end);
    s.value.push_back(table.numeric[col][size_t(r)]);
  }
  return s;
}

QStringList plottableColumns(const fx::NpyTable &table) {
  static const QSet<QString> skip{"Zaman_Dk", "Deprem_Sayisi", "Etiket"};
  QStringList out;
  for (const fx::NpyField &f : table.fields)
    if (!f.dtype.contains('U') && !skip.contains(f.name) &&
        !f.name.endsWith(QLatin1String("_INTERP_FRAC")))
      out << f.name;
  return out;
}

std::vector<QDate> daysOf(const MetricSeries &s) {
  std::vector<QDate> days;
  for (double e : s.epoch) {
    // The window ends at e; the window belongs to the day it ends in, except
    // an end exactly at midnight, which closes the previous day.
    const QDate d = dayOf(e - 1e-3);
    if (days.empty() || days.back() != d)
      days.push_back(d);
  }
  std::sort(days.begin(), days.end());
  days.erase(std::unique(days.begin(), days.end()), days.end());
  return days;
}

DaySlice daySlice(const MetricSeries &s, const QDate &day) {
  DaySlice out;
  out.day = day;
  const double t0 = dayStart(day);
  for (size_t i = 0; i < s.epoch.size(); ++i) {
    const double m = (s.epoch[i] - t0) / 60.0;
    if (m <= 0 || m > 1440 || std::isnan(s.value[i]))
      continue;
    out.minutes.push_back(m);
    out.value.push_back(s.value[i]);
  }
  return out;
}

Areas areasOf(const DaySlice &d) {
  Areas a;
  for (size_t i = 1; i < d.minutes.size(); ++i) {
    const double dt = d.minutes[i] - d.minutes[i - 1];
    const double p0 = std::max(0.0, d.value[i - 1]),
                 p1 = std::max(0.0, d.value[i]);
    const double n0 = std::min(0.0, d.value[i - 1]),
                 n1 = std::min(0.0, d.value[i]);
    a.positive += 0.5 * (p0 + p1) * dt;
    a.negative += 0.5 * (n0 + n1) * dt;
  }
  return a;
}

std::vector<QDate> centredDays(const QDate &eventDay,
                               const std::vector<QDate> &available,
                               int windowDays) {
  std::vector<QDate> out;
  if (available.empty() || windowDays < 1)
    return out;
  const int before = (windowDays - 1) / 2, after = windowDays - 1 - before;
  const QDate start = std::max(eventDay.addDays(-before), available.front());
  const QDate end = std::min(eventDay.addDays(after), available.back());
  for (QDate d = start; d <= end; d = d.addDays(1))
    if (std::binary_search(available.begin(), available.end(), d))
      out.push_back(d);
  return out;
}

std::optional<YRange> scapRange(const QString &column) {
  if (column == QLatin1String("Rosenstein_LyE_Short") ||
      column == QLatin1String("Rosenstein_LyE_Long"))
    return YRange{-5.0, 5.0};
  if (column == QLatin1String("Wolf_LyE"))
    return YRange{-0.5, 1.0};
  if (column == QLatin1String("Samp_Ent"))
    return YRange{0.0, 1.5};
  if (column == QLatin1String("CorDim"))
    return YRange{0.0, 5.0};
  return std::nullopt;
}

YRange autoRange(const MetricSeries &s) {
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (double v : s.value)
    if (std::isfinite(v)) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  if (!std::isfinite(lo))
    return {0, 1};
  if (lo >= 0) {
    const double top = hi > 0 ? hi * 1.1 : 1.0;
    return {0.0, top};
  }
  const double m = std::max(std::abs(lo), std::abs(hi)) * 1.1;
  return {-m, m == 0 ? 1.0 : m};
}

QString metricTitle(const QString &column, const QString &lang) {
  const bool tr = lang == QLatin1String("tr");
  if (column == QLatin1String("Rosenstein_LyE_Short"))
    return tr ? QStringLiteral("Rosenstein Lyapunov Üssü (Kısa)")
              : QStringLiteral("Rosenstein Lyapunov Exponent (Short)");
  if (column == QLatin1String("Rosenstein_LyE_Long"))
    return tr ? QStringLiteral("Rosenstein Lyapunov Üssü (Uzun)")
              : QStringLiteral("Rosenstein Lyapunov Exponent (Long)");
  if (column == QLatin1String("Wolf_LyE"))
    return tr ? QStringLiteral("Wolf Lyapunov Üssü")
              : QStringLiteral("Wolf Lyapunov Exponent");
  if (column == QLatin1String("Samp_Ent"))
    return tr ? QStringLiteral("Örnek Entropisi")
              : QStringLiteral("Sample Entropy");
  if (column == QLatin1String("CorDim"))
    return tr ? QStringLiteral("Korelasyon Boyutu")
              : QStringLiteral("Correlation Dimension");
  return column;
}

QString monthName(int month, const QString &lang) {
  static const QStringList tr{"Ocak",  "Şubat",   "Mart",   "Nisan",
                              "Mayıs", "Haziran", "Temmuz", "Ağustos",
                              "Eylül", "Ekim",    "Kasım",  "Aralık"};
  static const QStringList en{"January",   "February", "March",    "April",
                              "May",       "June",     "July",     "August",
                              "September", "October",  "November", "December"};
  if (month < 1 || month > 12)
    return {};
  return (lang == QLatin1String("tr") ? tr : en)[month - 1];
}

} // namespace skg::analysis
