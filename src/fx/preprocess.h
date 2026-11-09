// Streaming preprocessing of a directory of miniSEED files (and zips): the
// records are indexed, assembled per component onto a common time grid in
// hour-long blocks, gap-interpolated, detrended, bandpassed with real
// context on both sides, decimated, and handed out as chunks. Port of
// sismokaos-cli's preprocess.rs.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "fx/config.h"
#include <utility>

namespace skg::fx {

struct ChannelChunk {
  std::vector<double> e, n, z;
  // Per-sample flags marking samples the gap interpolant fabricated.
  std::vector<bool> eInterp, nInterp, zInterp;
  double fs = 0;
  double startEpoch = 0;
  int recordsDone = 0, recordsTotal = 0;
};

using ChunkSink = std::function<void(ChannelChunk &&)>;
using LogFn = std::function<void(const QString &)>;
using StopFn = std::function<bool()>;

struct PreprocessOptions {
  // Where zip members are inflated for the run (removed afterwards).
  QString stagingRoot;
  LogFn log;
  StopFn shouldStop;
};

// The input files of a directory: every regular file, zips replaced by their
// inflated members under `<stagingRoot>/.unzipped-<pid>`. The returned guard
// removes the staging directory when destroyed.
struct Inputs {
  QStringList files;
  QString stagingDir;
  ~Inputs();
  Inputs() = default;
  Inputs(Inputs &&) = default;
  Inputs &operator=(Inputs &&) = default;
  Inputs(const Inputs &) = delete;
};
Inputs expandInputs(const QStringList &listed, const QString &stagingRoot);

// Runs the whole directory through the preprocessor, calling `sink` per
// block. Throws std::runtime_error on unreadable input. Returns false when
// stopped early.
bool preprocessDirectory(const QString &dataDir, const Config &cfg,
                         const PreprocessOptions &opt, const ChunkSink &sink);

// One component of one block, exposed for tests: interpolate, detrend,
// filter with `skip`/`end` as context, decimate. Returns samples and mask.
std::pair<std::vector<double>, std::vector<bool>>
processComponent(std::vector<double> data, double freqmin, double freqmax,
                 double fs, int decimationFactor, int skip, int end);

double filterContextSeconds(double freqmin);

} // namespace skg::fx
