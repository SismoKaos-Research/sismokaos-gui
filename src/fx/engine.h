// The feature-extraction run: calibration (or a reused one), streaming
// preprocessing, windowing with a carry-over between chunks, features per
// window, and the outputs. Port of sismokaos-cli's engine.rs, export.rs,
// preprocess_only.rs and raw_binary.rs, with the Python tool's output
// formats (CSV + structured .npy + run_metadata.json) instead of parquet.
#pragma once

#include <QFile>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "fx/calibrate.h"
#include "fx/config.h"
#include "fx/features.h"
#include "fx/preprocess.h"
#include <utility>

namespace skg::fx {

// Starts of the whole windows in a buffer of `len` samples, the first at
// `first`, and the next cursor, which may lie beyond `len` when the step is
// longer than the window.
std::pair<std::vector<int>, int> windowStarts(int len, int win, int step,
                                              int first);

// Accumulates feature rows and writes <stem>_features.csv, .npy and
// _run_metadata.json. `_DEV` columns are first differences (NaN on the
// first row and whenever either side is NaN); *_INTERP_FRAC columns are the
// fraction of fabricated samples per component and get no _DEV twin.
class FeatureWriter {
public:
  explicit FeatureWriter(QStringList featureNames);
  void addWindow(const QString &id, double timeMinutes,
                 const FeatureRow &features, std::array<double, 3> interp);
  int rows() const { return int(ids_.size()); }
  QStringList columns() const;
  // Writes the three files; throws std::runtime_error on failure.
  void finish(const QString &basePath, const Config &cfg) const;

private:
  QStringList names_;
  QStringList ids_;
  std::vector<double> times_;
  std::vector<std::vector<double>> features_, devs_; // per row
  std::vector<std::array<double, 3>> interp_;
  std::vector<double> prev_;
};

// Flat little-endian f32, E N Z interleaved, plus a JSON sidecar with the
// piecewise-linear time axis (segments). numpy: np.memmap(p, '<f4').reshape(-1,
// 3).
class RawBinaryWriter {
public:
  explicit RawBinaryWriter(const QString &basePath); // writes <base>.f32
  ~RawBinaryWriter();
  void writeChunk(const std::vector<double> &e, const std::vector<double> &n,
                  const std::vector<double> &z, double fs, double startEpoch);
  qint64 finish(const Config &cfg); // sidecar <base>.f32.json; returns samples
  QString path() const { return path_; }

private:
  struct Segment {
    qint64 sampleOffset;
    double startEpoch;
  };
  QString path_;
  std::unique_ptr<QFile> file_;
  double fs_ = 0;
  qint64 samples_ = 0;
  std::vector<Segment> segments_;
  std::array<qint64, 3> nanCounts_{};
};

// A run goes through phases (unpacking the archives, calibrating, extracting
// the features); which ones depends on the run. `overall` gives one bar for
// all of them: a fraction of the whole run that never goes back, the phase
// it is in ("2/3 Calibrating chaos parameters"), and a line to show beside
// the bar (the phase, how far it is, and the time it has left when that can
// be told). The share of the bar each phase gets is fixed (kPhaseWeights in
// engine.cpp): it is a guess at their cost, not a measurement of this run.
struct RunProgress {
  double fraction = 0; // of the whole run, 0..1, never decreasing
  int phase = 0;       // 1-based
  int phases = 0;
  QString name;        // "Calibrating chaos parameters"
  QString text;        // "2/3 Calibrating chaos parameters · 40% · about 3 min left"
};

struct RunHooks {
  LogFn log;
  StopFn shouldStop;
  // Called when a phase starts, with its name alone (also logged by most
  // callers).
  std::function<void(const QString &phase)> phase;
  // The current phase alone, in its own units (records); starts again from
  // zero with each phase. Use `overall` for a progress bar.
  std::function<void(int done, int total)> progress;
  std::function<void(const RunProgress &)> overall;
  // A calibration was computed (not loaded from `calibrationFile`) and
  // written to this path, before the extraction starts: a caller that wants
  // to keep it whatever becomes of the run copies it now.
  std::function<void(const QString &calibrationPath)> calibrated;
};

struct RunResult {
  QString featuresCsv, featuresNpy, metadata, calibration, rawF32, rawSidecar;
  int windows = 0;
  qint64 samples = 0;
  bool stopped = false;
  // The sensor of the station that was read, and what was left out.
  ChannelChoice channels;
};

// The output stem of a run: the data directory's name, as the Rust tool.
QString outputStem(const Config &cfg);

// Full run for one configuration (one band). Throws std::runtime_error.
RunResult runPipeline(Config cfg, const RunHooks &hooks);
// Preprocess only: filtered and decimated E/N/Z as f32 + sidecar.
RunResult runPreprocessOnly(const Config &cfg, const RunHooks &hooks);

} // namespace skg::fx
