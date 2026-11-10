#include "fx/preprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

#include "fx/dsp.h"
#include "fx/mseed.h"
#include "miniz.h"
#include <QSet>
#include <utility>

namespace skg::fx {

namespace {
constexpr double kBlockSeconds = 3600.0;
constexpr double kWatermarkSeconds = 60.0;
constexpr double kFilterContextSeconds = 120.0;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
const char kComponents[3] = {'E', 'N', 'Z'};
} // namespace

double filterContextSeconds(double freqmin) {
  return std::max(kFilterContextSeconds, settleSeconds(freqmin));
}

Inputs::~Inputs() {
  if (!stagingDir.isEmpty())
    QDir(stagingDir).removeRecursively();
}

Inputs expandInputs(const QStringList &listed, const QString &stagingRoot) {
  Inputs in;
  QStringList zips;
  for (const QString &p : listed)
    (p.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) ? zips : in.files)
        << p;
  if (zips.isEmpty())
    return in;
  // Leftovers of interrupted runs whose process is gone.
  const QDir root(stagingRoot);
  for (const QString &d : root.entryList({QStringLiteral(".unzipped-*")},
                                         QDir::Dirs | QDir::Hidden)) {
    const qint64 pid = d.mid(10).toLongLong();
    if (pid > 0 && !QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))
      QDir(root.filePath(d)).removeRecursively();
  }
  in.stagingDir = root.filePath(
      QStringLiteral(".unzipped-%1").arg(QCoreApplication::applicationPid()));
  QDir().mkpath(in.stagingDir);
  for (const QString &zipPath : zips) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipPath.toLocal8Bit().constData(), 0))
      throw std::runtime_error(("failed to read zip " + zipPath).toStdString());
    const QString stem = QFileInfo(zipPath).completeBaseName();
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
      if (mz_zip_reader_is_file_a_directory(&zip, i))
        continue;
      char nameBuf[1024];
      mz_zip_reader_get_filename(&zip, i, nameBuf, sizeof nameBuf);
      const QString name = QFileInfo(QString::fromUtf8(nameBuf)).fileName();
      const QString dest = in.stagingDir + '/' + stem + "__" + name;
      if (!mz_zip_reader_extract_to_file(&zip, i,
                                         dest.toLocal8Bit().constData(), 0)) {
        mz_zip_reader_end(&zip);
        throw std::runtime_error(
            ("failed to inflate " + name + " from " + zipPath).toStdString());
      }
      in.files << dest;
    }
    mz_zip_reader_end(&zip);
  }
  if (in.files.isEmpty())
    throw std::runtime_error("the input zips contain no files");
  return in;
}

std::pair<std::vector<double>, std::vector<bool>>
processComponent(std::vector<double> data, double freqmin, double freqmax,
                 double fs, int decimationFactor, int skip, int end) {
  if (data.empty())
    return {};
  const std::vector<bool> interpolated = interpolateGaps(data);
  detrendLinear(data);
  std::vector<size_t> missing;
  for (size_t i = 0; i < data.size(); ++i)
    if (std::isnan(data[i])) {
      missing.push_back(i);
      data[i] = 0.0;
    }
  end = std::min<int>(end, int(data.size()));
  skip = std::min(skip, end);
  if (missing.size() == data.size()) {
    const int kept =
        (std::max(0, end - skip) + decimationFactor - 1) / decimationFactor;
    return {std::vector<double>(kept, kNaN), std::vector<bool>(kept, false)};
  }
  const BandPass filter(4, fs, freqmin, freqmax);
  const int padlen = int(std::ceil(settleSeconds(freqmin) * fs));
  std::vector<double> filtered = filter.filtfilt(data, padlen);
  for (size_t i : missing)
    filtered[i] = kNaN;
  std::vector<double> out;
  std::vector<bool> mask;
  for (int i = skip; i < end; i += decimationFactor) {
    out.push_back(filtered[i]);
    mask.push_back(interpolated[i]);
  }
  return {out, mask};
}

