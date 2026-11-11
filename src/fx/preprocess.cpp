#include "fx/preprocess.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

#include "fx/dsp.h"
#include "fx/mseed.h"
#include "miniz.h"
#include "core/nativepath.h"
#include <QMap>
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

Inputs::Inputs() = default;
Inputs::Inputs(Inputs &&) noexcept = default;
Inputs &Inputs::operator=(Inputs &&other) noexcept {
  if (this != &other) {
    lock.reset();
    if (!stagingDir.isEmpty())
      QDir(stagingDir).removeRecursively();
    files = std::move(other.files);
    stagingDir = std::move(other.stagingDir);
    other.stagingDir.clear();
    lock = std::move(other.lock);
  }
  return *this;
}

Inputs::~Inputs() {
  lock.reset(); // the lock file goes with the folder
  if (!stagingDir.isEmpty())
    QDir(stagingDir).removeRecursively();
}

namespace {
const QString kStagingLock = QStringLiteral(".lock");

// Staging folders nothing uses any more. One in use holds its lock file
// locked (QLockFile knows a lock whose process is gone from one that is
// held, on every platform); a folder without a lock file is from an older
// version of the application, which named them by process id, and is
// removed once it has not been touched for an hour.
void removeDeadStaging(const QDir &root) {
  for (const QFileInfo &d :
       root.entryInfoList({QStringLiteral(".unzipped-*")},
                          QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
    const QString lockPath = QDir(d.filePath()).filePath(kStagingLock);
    if (!QFileInfo::exists(lockPath)) {
      if (d.lastModified().secsTo(QDateTime::currentDateTime()) > 3600)
        QDir(d.filePath()).removeRecursively();
      continue;
    }
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0); // by its process alone, never by its age
    if (!lock.tryLock(0))
      continue; // in use, by this process or another
    lock.unlock();
    QDir(d.filePath()).removeRecursively();
  }
}
} // namespace

