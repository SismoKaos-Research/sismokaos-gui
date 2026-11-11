#include "fx/engine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "fx/npy.h"
#include <utility>

namespace skg::fx {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
const QStringList kInterpNames{QStringLiteral("E_INTERP_FRAC"),
                               QStringLiteral("N_INTERP_FRAC"),
                               QStringLiteral("Z_INTERP_FRAC")};

QString fmt(double v) {
  if (std::isnan(v))
    return {};
  return QString::number(v, 'f', 6);
}

void say(const RunHooks &h, const QString &m) {
  if (h.log)
    h.log(m);
}
bool stopping(const RunHooks &h) { return h.shouldStop && h.shouldStop(); }
} // namespace

std::pair<std::vector<int>, int> windowStarts(int len, int win, int step,
                                              int first) {
  std::vector<int> starts;
  int cursor = first;
  while (cursor + win <= len) {
    starts.push_back(cursor);
    cursor += step;
  }
  return {starts, cursor};
}

// --- FeatureWriter -----------------------------------------------------------

FeatureWriter::FeatureWriter(QStringList featureNames)
    : names_(std::move(featureNames)) {}

void FeatureWriter::addWindow(const QString &id, double timeMinutes,
                              const FeatureRow &features,
                              std::array<double, 3> interp) {
  ids_ << id;
  times_.push_back(timeMinutes);
  interp_.push_back(interp);
  std::vector<double> row(size_t(names_.size()), kNaN);
  for (const auto &[name, value] : features) {
    const qsizetype i = names_.indexOf(name);
    if (i >= 0)
      row[size_t(i)] = value;
  }
  std::vector<double> dev(row.size(), kNaN);
  if (!prev_.empty())
    for (size_t i = 0; i < row.size(); ++i)
      if (!std::isnan(row[i]) && !std::isnan(prev_[i]))
        dev[i] = row[i] - prev_[i];
  prev_ = row;
  features_.push_back(std::move(row));
  devs_.push_back(std::move(dev));
}

QStringList FeatureWriter::columns() const {
  QStringList cols{QStringLiteral("Pencere_ID"), QStringLiteral("Zaman_Dk")};
  cols << names_;
  for (const QString &n : names_)
    cols << n + QStringLiteral("_DEV");
  cols << kInterpNames;
  return cols;
}

void FeatureWriter::finish(const QString &basePath, const Config &cfg) const {
  QDir().mkpath(QFileInfo(basePath).absolutePath());
  const QStringList cols = columns();
  // CSV, the Python tool's float_format="%.6f" and empty cells for NaN.
  {
    QSaveFile f(basePath + QStringLiteral(".csv"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      throw std::runtime_error(("cannot write " + f.fileName()).toStdString());
    f.write(cols.join(',').toUtf8() + '\n');
    for (int r = 0; r < rows(); ++r) {
      QStringList cells{ids_[r], fmt(times_[size_t(r)])};
      for (double v : features_[size_t(r)])
        cells << fmt(v);
      for (double v : devs_[size_t(r)])
        cells << fmt(v);
      for (double v : interp_[size_t(r)])
        cells << fmt(v);
      f.write(cells.join(',').toUtf8() + '\n');
    }
    if (!f.commit())
      throw std::runtime_error(("cannot write " + f.fileName()).toStdString());
  }
  // Structured npy with the same columns.
  {
    NpyTable t;
    t.fields.append({QStringLiteral("Pencere_ID"), QStringLiteral("<U50")});
    for (int c = 1; c < cols.size(); ++c)
      t.fields.append({cols[c], QStringLiteral("<f8")});
    t.rows = rows();
    t.strings.append(ids_);
    t.numeric.append(std::vector<double>());
    t.numeric.append(times_);
    t.strings.append(QStringList());
    const size_t nf = size_t(names_.size());
    for (size_t j = 0; j < nf; ++j) {
      std::vector<double> col;
      col.reserve(size_t(rows()));
      for (int r = 0; r < rows(); ++r)
        col.push_back(features_[size_t(r)][j]);
      t.numeric.append(std::move(col));
      t.strings.append(QStringList());
    }
    for (size_t j = 0; j < nf; ++j) {
      std::vector<double> col;
      col.reserve(size_t(rows()));
      for (int r = 0; r < rows(); ++r)
        col.push_back(devs_[size_t(r)][j]);
      t.numeric.append(std::move(col));
      t.strings.append(QStringList());
    }
    for (size_t c = 0; c < 3; ++c) {
      std::vector<double> col;
      for (int r = 0; r < rows(); ++r)
        col.push_back(interp_[size_t(r)][c]);
      t.numeric.append(std::move(col));
      t.strings.append(QStringList());
    }
    if (!writeNpy(basePath + QStringLiteral(".npy"), t))
      throw std::runtime_error(
          ("cannot write " + basePath + ".npy").toStdString());
  }
  QString metaPath = basePath;
  metaPath.replace(QRegularExpression(QStringLiteral("_(chaos_)?features$")),
                   QString());
  if (!cfg.save(metaPath + QStringLiteral("_run_metadata.json")))
    throw std::runtime_error(
        ("cannot write " + metaPath + "_run_metadata.json").toStdString());
}

// --- RawBinaryWriter
// ----------------------------------------------------------

RawBinaryWriter::RawBinaryWriter(const QString &basePath)
    : path_(basePath + QStringLiteral(".f32")) {
  QDir().mkpath(QFileInfo(path_).absolutePath());
  file_ = std::make_unique<QFile>(path_);
  if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate))
    throw std::runtime_error(("cannot create " + path_).toStdString());
}

