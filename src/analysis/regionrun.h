// A whole regions run: read the catalog, regionalise it by the chosen
// methods, and write into the output folder the script's files (the two
// study-area maps, a map and the per-region catalogs per method) plus a
// summary table and a record of what was run.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "analysis/regionmap.h"
#include "analysis/regions.h"

namespace skg::analysis {

struct RegionRunOptions {
  QString catalogPath;
  QString outDir;
  bool som = true, somMeanShift = true; // the methods to run
  int regions = 5;                      // the linear map's units
  int gridRows = 10, gridCols = 10;     // the hybrid's map
  int iterations = 5000;
  double quantile = 0.15;
  quint64 seed = 42;

  QString areaName; // first line of the map titles; may be empty
  QString lang = QStringLiteral("tr");
  bool maps = true;
  bool svg = false; // an SVG beside every PNG (large for big catalogs)
  int dpi = 200;    // PNG resolution of the 12 x 10 inch maps
  std::optional<MapMarker> marker;
  std::vector<City> cities;
  // Layers, shared with the caller; null leaves the layer out. The
  // *Source strings only go into the run record.
  std::shared_ptr<const ShapeLayer> countries, provinces;
  std::shared_ptr<const std::vector<FaultLine>> faults;
  QString countriesSource, provincesSource, faultsSource, citiesSource;
  // Set when `faults` is the shipped fault-source layer and not an active
  // fault map: the legend name of its lines and the attribution printed
  // under the maps that draw it (both in `lang`).
  QString faultsLabel, faultsCredit;
};

struct RegionRunHooks {
  std::function<void(const QString &)> log;
  std::function<bool()> shouldStop;
  std::function<void(int done, int total, const QString &what)> progress;
};

struct RegionRunResult {
  QString outDir;
  std::shared_ptr<const PointTable> table;
  std::vector<Regions> results; // in the order run: SOM, then the hybrid
  QStringList files;            // everything written
  bool stopped = false;
};

// The map titles ("<area>  |  Distribution of Seismic Sub-regions\nSOM
// Model"), `kind` one of plain, geographic, som, sommeanshift.
QString regionMapTitle(const QString &kind, const QString &areaName,
                       const QString &lang);
// The spec of one of the run's maps; `regions` null for the study-area maps
// and `geographic` false for the plain one.
RegionMapSpec regionMapSpec(const RegionRunOptions &o, const PointTable &table,
                            const Regions *regions, bool geographic);

// Throws std::runtime_error when the catalog cannot be read, holds no
// event with coordinates, no method is chosen, or nothing can be written.
RegionRunResult runRegions(const RegionRunOptions &o,
                           const RegionRunHooks &hooks = {});

} // namespace skg::analysis
