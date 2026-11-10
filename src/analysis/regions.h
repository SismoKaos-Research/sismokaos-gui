// Seismic regionalisation of an earthquake catalog by epicentre: a linear
// self-organising map (one region per unit), and the hybrid of a square
// SOM whose units are then grouped by Mean Shift, which finds the number of
// regions itself. Port of earthquake-clustering-eafz (Z. Çalım); the SOM
// follows MiniSom's training rule and the Mean Shift scikit-learn's, with
// this library's own random numbers, so the regions are reproducible for a
// seed here but are not the Python script's event for event.
#pragma once

#include <QString>
#include <QStringList>

#include <array>
#include <vector>

namespace skg::analysis {

using Point2 = std::array<double, 2>; // (latitude, longitude)

// A catalog kept as text, so the region files can carry every original
// column untouched. Rows without a latitude or longitude are dropped.
struct PointTable {
  QString headerLine;
  QStringList header;
  int latCol = -1, lonCol = -1;
  QStringList rows; // the original lines
  std::vector<Point2> points;
  int size() const { return int(points.size()); }
};
// Columns latitude/enlem and longitude/boylam (also lat, lon), any case;
// plain or .gz. Throws std::runtime_error.
PointTable loadPointTable(const QString &path);

struct SomOptions {
  int rows = 1, cols = 5;
  double sigma = 0.5;
  double learningRate = 0.5;
  int iterations = 5000;
  quint64 seed = 42;
};

struct Som {
  int rows = 0, cols = 0;
  std::vector<Point2> weights; // row-major: unit (r, c) is weights[r*cols+c]
  // Index of the best-matching unit.
  int winner(const Point2 &x) const;
};
// Units start at randomly chosen samples; each iteration moves the units
// towards one random sample, with a Gaussian neighbourhood on the grid and
// the rate and width decaying as 1 / (1 + 2t/iterations). Throws on no data.
Som trainSom(const std::vector<Point2> &x, const SomOptions &o);

// The mean distance from each point to its int(n * quantile)-th nearest
// neighbour, itself included (scikit-learn's estimate_bandwidth).
double estimateBandwidth(const std::vector<Point2> &x, double quantile);

struct MeanShiftResult {
  std::vector<Point2> centers; // densest first
  std::vector<int> labels;     // index of the nearest centre, per point
  double bandwidth = 0;
};
// Flat-kernel Mean Shift seeded from the occupied bins of a grid of the
// bandwidth's size (bin_seeding); centres closer than the bandwidth to a
// denser one are dropped. Throws when the bandwidth is not positive.
MeanShiftResult meanShift(const std::vector<Point2> &x, double bandwidth,
                          int maxIterations = 300);

enum class RegionMethod { Som, SomMeanShift };

struct RegionOptions {
  RegionMethod method = RegionMethod::Som;
  int regions = 5;   // Som: a 1 x regions map
  int gridRows = 10; // SomMeanShift: the compressing map
  int gridCols = 10;
  int iterations = 5000;
  double quantile = 0.15; // SomMeanShift: bandwidth quantile
  quint64 seed = 42;
  // The script's values: sigma 0.5 for the linear map, 1.0 for the grid.
  double sigma() const { return method == RegionMethod::Som ? 0.5 : 1.0; }
};

struct RegionSummary {
  int id = 0; // 1-based
  Point2 center{0, 0};
  double latMin = 0, latMax = 0, lonMin = 0, lonMax = 0;
  int count = 0;
};

struct Regions {
  RegionMethod method = RegionMethod::Som;
  std::vector<int> regionOf;          // 1-based id per point
  std::vector<RegionSummary> regions; // those with at least one event, by id
  double bandwidth = 0;               // SomMeanShift only
};
Regions regionalise(const std::vector<Point2> &x, const RegionOptions &o);

// "som" or "sommeanshift": the prefix of the script's file names.
QString methodPrefix(RegionMethod m);
// Writes <prefix>_region_<id>.csv per region into `dir`, each the header
// and the region's original lines. Returns the paths. Throws.
QStringList saveRegionCsvs(const PointTable &table, const Regions &regions,
                           const QString &dir);

} // namespace skg::analysis