RawBinaryWriter::~RawBinaryWriter() = default;

void RawBinaryWriter::writeChunk(const std::vector<double> &e,
                                 const std::vector<double> &n,
                                 const std::vector<double> &z, double fs,
                                 double startEpoch) {
  const size_t len = std::min({e.size(), n.size(), z.size()});
  if (len == 0)
    return;
  fs_ = fs;
  bool continues = false;
  if (!segments_.empty()) {
    const Segment &s = segments_.back();
    const double expected =
        s.startEpoch + double(samples_ - s.sampleOffset) / fs;
    continues = std::abs(startEpoch - expected) < 0.5 / fs;
  }
  if (!continues)
    segments_.push_back({samples_, startEpoch});
  QByteArray buf;
  buf.resize(qsizetype(len * 3 * sizeof(float)));
  float *out = reinterpret_cast<float *>(buf.data());
  for (size_t i = 0; i < len; ++i) {
    const double v[3] = {e[i], n[i], z[i]};
    for (size_t c = 0; c < 3; ++c) {
      if (std::isnan(v[c]))
        ++nanCounts_[c];
      float f = float(v[c]);
      if constexpr (std::endian::native == std::endian::big)
        f = std::bit_cast<float>(qbswap(std::bit_cast<quint32>(f)));
      out[i * 3 + c] = f;
    }
  }
  if (file_->write(buf) != buf.size())
    throw std::runtime_error(("failed writing " + path_).toStdString());
  samples_ += qint64(len);
}

qint64 RawBinaryWriter::finish(const Config &cfg) {
  if (!file_->flush())
    throw std::runtime_error(("failed to flush " + path_).toStdString());
  file_->close();
  QJsonArray segs;
  for (const Segment &s : segments_)
    segs.append(QJsonObject{{"sample_offset", double(s.sampleOffset)},
                            {"start_epoch", s.startEpoch}});
  const QJsonObject meta{
      {"format", "raw-f32-le"},
      {"layout", "interleaved"},
      {"channels", QJsonArray{"E", "N", "Z"}},
      {"dtype", "<f4"},
      {"samples", double(samples_)},
      {"shape", QJsonArray{double(samples_), 3}},
      {"fs", fs_},
      {"nan_counts", QJsonObject{{"E", double(nanCounts_[0])},
                                 {"N", double(nanCounts_[1])},
                                 {"Z", double(nanCounts_[2])}}},
      {"freqmin", cfg.freqmin},
      {"freqmax", cfg.freqmax},
      {"station", cfg.station},
      {"segments", segs},
      {"note",
       "Sample i of segment s is at start_epoch + (i - sample_offset)/fs. The "
       "time axis is "
       "piecewise linear; use the segment table, not a single start time."}};
  QSaveFile side(path_ + QStringLiteral(".json"));
  if (!side.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      side.write(QJsonDocument(meta).toJson(QJsonDocument::Indented)) < 0 ||
      !side.commit())
    throw std::runtime_error(("cannot write " + path_ + ".json").toStdString());
  return samples_;
}

