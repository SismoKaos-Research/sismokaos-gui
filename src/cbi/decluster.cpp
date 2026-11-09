#include "cbi/decluster.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace skg::cbi {

std::pair<double, double> gkWindow(double m) {
  const double km = std::pow(10.0, 0.1238 * m + 0.983);
  const double days = m >= 6.5 ? std::pow(10.0, 0.032 * m + 2.7389)
                               : std::pow(10.0, 0.5409 * m - 0.547);
  return {km, days};
}

std::vector<bool> gardnerKnopoff(const Catalog &catalog) {
  const size_t n = catalog.size();
  std::vector<bool> clustered(n, false), mainshock(n, false);
  if (n == 0)
    return mainshock;
  std::vector<double> days(n);
  const double t0 = catalog.events[0].epoch;
  for (size_t i = 0; i < n; ++i)
    days[i] = (catalog.events[i].epoch - t0) / 86400.0;

  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), 0);
  // Stable sort on -magnitude keeps the earliest of equal magnitudes first.
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return catalog.events[a].mag > catalog.events[b].mag;
  });
  for (size_t i : order) {
    if (clustered[i])
      continue;
    mainshock[i] = clustered[i] = true;
    const analysis::Event &e = catalog.events[i];
    const auto [winKm, winDays] = gkWindow(e.mag);
    const auto lo =
        std::lower_bound(days.begin(), days.end(), days[i] - winDays);
    const auto hi =
        std::lower_bound(days.begin(), days.end(), days[i] + winDays);
    const double kx = kKmPerDegLonEquator * std::cos(e.lat * M_PI / 180.0);
    for (auto it = lo; it != hi; ++it) {
      const size_t j = static_cast<size_t>(it - days.begin());
      if (clustered[j])
        continue;
      const analysis::Event &o = catalog.events[j];
      const double d =
          std::hypot((o.lon - e.lon) * kx, (o.lat - e.lat) * kKmPerDegLat);
      if (d <= winKm)
        clustered[j] = true;
    }
  }
  return mainshock;
}

} // namespace skg::cbi