namespace {

struct Assembler {
  double startEpoch = kNaN;
  double fs = 0;
  std::map<char, std::vector<double>> comps;
  double watermark = -std::numeric_limits<double>::infinity();
  bool haveContext = false;
  std::map<char, std::vector<double>> context;
  size_t globalOffset = 0;
  int droppedLate = 0;
  int recordsDone = 0, recordsTotal = 0;
};

void flushBlock(Assembler &a, int blockLen, const Config &cfg, int decimation,
                double outFs, const ChunkSink &sink) {
  size_t available = 0;
  for (const auto &[c, v] : a.comps)
    available = std::max(available, v.size());
  if (available == 0) {
    for (auto &[c, v] : a.comps)
      v.clear();
    return;
  }
  const size_t take = std::min<size_t>(size_t(blockLen), available);
  for (auto &[c, v] : a.comps)
    if (v.size() < take)
      v.resize(take, kNaN);
  size_t lead = 0;
  if (a.haveContext) {
    lead = std::numeric_limits<size_t>::max();
    for (const auto &[c, v] : a.context)
      lead = std::min(lead, v.size());
  }
  const size_t contextLen = size_t(filterContextSeconds(cfg.freqmin) * a.fs);
  const size_t carried = std::min(contextLen, take);
  std::map<char, std::vector<double>> nextContext;
  for (char c : kComponents) {
    const std::vector<double> &v = a.comps[c];
    nextContext[c] =
        std::vector<double>(v.begin() + (take - carried), v.begin() + take);
  }
  const int phase =
      int((decimation - int(a.globalOffset % decimation)) % decimation);
  ChannelChunk chunk;
  for (char c : kComponents) {
    std::vector<double> combined;
    if (lead > 0) {
      const std::vector<double> &ctx = a.context[c];
      combined.insert(combined.end(), ctx.end() - lead, ctx.end());
    }
    const std::vector<double> &v = a.comps[c];
    const size_t ahead = std::min(v.size(), take + contextLen);
    combined.insert(combined.end(), v.begin(), v.begin() + ahead);
    auto [samples, mask] =
        processComponent(std::move(combined), cfg.freqmin, cfg.freqmax, a.fs,
                         decimation, int(lead) + phase, int(lead + take));
    if (c == 'E') {
      chunk.e = std::move(samples);
      chunk.eInterp = std::move(mask);
    } else if (c == 'N') {
      chunk.n = std::move(samples);
      chunk.nInterp = std::move(mask);
    } else {
      chunk.z = std::move(samples);
      chunk.zInterp = std::move(mask);
    }
  }
  chunk.fs = outFs;
  chunk.startEpoch = a.startEpoch;
  chunk.recordsDone = a.recordsDone;
  chunk.recordsTotal = a.recordsTotal;
  sink(std::move(chunk));
  for (auto &[c, v] : a.comps)
    v.erase(v.begin(), v.begin() + take);
  a.startEpoch += double(take) / a.fs;
  a.globalOffset += take;
  a.context = std::move(nextContext);
  a.haveContext = true;
}

} // namespace

