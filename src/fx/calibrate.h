// Calibration of the chaos parameters on seismically quiet periods. Port of
// sismokaos-cli's calibrate.rs: catalog events near the station become
// exclusion intervals; gap-free windows clear of them are reservoir-sampled;
// too-loud ones are dropped; per window and component tau comes from the
// first AMI minimum, the mean period from the power-weighted mean frequency,
// dim from False Nearest Neighbours; the run uses the medians, and SampEn's
// tolerance becomes absolute (r times the quiet std per component).
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <vector>

#include "fx/config.h"
#include "fx/preprocess.h"

namespace skg::fx {

struct Calibration {
  QString station;
  double fs = 0;
  int tau = 0, dim = 0;
  double meanPeriod = 0;
  double sampentR = 0;
  std::array<double, 3> sampentTol{};
  std::array<double, 2> tauIqr{}, dimIqr{}, meanPeriodIqr{};
  std::array<double, 3> quietStd{};
  int windowsTotal = 0, windowsQuiet = 0, windowsSampled = 0;
  std::array<int, 3> windowsLoud{};
  int measured = 0, withTau = 0;
  double stationLat = 0, stationLon = 0;
  int exclusionIntervals = 0;
  CalibrationConfig settings;

  QJsonObject toJson() const;
  static Calibration
  fromJson(const QJsonObject &o);               // throws on a non-calibration
  static Calibration load(const QString &path); // throws std::runtime_error
  bool save(const QString &path) const;
  void apply(Config &cfg) const;
  QStringList describe() const;
};

// Linear-interpolated percentile, as numpy's default.
double percentile(std::vector<double> v, double q);
// Power-weighted mean frequency over a Hann-tapered periodogram, DC excluded.
double meanFrequency(const std::vector<double> &x, double fs);

using ProgressFn = std::function<void(int done, int total)>;

// Why a calibration found too few quiet windows, for whoever has to say
// what to do about it. what() is the full sentence.
class CalibrationError : public std::runtime_error {
public:
  enum class Reason {
    NoData,     // nothing could be read for the station in the files given
    LittleData, // some data, but not enough of it for the windows needed
    GappyData,  // enough windows, most of them with gaps
    Events,     // enough clean windows, the catalog's events exclude them
  };
  CalibrationError(Reason reason, const QString &message)
      : std::runtime_error(message.toStdString()), reason_(reason) {}
  Reason reason() const { return reason_; }
  // "no_data", "little_data", "gappy_data", "events": as run records keep it.
  static QString code(Reason reason);

private:
  Reason reason_;
};

// Runs the calibration pass over the data. Throws CalibrationError when the
// quiet windows are too few, std::runtime_error with the reason for anything
// else (no station position, no catalog...).
Calibration calibrate(const Config &cfg, const PreprocessOptions &opt,
                      const ProgressFn &progress = {});

} // namespace skg::fx