bool hasArchives(const QStringList &listed) {
  for (const QString &p : listed)
    if (p.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
      return true;
  return false;
}

QStringList listInputs(const QString &dataDir, const Config &cfg) {
  QStringList listed;
  const QFileInfo info(dataDir);
  if (!cfg.inputFiles.isEmpty())
    listed = cfg.inputFiles;
  else if (info.isFile())
    listed << dataDir;
  else
    for (const QFileInfo &f :
         QDir(dataDir).entryInfoList(QDir::Files, QDir::Name))
      listed << f.filePath();
  if (listed.isEmpty())
    throw std::runtime_error(("no files found in " + dataDir).toStdString());
  return listed;
}

Inputs prepareInputs(const QString &dataDir, const Config &cfg,
                     const PreprocessOptions &opt) {
  Inputs inputs = expandInputs(
      listInputs(dataDir, cfg),
      opt.stagingRoot.isEmpty() ? cfg.outputRoot : opt.stagingRoot,
      opt.unpacking, opt.shouldStop);
  inputs.files.sort();
  return inputs;
}

namespace {
// One archive member written to a file as it is inflated, so that the
// unpacking can report how far it is and be stopped.
struct Unpack {
  QFile out;
  qint64 done = 0, total = 0;
  const std::function<void(qint64, qint64)> *progress = nullptr;
  const StopFn *stop = nullptr;
  qint64 sinceReport = 0;
  bool stopped = false;
};
size_t writeUnpacked(void *opaque, mz_uint64, const void *buf, size_t n) {
  auto *u = static_cast<Unpack *>(opaque);
  if (u->stop && *u->stop && (*u->stop)()) {
    u->stopped = true;
    return 0;
  }
  if (u->out.write(static_cast<const char *>(buf), qint64(n)) != qint64(n))
    return 0;
  u->done += qint64(n);
  u->sinceReport += qint64(n);
  if (u->sinceReport >= (8 << 20) && u->progress && *u->progress) {
    u->sinceReport = 0;
    (*u->progress)(u->done, u->total);
  }
  return n;
}
} // namespace

Inputs expandInputs(const QStringList &listed, const QString &stagingRoot,
                    const std::function<void(qint64, qint64)> &unpacking,
                    const StopFn &shouldStop) {
  Inputs in;
  QStringList zips;
  for (const QString &p : listed)
    (p.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) ? zips : in.files)
        << p;
  if (zips.isEmpty())
    return in;
  // Leftovers of interrupted runs, then a folder of this call's own.
  const QDir root(stagingRoot);
  QDir().mkpath(stagingRoot);
  removeDeadStaging(root);
  {
    QTemporaryDir made(root.filePath(QStringLiteral(".unzipped-XXXXXX")));
    made.setAutoRemove(false);
    if (!made.isValid())
      throw std::runtime_error(
          QStringLiteral("cannot create a folder to unpack the archives in "
                         "under %1: %2")
              .arg(stagingRoot, made.errorString())
              .toStdString());
    in.stagingDir = made.path();
  }
  in.lock = std::make_unique<QLockFile>(
      QDir(in.stagingDir).filePath(kStagingLock));
  in.lock->setStaleLockTime(0);
  if (!in.lock->tryLock(0))
    throw std::runtime_error(
        ("cannot lock the unpacking folder " + in.stagingDir).toStdString());
  // How much there is to write, for the progress: the archives' directories
  // say it without reading the members.
  Unpack unpack;
  unpack.progress = &unpacking;
  unpack.stop = &shouldStop;
  for (const QString &zipPath : zips) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, minizPath(zipPath).constData(), 0))
      throw std::runtime_error(("failed to read zip " + zipPath).toStdString());
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
      mz_zip_archive_file_stat st{};
      if (!mz_zip_reader_is_file_a_directory(&zip, i) &&
          mz_zip_reader_file_stat(&zip, i, &st))
        unpack.total += qint64(st.m_uncomp_size);
    }
    mz_zip_reader_end(&zip);
  }
  if (unpacking)
    unpacking(0, unpack.total);
  for (const QString &zipPath : zips) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, minizPath(zipPath).constData(), 0))
      throw std::runtime_error(("failed to read zip " + zipPath).toStdString());
    const QString stem = QFileInfo(zipPath).completeBaseName();
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
      if (mz_zip_reader_is_file_a_directory(&zip, i))
        continue;
      char nameBuf[1024];
      mz_zip_reader_get_filename(&zip, i, nameBuf, sizeof nameBuf);
      const QString name = QFileInfo(QString::fromUtf8(nameBuf)).fileName();
      // The member's number keeps apart members of one name in different
      // folders of the archive (2023/036/X.mseed and 2023/037/X.mseed).
      const QString dest = in.stagingDir + '/' + stem + "__" +
                           QString::number(i) + "__" + name;
      unpack.out.setFileName(dest);
      const bool opened = unpack.out.open(QIODevice::WriteOnly);
      const bool ok =
          opened && mz_zip_reader_extract_to_callback(&zip, i, writeUnpacked,
                                                      &unpack, 0);
      unpack.out.close();
      if (!ok) {
        mz_zip_reader_end(&zip);
        if (unpack.stopped)
          throw Stopped();
        throw std::runtime_error(
            ("failed to inflate " + name + " from " + zipPath).toStdString());
      }
      in.files << dest;
    }
    mz_zip_reader_end(&zip);
  }
  if (unpacking)
    unpacking(unpack.total, unpack.total);
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

QString isoOfEpoch(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(epoch * 1000.0)),
                                        QTimeZone::UTC)
      .toString(Qt::ISODate);
}

// "NET.STA.LOC.HH" as people write it: NET.STA.LOC.HH?
QString familyText(const QString &family) {
  return family + QLatin1Char('?');
}

