// Configuration of the feature-extraction pipeline. A port of sismokaos-cli's
// `config.rs` (the Rust rewrite of Sismokaos-featureExtract, whose chaos
// parameters are the current ones). The JSON layout is the Rust tool's; the
// Python tool's upper-case keys (STATION, Fs, WIN_SEC, CHAOS_TAU, ...) are
// accepted as aliases when reading, so either config.json loads.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace skg::fx {

// Where quiet periods come from and how the chaos parameters are estimated
// on them (see calibrate.h).
struct CalibrationConfig {
  QString catalog;       // AFAD catalog CSV, optionally gzipped
  QString stationCoords; // station list with code/latitude/longitude
  std::optional<double> stationLat, stationLon; // override the list
  double minMag = 0.0;
  double maxDistKm = 300.0;
  double preSec = 120.0;
  double minPostSec = 300.0;
  double codaFactor = 3.0;
  int maxWindows = 300;
  double loudFactor = 3.0;
  quint64 seed = 0;
  int maxTau = 20;
  int maxDim = 10;

  QJsonObject toJson() const;
  static CalibrationConfig fromJson(const QJsonObject &o);
};

// One frequency band to extract features from; each is a run of its own.
struct Band {
  QString name; // defaults to "<freqmin>-<freqmax>Hz"
  double freqmin = 0, freqmax = 0, fs = 0;
  int winSec = 0, stepSec = 0;
  double staSec = 0;
  int ltaSec = 0;
  std::optional<double> spectralSplitHz;
  QString label() const;
};

struct Config {
  QString station = QStringLiteral("ELZG");
  double fs = 5.0;
  int winSec = 200;
  int stepSec = 50;
  double staSec = 0.5;
  int ltaSec = 60;
  double freqmin = 0.1;
  double freqmax = 2.0;

  int chaosTau = 5;
  int chaosDim = 5;
  int chaosEvolve = 5;
  int chaosMinSamples = 500;
  double chaosMeanPeriod = 1.0;
  std::array<double, 2> chaosRosRise{0.1, 0.9};
  double chaosRosHorizon = 20.0;
  int chaosSampentM = 2;
  double chaosSampentR = 0.2;
  // Absolute SampEn tolerance per component (E, N, Z), set by the calibration.
  std::optional<std::array<double, 3>> chaosSampentTol;
  CalibrationConfig calibration;
  bool chaosOnly = false;
  double spectralSplitHz = 1.0;
  QList<Band> bands;
  QString band; // the band this run is for, in its metadata

  // Derived and run-time fields, not in the JSON.
  QString dataDir;
  // When not empty, exactly these files (plain or zip) are read instead of
  // everything in `dataDir`, and only the records of `station` are used.
  QStringList inputFiles;
  QString outputRoot;
  QString calibrationFile; // reuse a previous run's calibration
  QStringList bandFilter;  // empty runs every band

  int winSize() const { return int(winSec * fs); }
  int stepSize() const { return int(stepSec * fs); }
  int nsta() const { return int(staSec * fs); }
  int nlta() const { return int(ltaSec * fs); }

  // The configurations to run: one per selected band, or this one alone.
  // Throws std::invalid_argument for an unknown band filter.
  QList<Config> bandRuns() const;
  // Problems that would silently corrupt the output; empty when fine.
  QStringList validate() const;
  // Warnings that do not stop a run (near-Nyquist, STA/LTA lengths).
  QStringList warnings() const;
  // Non-empty when `fs` is not an exact divisor of the native rate.
  QString checkDecimation(double nativeFs) const;

  QJsonObject toJson() const;
  static Config fromJson(const QJsonObject &o);
  static Config load(const QString &path); // defaults when missing/invalid
  bool save(const QString &path) const;
};

} // namespace skg::fx
