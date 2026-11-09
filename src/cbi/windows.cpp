#include "cbi/windows.h"

#include <QRegularExpression>

#include <cmath>
#include <numeric>

namespace skg::cbi {

std::optional<Duration> Duration::parse(const QString &text) {
  static const QRegularExpression re(
      QStringLiteral("^\\s*(\\d+)\\s*([DWMYdwmy])\\s*$"));
  const QRegularExpressionMatch m = re.match(text);
  if (!m.hasMatch())
    return std::nullopt;
  const int n = m.captured(1).toInt();
  if (n <= 0)
    return std::nullopt;
  return Duration{n, m.captured(2).toUpper().at(0)};
}

QDateTime Duration::addTo(const QDateTime &t) const {
  switch (unit.toLatin1()) {
  case 'D':
    return t.addDays(amount);
  case 'W':
    return t.addDays(7LL * amount);
  case 'Y':
    return t.addYears(amount);
  default:
    return t.addMonths(amount);
  }
}

QString Duration::text() const { return QString::number(amount) + unit; }

QString Duration::label(const QString &lang) const {
  const bool tr = lang == QLatin1String("tr");
  QString u;
  switch (unit.toLatin1()) {
  case 'D':
    u = tr ? QStringLiteral("Gun") : QStringLiteral("days");
    break;
  case 'W':
    u = tr ? QStringLiteral("Hafta") : QStringLiteral("weeks");
    break;
  case 'Y':
    u = tr ? QStringLiteral("Yil") : QStringLiteral("years");
    break;
  default:
    u = tr ? QStringLiteral("Ay") : QStringLiteral("months");
  }
  return QStringLiteral("%1 %2").arg(amount).arg(u);
}

QString Duration::shortName() const {
  QString u;
  switch (unit.toLatin1()) {
  case 'D':
    u = QStringLiteral("gun");
    break;
  case 'W':
    u = QStringLiteral("hafta");
    break;
  case 'Y':
    u = QStringLiteral("yil");
    break;
  default:
    u = QStringLiteral("ay");
  }
  return QString::number(amount) + u;
}

std::vector<std::pair<QDateTime, QDateTime>>
windowBounds(const QDateTime &tMin, const QDateTime &tMax,
             const Duration &length, const Duration &step) {
  std::vector<std::pair<QDateTime, QDateTime>> out;
  QDateTime start = tMin;
  while (length.addTo(start) <= tMax) {
    out.emplace_back(start, length.addTo(start));
    start = step.addTo(start);
  }
  return out;
}

std::vector<double> rectangularWindow(int n) {
  return std::vector<double>(static_cast<size_t>(std::max(0, n)), 1.0);
}

std::vector<double> sawtoothWindow(int n) {
  std::vector<double> w(static_cast<size_t>(std::max(0, n)));
  for (int i = 0; i < n; ++i)
    w[static_cast<size_t>(i)] = n == 1 ? 0.0 : double(i) / (n - 1);
  return w;
}

namespace {
void normalise(std::vector<double> &w) {
  const double s = std::accumulate(w.begin(), w.end(), 0.0);
  if (s > 0)
    for (double &v : w)
      v /= s;
}
} // namespace

std::vector<double> halfHammingWindow(int n) {
  std::vector<double> w(static_cast<size_t>(std::max(0, n)));
  const int m = 2 * n; // numpy.hamming(2n)[:n]
  for (int i = 0; i < n; ++i)
    w[static_cast<size_t>(i)] =
        m == 1 ? 1.0 : 0.54 - 0.46 * std::cos(2 * M_PI * i / (m - 1));
  normalise(w);
  return w;
}

std::vector<double> halfGaussWindow(int n, double sigma) {
  std::vector<double> w(static_cast<size_t>(std::max(0, n)));
  for (int i = 0; i < n; ++i) {
    const double x = n == 1 ? 0.0 : double(i) / (n - 1);
    w[static_cast<size_t>(i)] = std::exp(-0.5 * std::pow((x - 1) / sigma, 2));
  }
  normalise(w);
  return w;
}

} // namespace skg::cbi
