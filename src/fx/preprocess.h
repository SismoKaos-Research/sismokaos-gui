// Streaming preprocessing of a directory of miniSEED files (and zips): the
// records are indexed, assembled per component onto a common time grid in
// hour-long blocks, gap-interpolated, detrended, bandpassed with real
// context on both sides, decimated, and handed out as chunks. Port of
// sismokaos-cli's preprocess.rs.
#pragma once

#include <QLockFile>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <stdexcept>
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

struct Inputs;

// A run was asked to stop. It is a std::runtime_error whose what() is
// "stopped", as before; a caller that is told to stop treats it as that,
// not as a failure.
struct Stopped : std::runtime_error {
  Stopped() : std::runtime_error("stopped") {}
};

// The files hold no record of the station inside the period asked for
// (Config::startEpoch, endEpoch).
struct NoDataInPeriod : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Which sensor of the station a pass read, and what it left aside. A
// station's files can hold several: a broadband (HH?) and an accelerometer
// (HN?), or the same instrument under two location codes. Only one is read.
struct ChannelChoice {
  QString family;      // NET.STA.LOC.HH, the three channels without E/N/Z
  QStringList used;    // the channels read, NET.STA.LOC.CHA
  QStringList dropped; // the families left out, as "NET.STA.LOC.HN?"
  // True when several complete sensors were there and one was taken without
  // the configuration naming it.
  bool guessed = false;
  QString how; // one line for the log: what was read, what was left, why
};

struct PreprocessOptions {
  // Where zip members are inflated for the run (removed afterwards).
  QString stagingRoot;
  LogFn log;
  StopFn shouldStop;
  // The input files, already listed and unpacked (prepareInputs): a run
  // that goes over the data twice (calibration, then extraction) unpacks
  // its archives once and hands them to both passes. Null: the pass lists
  // and unpacks for itself, and removes what it unpacked when it ends.
  const Inputs *prepared = nullptr;
  // While archives are unpacked: bytes written of the bytes to write.
  std::function<void(qint64 done, qint64 total)> unpacking;
  // When set, filled with the channels the pass read.
  ChannelChoice *channels = nullptr;
};

// The input files of a directory: every regular file, zips replaced by their
// inflated members in a staging folder of its own under `stagingRoot`
// (`.unzipped-XXXXXX`, a new one for every call, with a lock file held as
// long as it is in use). The returned guard removes the folder when
// destroyed. Staging folders other calls left behind are removed only when
// nothing holds their lock any more: a second run in this process, or
// another instance of the application, keeps its own.
struct Inputs {
  QStringList files;
  QString stagingDir;
  std::unique_ptr<QLockFile> lock;
  ~Inputs();
  Inputs();
  Inputs(Inputs &&) noexcept;
  Inputs &operator=(Inputs &&) noexcept;
  Inputs(const Inputs &) = delete;
};
// `unpacking` is called as the members are written; a true `shouldStop`
// ends the unpacking with Stopped. A member is written under its archive's
// name, its number in the archive and its own name, so two members of the
// same name in different folders of an archive both stay.
Inputs expandInputs(
    const QStringList &listed, const QString &stagingRoot,
    const std::function<void(qint64 done, qint64 total)> &unpacking = {},
    const StopFn &shouldStop = {});
// The files a run of `cfg` reads (its chosen input files, else the file or
// every file of `dataDir`), before any archive is unpacked. Throws
// std::runtime_error when there is none.
QStringList listInputs(const QString &dataDir, const Config &cfg);
// Whether any of them is an archive that has to be unpacked first.
bool hasArchives(const QStringList &listed);
// listInputs() and expandInputs() in one, with the options' staging folder,
// progress and stop.
Inputs prepareInputs(const QString &dataDir, const Config &cfg,
                     const PreprocessOptions &opt);

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