// The one sensor a pass reads, of those the indexed records belong to.
ChannelChoice chooseChannels(const std::vector<RecordRef> &index,
                             const ChannelTable &table, const Config &cfg) {
  struct Family {
    double samples = 0;
    QSet<char> components;
    QStringList channels;
  };
  QMap<QString, Family> families;
  for (const RecordRef &r : index) {
    const ChannelTable::Channel &ch = table.channels[r.channel];
    Family &f = families[ch.family()];
    f.samples += double(r.count);
    f.components.insert(ch.component);
    if (!f.channels.contains(ch.id))
      f.channels << ch.id;
  }
  const auto complete = [](const Family &f) {
    return f.components.contains('E') && f.components.contains('N') &&
           f.components.contains('Z');
  };
  QStringList all;
  for (auto it = families.constBegin(); it != families.constEnd(); ++it)
    all << familyText(it.key());

  // The families the configuration allows: all of them, or those its
  // `channels` names ("HH", "HH?", "00.HH?").
  QStringList allowed;
  QString wanted = cfg.channels.trimmed().toUpper();
  while (wanted.endsWith(QLatin1Char('?')) || wanted.endsWith(QLatin1Char('*')))
    wanted.chop(1);
  if (wanted.isEmpty()) {
    allowed = families.keys();
  } else {
    const QStringList parts = wanted.split(QLatin1Char('.'));
    const QString code = parts.last().left(2);
    const bool withLocation = parts.size() >= 2;
    const QString location = withLocation ? parts[parts.size() - 2] : QString();
    for (auto it = families.constBegin(); it != families.constEnd(); ++it) {
      const QStringList f = it.key().split(QLatin1Char('.'));
      if (f.value(3).compare(code, Qt::CaseInsensitive) != 0)
        continue;
      if (withLocation && f.value(2).compare(location, Qt::CaseInsensitive) != 0)
        continue;
      allowed << it.key();
    }
    if (allowed.isEmpty())
      throw std::runtime_error(
          QStringLiteral("no %1 channels in the input files: the data holds "
                         "%2. Change the \"channels\" setting, or clear it")
              .arg(cfg.channels, all.join(QStringLiteral(", ")))
              .toStdString());
  }
  // Of those, the ones with all three components; the most data wins.
  QString best;
  int candidates = 0;
  for (const QString &name : std::as_const(allowed)) {
    const Family &f = families[name];
    if (!complete(f))
      continue;
    ++candidates;
    if (best.isEmpty() || f.samples > families[best].samples)
      best = name;
  }
  if (best.isEmpty()) {
    // The old message when there is one sensor with a component missing.
    if (allowed.size() == 1)
      for (char c : kComponents)
        if (!families[allowed.first()].components.contains(c))
          throw std::runtime_error(
              QStringLiteral("component '%1' not found in the input files; "
                             "need all of E, N, Z")
                  .arg(c)
                  .toStdString());
    throw std::runtime_error(
        QStringLiteral("no sensor in the input files has all of E, N and Z: "
                       "the data holds %1")
            .arg(all.join(QStringLiteral(", ")))
            .toStdString());
  }
  ChannelChoice choice;
  choice.family = best;
  choice.used = families[best].channels;
  choice.used.sort();
  for (auto it = families.constBegin(); it != families.constEnd(); ++it)
    if (it.key() != best)
      choice.dropped << familyText(it.key());
  choice.guessed = wanted.isEmpty() && candidates > 1;
  choice.how = familyText(best);
  if (!choice.dropped.isEmpty())
    choice.how +=
        (choice.guessed
             ? QStringLiteral(" (the sensor with the most data; left out: %1 "
                              "- name the one to read in the \"channels\" "
                              "setting)")
             : candidates > 1
                   ? QStringLiteral(" (as set; left out: %1)")
                   : QStringLiteral(" (left out: %1)"))
            .arg(choice.dropped.join(QStringLiteral(", ")));
  return choice;
}

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
  const bool chosen = !cfg.inputFiles.isEmpty();
  // The caller's files when it has prepared them (and keeps them for
  // another pass); otherwise listed and unpacked here, for this pass alone.
  Inputs own;
  if (!opt.prepared) {
    try {
      own = prepareInputs(dataDir, cfg, opt);
    } catch (const std::runtime_error &e) {
      if (stopping() && QByteArray(e.what()) == "stopped")
        return false;
      throw;
    }
  }
  const Inputs &inputs = opt.prepared ? *opt.prepared : own;

  // Index every record before any is read: those of the station, inside
  // the period asked for.
  std::vector<RecordRef> index;
  ChannelTable table;
  double nativeFs = 0;
  qint64 outsidePeriod = 0;
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
    // Which channels of the table are the station's, looked up once each.
    std::vector<qint8> ofStation;
    for (const RecordRef &r : indexFile(inputs.files[fi], fi, table)) {
      const ChannelTable::Channel &ch = table.channels[r.channel];
      // A chosen file may hold other stations beside the one asked for.
      if (chosen) {
        if (ofStation.size() <= size_t(r.channel))
          ofStation.resize(size_t(r.channel) + 1, qint8(-1));
        if (ofStation[r.channel] < 0)
          ofStation[r.channel] =
              ch.id.section(QLatin1Char('.'), 1, 1)
                      .compare(cfg.station, Qt::CaseInsensitive) == 0
                  ? 1
                  : 0;
        if (ofStation[r.channel] == 0)
          continue;
      }
      // Outside the period: not read at all (a record across one of its
      // ends is cut to it when it is read, below).
      if ((cfg.endEpoch && r.startEpoch >= *cfg.endEpoch) ||
          (cfg.startEpoch &&
           r.startEpoch + double(r.count) / ch.rate <= *cfg.startEpoch)) {
        ++outsidePeriod;
        continue;
      }
      index.push_back(r);
    }
  }
  if (index.empty()) {
    if (outsidePeriod > 0)
      throw NoDataInPeriod(
          QStringLiteral("the input files hold no data of %1 between %2 and "
                         "%3: all of their %4 record(s) lie outside that "
                         "period")
              .arg(cfg.station,
                   cfg.startEpoch ? isoOfEpoch(*cfg.startEpoch)
                                  : QStringLiteral("their start"),
                   cfg.endEpoch ? isoOfEpoch(*cfg.endEpoch)
                                : QStringLiteral("their end"))
              .arg(outsidePeriod)
              .toStdString());
    throw std::runtime_error(
        "no E/N/Z component data found in the input files");
  }
  // One sensor only. Records are placed by time alone, so two sensors of a
  // station (a broadband and an accelerometer, or two location codes) would
  // be written over each other, sample by sample.
  const ChannelChoice choice = chooseChannels(index, table, cfg);
  {
    std::vector<char> keep(size_t(table.channels.size()), 0);
    for (int i = 0; i < table.channels.size(); ++i)
      keep[size_t(i)] = table.channels[i].family() == choice.family;
    index.erase(std::remove_if(index.begin(), index.end(),
                               [&keep](const RecordRef &r) {
                                 return !keep[r.channel];
                               }),
                index.end());
    index.shrink_to_fit();
    for (int i = 0; i < table.channels.size(); ++i) {
      if (!keep[size_t(i)])
        continue;
      const double rate = table.channels[i].rate;
      if (nativeFs == 0)
        nativeFs = rate;
      else if (std::abs(nativeFs - rate) > 1e-6)
        throw std::runtime_error(
            QStringLiteral("mixed sample rates (%1 and %2 Hz) in the %3? "
                           "channels; not supported")
                .arg(nativeFs)
                .arg(rate)
                .arg(choice.family)
                .toStdString());
    }
  }
  say(QStringLiteral(" Channels: %1").arg(choice.how));
  if (opt.channels)
    *opt.channels = choice;
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
  a.recordsTotal = int(index.size());
  const int decimation = std::max(1, int(nativeFs / cfg.fs));
  const double outFs = nativeFs / decimation;
  const int blockLen = int(kBlockSeconds * nativeFs);
  const double lag =
      std::max(kWatermarkSeconds, filterContextSeconds(cfg.freqmin));

  int openFile = -1;
  QFile file;
  for (int i = 0; i < int(index.size()); ++i) {
    if (stopping())
      return false;
    const RecordRef &r = index[size_t(i)];
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
    size_t count = std::min<size_t>(samples.size(), size_t(r.count));
    // A record across an end of the period is cut to it: nothing at or
    // after `endEpoch`, nothing before `startEpoch`.
    size_t first = 0;
    if (cfg.startEpoch && r.startEpoch < *cfg.startEpoch)
      first = size_t(
          std::ceil((*cfg.startEpoch - r.startEpoch) * nativeFs - 1e-6));
    if (cfg.endEpoch)
      count = std::min<size_t>(
          count, size_t(std::max(0.0, std::ceil((*cfg.endEpoch - r.startEpoch) *
                                                    nativeFs -
                                                1e-6))));
    if (first >= count)
      continue;
    const double recordStart = r.startEpoch + double(first) / nativeFs;
    count -= first;
    if (std::isnan(a.startEpoch))
      a.startEpoch = recordStart;
    if (recordStart - a.startEpoch > 2.0 * kBlockSeconds) {
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
      a.startEpoch = recordStart;
      a.watermark = -std::numeric_limits<double>::infinity();
      a.context.clear();
      a.haveContext = false;
    }
    const qint64 idx =
        qint64(std::llround((recordStart - a.startEpoch) * nativeFs));
    if (idx < 0) {
      ++a.droppedLate;
      continue;
    }
    std::vector<double> &dest = a.comps[table.channels[r.channel].component];
    if (dest.size() < size_t(idx) + count)
      dest.resize(size_t(idx) + count, kNaN);
    std::copy(samples.begin() + first, samples.begin() + first + count,
              dest.begin() + idx);
    a.recordsDone = i + 1;
    a.watermark =
        std::max(a.watermark, recordStart + (double(count) - 1.0) / nativeFs);
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
