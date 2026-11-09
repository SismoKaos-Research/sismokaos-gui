// Calendar durations ("12M", "15D"), the sliding [start, end) windows over
// a catalog, and the weighting functions applied inside a window. Port of
// indicators-forecast's windows.py; the month arithmetic follows pandas'
// DateOffset (day clamped to the month's end), which QDate::addMonths
// shares.
#pragma once

#include <QDateTime>
#include <QString>

#include <optional>
#include <utility>
#include <vector>

namespace skg::cbi {

struct Duration {
  int amount = 1;
  QChar unit = QLatin1Char('M'); // D, W, M, Y

  // "12M", "15d", " 2 W " -> Duration; anything else nullopt.
  static std::optional<Duration> parse(const QString &text);
  QDateTime addTo(const QDateTime &t) const;
  QString text() const;                     // "12M"
  QString label(const QString &lang) const; // "12 Ay" / "12 months"
  QString shortName() const;                // "12ay", "15gun", "2hafta", "1yil"
  bool operator==(const Duration &o) const {
    return amount == o.amount && unit == o.unit;
  }
};

// Windows from tMin advancing by step, each fully inside [tMin, tMax].
std::vector<std::pair<QDateTime, QDateTime>>
windowBounds(const QDateTime &tMin, const QDateTime &tMax,
             const Duration &length, const Duration &step);

std::vector<double> rectangularWindow(int n);
std::vector<double> sawtoothWindow(int n);
// Rising half of a Hamming window peaking at the newest event, sum 1.
std::vector<double> halfHammingWindow(int n);
// Right half of a Gaussian peaking at the newest event, sum 1.
std::vector<double> halfGaussWindow(int n, double sigma = 0.4);

} // namespace skg::cbi