// --- runs
// ---------------------------------------------------------------------

QString outputStem(const Config &cfg) {
  const QString name = QFileInfo(QDir::cleanPath(cfg.dataDir)).fileName();
  return name.isEmpty() ? QStringLiteral("output") : name;
}

namespace {

// The shares of the bar. A guess at the cost of each phase on real station
// data: unpacking is the quickest, the calibration reads everything but
// measures a few hundred windows only, the extraction reads everything again
// and measures every window.
constexpr double kWeightUnpack = 5, kWeightCalibrate = 10,
                 kWeightExtract = 85;

// One bar over the phases of a run.
class Phases {
public:
  Phases(const RunHooks &hooks) : hooks_(hooks) {}
  void add(const QString &name, double weight) {
    names_ << name;
    weights_.push_back(weight);
    total_ += weight;
  }
  // The phase begins: announced, and the bar stands at its start.
  void begin(int index) {
    current_ = index;
    before_ = 0;
    for (int i = 0; i < index; ++i)
      before_ += weights_[size_t(i)];
    clock_.start();
    if (hooks_.phase)
      hooks_.phase(names_[index]);
    report(0.0, QString());
  }
  // `within`: how far the phase is, 0..1; `detail`: its own count.
  void report(double within, const QString &detail) {
    if (!hooks_.overall || current_ < 0 || total_ <= 0)
      return;
    within = std::clamp(within, 0.0, 1.0);
    RunProgress p;
    p.phase = current_ + 1;
    p.phases = int(names_.size());
    p.name = names_[current_];
    // Never back: a phase that reports less than it did (a count that was
    // revised) leaves the bar where it was.
    best_ = std::max(
        best_, (before_ + weights_[size_t(current_)] * within) / total_);
    p.fraction = std::min(1.0, best_);
    p.text = QStringLiteral("%1/%2 %3").arg(p.phase).arg(p.phases).arg(p.name);
    if (!detail.isEmpty())
      p.text += QStringLiteral(" · ") + detail;
    // The time the phase has left, from its own pace, once that is steady.
    const double elapsed = double(clock_.elapsed()) / 1000.0;
    if (within > 0.05 && within < 1.0 && elapsed > 10.0) {
      const double left = elapsed * (1.0 - within) / within;
      p.text += left < 90
                    ? QStringLiteral(" · under 2 min left in this phase")
                    : QStringLiteral(" · about %1 min left in this phase")
                          .arg(qRound(left / 60.0));
    }
    hooks_.overall(p);
  }
  void finish() {
    if (!hooks_.overall || names_.isEmpty())
      return;
    best_ = 1.0;
    RunProgress p;
    p.fraction = 1.0;
    p.phase = p.phases = int(names_.size());
    p.name = names_.last();
    p.text = QStringLiteral("%1/%2 %3").arg(p.phase).arg(p.phases).arg(p.name);
    hooks_.overall(p);
  }

private:
  const RunHooks &hooks_;
  QStringList names_;
  std::vector<double> weights_;
  double total_ = 0, before_ = 0, best_ = 0;
  int current_ = -1;
  QElapsedTimer clock_;
};

QString megabytes(qint64 bytes) {
  return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 0);
}