bool preprocessDirectory(const QString &dataDir, const Config &cfg,
                         const PreprocessOptions &opt, const ChunkSink &sink) {
  auto say = [&](const QString &m) {
    if (opt.log)
      opt.log(m);
  };
  auto stopping = [&] { return opt.shouldStop && opt.shouldStop(); };
  QStringList listed;
  const QFileInfo info(dataDir);
  const bool chosen = !cfg.inputFiles.isEmpty();
  if (chosen)
    listed = cfg.inputFiles;
  else if (info.isFile())
    listed << dataDir;
  else
    for (const QFileInfo &f :
         QDir(dataDir).entryInfoList(QDir::Files, QDir::Name))
      listed << f.filePath();
  if (listed.isEmpty())
    throw std::runtime_error(("no files found in " + dataDir).toStdString());
  Inputs inputs = expandInputs(
      listed, opt.stagingRoot.isEmpty() ? cfg.outputRoot : opt.stagingRoot);
  inputs.files.sort();

  // Index every record before any is read.
  QList<RecordRef> index;
  double nativeFs = 0;
  QSet<char> seen;
  for (int fi = 0; fi < inputs.files.size(); ++fi) {
    if (stopping())
      return false;
    {
      // Anything beside the waveforms (catalogs, notes, sidecars) is skipped,
      // not fatal; a file that stops being miniSEED half-way still is.
      QFile peek(inputs.files[fi]);
      if (!peek.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            ("cannot open " + inputs.files[fi]).toStdString());
      if (!parseHeader(peek.read(64)).valid) {
        say(QStringLiteral(" Skipping %1: not miniSEED")
                .arg(QFileInfo(inputs.files[fi]).fileName()));
        continue;
      }
    }
    for (RecordRef &r : indexFile(inputs.files[fi], fi)) {
      // A chosen file may hold other stations beside the one asked for.
      if (chosen && r.channelId.section(QLatin1Char('.'), 1, 1)
                            .compare(cfg.station, Qt::CaseInsensitive) != 0)
        continue;
      if (nativeFs == 0)
        nativeFs = r.rate;
      else if (std::abs(nativeFs - r.rate) > 1e-6)
        throw std::runtime_error(
            QStringLiteral(
                "mixed sample rates (%1 and %2 Hz) in the input; not supported")
                .arg(nativeFs)
                .arg(r.rate)
                .toStdString());
      seen.insert(r.component);
      index << r;
    }
  }
  if (nativeFs == 0)
    throw std::runtime_error(
        "no E/N/Z component data found in the input files");
  for (char c : kComponents)
    if (!seen.contains(c))
      throw std::runtime_error(
          QStringLiteral("component '%1' not found in the input files; need "
                         "all of E, N, Z")
              .arg(c)
              .toStdString());
  std::sort(index.begin(), index.end(),
            [](const RecordRef &a, const RecordRef &b) {
              return a.startEpoch < b.startEpoch;
            });
  const QString decimationWarning = cfg.checkDecimation(nativeFs);
  if (!decimationWarning.isEmpty())
    say(QStringLiteral("[WARNING] ") + decimationWarning);

  Assembler a;
  a.fs = nativeFs;
  for (char c : kComponents)
    a.comps[c] = {};
  a.recordsTotal = index.size();
  const int decimation = std::max(1, int(nativeFs / cfg.fs));
  const double outFs = nativeFs / decimation;
  const int blockLen = int(kBlockSeconds * nativeFs);
  const double lag =
      std::max(kWatermarkSeconds, filterContextSeconds(cfg.freqmin));

  int openFile = -1;
  QFile file;
  for (int i = 0; i < index.size(); ++i) {
    if (stopping())
      return false;
    const RecordRef &r = index[i];
    if (openFile != r.file) {
      file.close();
      file.setFileName(inputs.files[r.file]);
      if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            ("cannot open " + inputs.files[r.file]).toStdString());
      openFile = r.file;
    }
    file.seek(r.offset);
    const QByteArray rec = file.read(r.length);
    const RecordHeader h = parseHeader(rec);
    const std::vector<double> samples = decodeRecord(rec, h);
    const size_t count = std::min<size_t>(samples.size(), size_t(r.count));
    if (count == 0)
      continue;
    if (std::isnan(a.startEpoch))
      a.startEpoch = r.startEpoch;
    if (r.startEpoch - a.startEpoch > 2.0 * kBlockSeconds) {
      // A jump of more than two blocks: finish this stretch and restart the
      // clock.
      bool any = true;
      while (any) {
        any = false;
        for (const auto &[c, v] : a.comps)
          any = any || !v.empty();
        if (any)
          flushBlock(a, blockLen, cfg, decimation, outFs, sink);
      }
      a.startEpoch = r.startEpoch;
      a.watermark = -std::numeric_limits<double>::infinity();
      a.context.clear();
      a.haveContext = false;
    }
    const qint64 idx =
        qint64(std::llround((r.startEpoch - a.startEpoch) * nativeFs));
    if (idx < 0) {
      ++a.droppedLate;
      continue;
    }
    std::vector<double> &dest = a.comps[r.component];
    if (dest.size() < size_t(idx) + count)
      dest.resize(size_t(idx) + count, kNaN);
    std::copy(samples.begin(), samples.begin() + count, dest.begin() + idx);
    a.recordsDone = i + 1;
    a.watermark =
        std::max(a.watermark, r.startEpoch + (double(count) - 1.0) / nativeFs);
    while (a.watermark >= a.startEpoch + kBlockSeconds + lag)
      flushBlock(a, blockLen, cfg, decimation, outFs, sink);
  }
  a.recordsDone = a.recordsTotal;
  bool any = true;
  while (any) {
    any = false;
    for (const auto &[c, v] : a.comps)
      any = any || !v.empty();
    if (any)
      flushBlock(a, blockLen, cfg, decimation, outFs, sink);
  }
  if (a.droppedLate > 0)
    say(QStringLiteral("[WARNING] skipped %1 record(s) that arrived after "
                       "their block had been "
                       "emitted; input may not be in time order")
            .arg(a.droppedLate));
  return true;
}

} // namespace skg::fx
