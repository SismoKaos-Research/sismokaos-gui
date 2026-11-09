// Gardner-Knopoff (1974) window declustering, with the common fits of
// their tabulated windows (van Stiphout et al. 2012, CORSSA). Port of
// indicators-forecast's decluster.py.
#pragma once

#include <utility>
#include <vector>

#include "cbi/catalog.h"

namespace skg::cbi {

constexpr double kKmPerDegLat = 110.57;
constexpr double kKmPerDegLonEquator = 111.32;

// Space (km) and time (days) windows of a magnitude.
std::pair<double, double> gkWindow(double magnitude);

// True for independent events (mainshocks and singles). Events are visited
// from the largest magnitude down; each not-yet-clustered event claims
// every unclustered event within its space-time window, before or after
// it. The catalog must be sorted by time.
std::vector<bool> gardnerKnopoff(const Catalog &catalog);

} // namespace skg::cbi