// The archives of the run unpacked once, for every pass over the data.
// Returns false when stopped.
bool unpackOnce(const Config &cfg, PreprocessOptions &popt, Phases &phases,
                int phase, Inputs &inputs) {
  phases.begin(phase);
  popt.unpacking = [&phases](qint64 done, qint64 total) {
    phases.report(total > 0 ? double(done) / double(total) : 0.0,
                  QStringLiteral("%1 of %2 MB")
                      .arg(megabytes(done), megabytes(total)));
  };
  try {
    inputs = prepareInputs(cfg.dataDir, cfg, popt);
  } catch (const std::runtime_error &e) {
    if (popt.shouldStop && popt.shouldStop() &&
        QByteArray(e.what()) == "stopped")
      return false;
    throw;
  }
  popt.unpacking = {};
  popt.prepared = &inputs;
  return true;
}

} // namespace

RunResult runPipeline(Config cfg, const RunHooks &hooks) {
  if (!QFileInfo::exists(cfg.dataDir))
    throw std::runtime_error(
        ("data directory not found: " + cfg.dataDir).toStdString());
  say(hooks, QStringLiteral(" Input: %1").arg(cfg.dataDir));
  const QString stem = outputStem(cfg);
  const QString suffix = cfg.chaosOnly ? QStringLiteral("chaos_features")
                                       : QStringLiteral("features");
  const QString base = QDir(cfg.outputRoot).filePath(stem + '_' + suffix);

  PreprocessOptions popt;
  popt.log = hooks.log;
  popt.shouldStop = hooks.shouldStop;
  popt.stagingRoot = cfg.outputRoot;
  QDir().mkpath(cfg.outputRoot);

  // The phases of this run, for one progress bar over all of them: the
  // archives are unpacked first and once, when there are any; the
  // calibration is left out when its result is reused.
  const bool calibrating = cfg.calibrationFile.isEmpty();
  const bool archives = hasArchives(listInputs(cfg.dataDir, cfg));
  Phases phases(hooks);
  int unpackPhase = -1, calibratePhase = -1, extractPhase = 0;
  if (archives) {
    unpackPhase = extractPhase++;
    phases.add(QStringLiteral("Unpacking the archives"), kWeightUnpack);
  }
  if (calibrating) {
    calibratePhase = extractPhase++;
    phases.add(QStringLiteral("Calibrating chaos parameters on quiet periods"),
               kWeightCalibrate);
  }
  phases.add(QStringLiteral("Extracting features"), kWeightExtract);
  const auto records = [](int done, int total) {
    return QStringLiteral("%1/%2 records").arg(done).arg(total);
  };

  RunResult result;
  popt.channels = &result.channels;
  Inputs unpacked; // removed when the run ends, however it ends
  if (archives && !unpackOnce(cfg, popt, phases, unpackPhase, unpacked)) {
    result.stopped = true;
    return result;
  }
  Calibration cal;
  if (!cfg.calibrationFile.isEmpty()) {
    cal = Calibration::load(cfg.calibrationFile);
    if (std::abs(cal.fs - cfg.fs) > 1e-9)
      throw std::runtime_error(
          QStringLiteral(
              "%1 was calibrated at %2 Hz but this run is at %3 Hz; its "
              "tau is in samples of the other rate")
              .arg(cfg.calibrationFile)
              .arg(cal.fs)
              .arg(cfg.fs)
              .toStdString());
    say(hooks, QStringLiteral(" Chaos parameters from %1 (station %2)")
                   .arg(cfg.calibrationFile, cal.station));
  } else {
    phases.begin(calibratePhase);
    try {
      cal = calibrate(cfg, popt, [&](int done, int total) {
        if (hooks.progress)
          hooks.progress(done, total);
        phases.report(total > 0 ? double(done) / total : 0.0,
                      records(done, total));
      });
    } catch (const Stopped &) {
      // Asked to stop while calibrating: no calibration, and no failure.
      result.stopped = true;
      return result;
    }
  }
  for (const QString &line : cal.describe())
    say(hooks, line);
  cal.apply(cfg);
  result.calibration =
      QDir(cfg.outputRoot).filePath(stem + QStringLiteral("_calibration.json"));
  if (!cal.save(result.calibration))
    throw std::runtime_error(
        ("cannot write " + result.calibration).toStdString());
  if (calibrating && hooks.calibrated)
    hooks.calibrated(result.calibration);
  if (stopping(hooks)) {
    result.stopped = true;
    return result;
  }
  phases.begin(extractPhase);

  std::array<std::vector<double>, 3> buf;
  std::array<std::vector<bool>, 3> mask;
  int pendingSkip = 0;
  int windowIdx = 0;
  double fsOut = cfg.fs;
  double bufEpoch = kNaN;
  std::unique_ptr<FeatureWriter> writer;
  const int win = cfg.winSize(), step = cfg.stepSize();

  const bool complete =
      preprocessDirectory(cfg.dataDir, cfg, popt, [&](ChannelChunk &&chunk) {
        fsOut = chunk.fs;
        const double expected = bufEpoch + double(buf[0].size()) / fsOut;
        if (std::isnan(bufEpoch) || buf[0].empty() ||
            std::abs(chunk.startEpoch - expected) > 1.5 / fsOut) {
          for (auto &b : buf)
            b.clear();
          for (auto &m : mask)
            m.clear();
          bufEpoch = chunk.startEpoch;
          pendingSkip = 0;
        }
        buf[0].insert(buf[0].end(), chunk.e.begin(), chunk.e.end());
        buf[1].insert(buf[1].end(), chunk.n.begin(), chunk.n.end());
        buf[2].insert(buf[2].end(), chunk.z.begin(), chunk.z.end());
        mask[0].insert(mask[0].end(), chunk.eInterp.begin(),
                       chunk.eInterp.end());
        mask[1].insert(mask[1].end(), chunk.nInterp.begin(),
                       chunk.nInterp.end());
        mask[2].insert(mask[2].end(), chunk.zInterp.begin(),
                       chunk.zInterp.end());
        for (int c = 0; c < 3; ++c)
          if (buf[c].size() != mask[c].size() || buf[c].size() != buf[0].size())
            throw std::runtime_error("component buffers desynchronised");

        const auto [starts, cursor] =
            windowStarts(int(buf[0].size()), win, step, pendingSkip);
        Config perWindow = cfg;
        perWindow.fs = fsOut;
        for (int start : starts) {
          if (stopping(hooks))
            return;
          const int end = start + win;
          const double timeMinutes = (bufEpoch + double(end) / fsOut) / 60.0;
          std::array<std::vector<double>, 3> w;
          std::array<double, 3> interp{};
          for (int c = 0; c < 3; ++c) {
            w[c].assign(buf[c].begin() + start, buf[c].begin() + end);
            int fabricated = 0;
            for (int i = start; i < end; ++i)
              fabricated += mask[c][size_t(i)] ? 1 : 0;
            interp[c] = double(fabricated) / double(win);
          }
          const FeatureRow features =
              computeWindow(w[0], w[1], w[2], perWindow);
          if (!writer)
            writer =
                std::make_unique<FeatureWriter>(featureNames(cfg.chaosOnly));
          writer->addWindow(QStringLiteral("%1_w%2").arg(stem).arg(
                                ++windowIdx, 2, 10, QLatin1Char('0')),
                            timeMinutes, features, interp);
        }
        pendingSkip = std::max(0, cursor - int(buf[0].size()));
        const int drop = std::min(cursor, int(buf[0].size()));
        if (drop > 0) {
          for (auto &b : buf)
            b.erase(b.begin(), b.begin() + drop);
          for (auto &m : mask)
            m.erase(m.begin(), m.begin() + drop);
          bufEpoch += double(drop) / fsOut;
        }
        if (hooks.progress)
          hooks.progress(chunk.recordsDone, chunk.recordsTotal);
        phases.report(chunk.recordsTotal > 0 ? double(chunk.recordsDone) /
                                                   chunk.recordsTotal
                                             : 0.0,
                      records(chunk.recordsDone, chunk.recordsTotal));
      });

  result.windows = windowIdx;
  if (!complete || stopping(hooks)) {
    result.stopped = true;
    say(hooks, QStringLiteral(" Stopped after %1 windows; nothing written")
                   .arg(windowIdx));
    return result;
  }
  if (!writer) {
    say(hooks, QStringLiteral(" Warning: not enough data for a single window"));
    return result;
  }
  Config finalCfg = cfg;
  finalCfg.fs = fsOut;
  writer->finish(base, finalCfg);
  result.featuresCsv = base + QStringLiteral(".csv");
  result.featuresNpy = base + QStringLiteral(".npy");
  result.metadata = QDir(cfg.outputRoot)
                        .filePath(stem + QStringLiteral("_run_metadata.json"));
  say(hooks, QStringLiteral(" Windows processed: %1").arg(windowIdx));
  phases.finish();
  return result;
}

