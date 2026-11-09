#include "fx/engine.h"

#include <QDir>
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

  RunResult result;
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
    if (hooks.phase)
      hooks.phase(
          QStringLiteral("Calibrating chaos parameters on quiet periods"));
    cal = calibrate(cfg, popt, hooks.progress);
  }
  for (const QString &line : cal.describe())
    say(hooks, line);
  cal.apply(cfg);
  result.calibration =
      QDir(cfg.outputRoot).filePath(stem + QStringLiteral("_calibration.json"));
  if (!cal.save(result.calibration))
    throw std::runtime_error(
        ("cannot write " + result.calibration).toStdString());
  if (stopping(hooks)) {
    result.stopped = true;
    return result;
  }
  if (hooks.phase)
    hooks.phase(QStringLiteral("Extracting features"));

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

  RunResult result;
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
      });
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