RunResult runPreprocessOnly(const Config &cfg, const RunHooks &hooks) {
  if (!QFileInfo::exists(cfg.dataDir))
    throw std::runtime_error(
        ("data directory not found: " + cfg.dataDir).toStdString());
  say(hooks, QStringLiteral(" Input: %1").arg(cfg.dataDir));
  const QString base =
      QDir(cfg.outputRoot)
          .filePath(outputStem(cfg) + QStringLiteral("_preprocessed"));
  QDir().mkpath(cfg.outputRoot);
  PreprocessOptions popt;
  popt.log = hooks.log;
  popt.shouldStop = hooks.shouldStop;
  popt.stagingRoot = cfg.outputRoot;

  const bool archives = hasArchives(listInputs(cfg.dataDir, cfg));
  Phases phases(hooks);
  if (archives)
    phases.add(QStringLiteral("Unpacking the archives"), kWeightUnpack);
  phases.add(QStringLiteral("Preprocessing"), kWeightExtract);
  RunResult result;
  Inputs unpacked;
  if (archives && !unpackOnce(cfg, popt, phases, 0, unpacked)) {
    result.stopped = true;
    return result;
  }
  phases.begin(archives ? 1 : 0);
  RawBinaryWriter writer(base);
  const bool complete =
      preprocessDirectory(cfg.dataDir, cfg, popt, [&](ChannelChunk &&chunk) {
        if (chunk.e.size() != chunk.n.size() ||
            chunk.e.size() != chunk.z.size())
          say(hooks, QStringLiteral(" Warning: channel lengths differ in a "
                                    "chunk (E %1, N %2, Z %3)")
                         .arg(chunk.e.size())
                         .arg(chunk.n.size())
                         .arg(chunk.z.size()));
        writer.writeChunk(chunk.e, chunk.n, chunk.z, chunk.fs,
                          chunk.startEpoch);
        if (hooks.progress)
          hooks.progress(chunk.recordsDone, chunk.recordsTotal);
        phases.report(chunk.recordsTotal > 0 ? double(chunk.recordsDone) /
                                                   chunk.recordsTotal
                                             : 0.0,
                      QStringLiteral("%1/%2 records")
                          .arg(chunk.recordsDone)
                          .arg(chunk.recordsTotal));
      });
  if (complete)
    phases.finish();
  result.samples = writer.finish(cfg);
  result.rawF32 = writer.path();
  result.rawSidecar = writer.path() + QStringLiteral(".json");
  if (!complete) {
    result.stopped = true;
    say(hooks, QStringLiteral(" Stopped after %1 samples; output is partial")
                   .arg(result.samples));
    return result;
  }
  if (result.samples == 0)
    say(hooks,
        QStringLiteral(" Warning: no samples produced during preprocessing"));
  else
    say(hooks, QStringLiteral(" %1 samples x 3 channels -> %2 (%3 GB)")
                   .arg(result.samples)
                   .arg(result.rawF32)
                   .arg(double(result.samples) * 12.0 / 1e9, 0, 'f', 2));
  return result;
}

} // namespace skg::fx
