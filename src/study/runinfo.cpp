#include "study/runinfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <functional>

#include "miniz.h"

#include "fx/preprocess.h"
#include "study/waveindex.h"

namespace skg::study {

namespace {

const QString kCaseFolder = QStringLiteral("_case");
const QString kTmp = QStringLiteral(".tmp");
const QString kFailed = QStringLiteral(".failed");
const QString kCalibration = QStringLiteral(".calibration");
const QString kUnpackedPrefix = QStringLiteral(".unzipped-");

QString clean(const QString &dir) {
  return QDir::cleanPath(QDir(dir).absolutePath());
}

QMutex &liveMutex() {
  static QMutex m;
  return m;
}
QHash<QString, int> &liveSet() {
  static QHash<QString, int> s;
  return s;
}

QStringList subFolders(const QString &dir) {
  return QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                             QDir::Name);
}

const StationRef *stationOf(const Case &c, const QString &code) {
  for (const StationRef &s : c.stations)
    if (s.code == code)
      return &s;
  return nullptr;
}

// The run folder a working, failed or calibration folder stands beside.
QString baseOf(const QString &name, QString *suffix) {
  for (const QString &s : {kTmp, kFailed, kCalibration})
    if (name.endsWith(s)) {
      *suffix = s;
      return name.left(name.size() - s.size());
    }
  suffix->clear();
  return name;
}

} // namespace

// ---------------------------------------------------------------- activity

QList<Activity> activity(const Study &s) {
  QList<Activity> out;
  const auto take = [&](const Case &c, const QString &station, const Recipe &r,
                        const QString &dir) {
    const std::optional<RunRecord> rec = RunRecord::read(dir);
    if (!rec || !rec->started.isValid())
      return;
    Activity a;
    a.caseId = c.id;
    a.caseName = c.name;
    a.station = station;
    a.recipeId = r.id;
    a.recipeName = r.name;
    a.tool = r.tool;
    a.started = rec->started;
    a.finished = rec->finished;
    a.ok = rec->ok;
    a.error = rec->error;
    a.warnings = rec->warnings;
    a.dir = dir;
    out << a;
  };
  for (const Case &c : s.cases)
    for (const Recipe &r : s.recipes) {
      QStringList stations;
      if (perStation(r.tool))
        for (const StationRef &st : c.stations)
          stations << st.code;
      else
        stations << QString();
      for (const QString &station : stations) {
        const QString dir = runDir(s, c, station, r);
        take(c, station, r, dir);
        take(c, station, r, runFailedDir(dir));
      }
    }
  std::sort(out.begin(), out.end(), [](const Activity &a, const Activity &b) {
    const QDateTime x = a.finished.isValid() ? a.finished : a.started;
    const QDateTime y = b.finished.isValid() ? b.finished : b.started;
    return x > y;
  });
  return out;
}

// --------------------------------------------------------------- live runs

void LiveRuns::enter(const QString &runDir) {
  const QMutexLocker lock(&liveMutex());
  ++liveSet()[clean(runDir)];
}

void LiveRuns::leave(const QString &runDir) {
  const QMutexLocker lock(&liveMutex());
  const auto it = liveSet().find(clean(runDir));
  if (it != liveSet().end() && --it.value() <= 0)
    liveSet().erase(it);
}

bool LiveRuns::contains(const QString &runDir) {
  const QMutexLocker lock(&liveMutex());
  return liveSet().contains(clean(runDir));
}

QStringList LiveRuns::all() {
  const QMutexLocker lock(&liveMutex());
  return liveSet().keys();
}

// ------------------------------------------------------------------- disk

qint64 folderBytes(const QString &dir) {
  qint64 total = 0;
  QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}

QList<DiskItem> studyOnDisk(const Study &s) {
  QList<DiskItem> out;
  if (s.dir.isEmpty() || !QDir(s.dir).exists())
    return out;
  const QStringList tools = recipeTools();
  const auto add = [&out](const QString &path, DiskKind kind,
                          const QString &caseName, const QString &station,
                          const QString &tool, const QString &recipe,
                          bool leftover, bool live) {
    DiskItem d;
    d.path = clean(path);
    d.kind = kind;
    d.caseName = caseName;
    d.station = station;
    d.tool = tool;
    d.recipeName = recipe;
    d.live = live;
    d.leftover = leftover && !live;
    d.bytes = folderBytes(path);
    out << d;
  };
  // A folder is part of the run layout only when a tool's folder sits two
  // levels under it; the catalog, downloads and exports of a study are not.
  const auto looksLikeRuns = [&tools](const QString &caseDir) {
    for (const QString &st : subFolders(caseDir))
      for (const QString &t : subFolders(caseDir + QLatin1Char('/') + st))
        if (tools.contains(t))
          return true;
    return false;
  };
  for (const QString &caseFolder : subFolders(s.dir)) {
    const QString caseDir = QDir(s.dir).filePath(caseFolder);
    if (!looksLikeRuns(caseDir))
      continue;
    const Case *c = s.findCase(caseFolder);
    const auto anyLiveUnder = [](const QString &dir) {
      const QString prefix = clean(dir) + QLatin1Char('/');
      for (const QString &l : LiveRuns::all())
        if (l.startsWith(prefix))
          return true;
      return false;
    };
    if (!c) { // a case that was removed from the study
      add(caseDir, DiskKind::Orphan, caseFolder, {}, {}, {}, true,
          anyLiveUnder(caseDir));
      continue;
    }
    for (const QString &station : subFolders(caseDir)) {
      const QString stationDir = caseDir + QLatin1Char('/') + station;
      const bool known = station == kCaseFolder || stationOf(*c, station);
      bool hasTool = false;
      for (const QString &t : subFolders(stationDir))
        hasTool = hasTool || tools.contains(t);
      if (!hasTool)
        continue;
      const QString shown = station == kCaseFolder ? QString() : station;
      if (!known) { // a station no longer ticked for the case
        add(stationDir, DiskKind::Orphan, c->name, shown, {}, {}, true,
            anyLiveUnder(stationDir));
        continue;
      }
      for (const QString &tool : subFolders(stationDir)) {
        if (!tools.contains(tool))
          continue;
        const QString toolDir = stationDir + QLatin1Char('/') + tool;
        for (const QString &entry : subFolders(toolDir)) {
          const QString path = toolDir + QLatin1Char('/') + entry;
          if (entry.startsWith(kUnpackedPrefix)) {
            // In use while its lock is held (fx/preprocess.h), by a run of
            // this instance or of another one.
            add(path, DiskKind::Unpacked, c->name, shown, tool, {}, true,
                fx::stagingInUse(path));
            continue;
          }
          QString suffix;
          const QString id = baseOf(entry, &suffix);
          const Recipe *r = s.findRecipe(id);
          const bool mine = r && r->tool == tool;
          const QString runFolder = toolDir + QLatin1Char('/') + id;
          const bool live = LiveRuns::contains(runFolder);
          const QString name = mine ? r->name : id;
          if (!mine) { // a recipe that was removed from the study
            add(path, DiskKind::Orphan, c->name, shown, tool, name, true, live);
          } else if (suffix == kTmp) {
            // A working folder that holds a FINISHED run is no leftover: the
            // run could not take its folder's place (a file of the old one
            // was open elsewhere) and waits here, complete, for the next run
            // of the cell to put it there (runs.h). Removing it would throw
            // the result away.
            const std::optional<RunRecord> waiting = RunRecord::read(path);
            add(path, DiskKind::Working, c->name, shown, tool, name,
                !(waiting && waiting->ok), live);
          } else if (suffix == kFailed) {
            add(path, DiskKind::Failed, c->name, shown, tool, name, true, live);
          } else if (suffix == kCalibration) {
            // Kept on purpose for the next run of the cell: not a leftover.
            add(path, DiskKind::Calibration, c->name, shown, tool, name, false,
                live);
          } else {
            add(path, DiskKind::Result, c->name, shown, tool, name, false, live);
            const qsizetype result = out.size() - 1;
            // Archives a crashed run unpacked beside its results.
            for (const QString &inner : subFolders(path))
              if (inner.startsWith(kUnpackedPrefix)) {
                add(path + QLatin1Char('/') + inner, DiskKind::Unpacked,
                    c->name, shown, tool, name, true,
                    live ||
                        fx::stagingInUse(path + QLatin1Char('/') + inner));
                out[result].bytes -= out.last().bytes;
              }
          }
        }
      }
    }
  }
  return out;
}

qint64 cleanUp(const Study &s, const QStringList &paths, QStringList *failed) {
  // Looked at again now: what was a leftover when it was listed may belong
  // to a run that has started since.
  QHash<QString, DiskItem> now;
  for (const DiskItem &d : studyOnDisk(s))
    now.insert(d.path, d);
  const QString root = clean(s.dir) + QLatin1Char('/');
  qint64 freed = 0;
  for (const QString &p : paths) {
    const QString path = clean(p);
    const auto it = now.constFind(path);
    const bool allowed = it != now.constEnd() && it->leftover && !it->live &&
                         it->kind != DiskKind::Result &&
                         it->kind != DiskKind::Calibration &&
                         path.startsWith(root);
    if (!allowed) {
      if (failed)
        *failed << path;
      continue;
    }
    if (QDir(path).removeRecursively())
      freed += it->bytes;
    else if (failed)
      *failed << path;
  }
  return freed;
}

// ------------------------------------------------------------------ space

namespace {

size_t readAt(void *opaque, mz_uint64 offset, void *buffer, size_t n) {
  auto *f = static_cast<QFile *>(opaque);
  if (!f->seek(qint64(offset)))
    return 0;
  const qint64 got = f->read(static_cast<char *>(buffer), qint64(n));
  return got < 0 ? 0 : size_t(got);
}

struct Remembered {
  qint64 size = 0, time = 0, unpacked = 0;
};

} // namespace

qint64 unpackedBytes(const QString &file) {
  if (!file.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
    return 0;
  const QFileInfo fi(file);
  if (!fi.isFile())
    return 0;
  static QMutex mutex;
  static QHash<QString, Remembered> seen;
  const qint64 time = fi.lastModified().toMSecsSinceEpoch();
  {
    const QMutexLocker lock(&mutex);
    const auto it = seen.constFind(file);
    if (it != seen.constEnd() && it->size == fi.size() && it->time == time)
      return it->unpacked;
  }
  // Only the archive's directory is read (through Qt, so any path works).
  QFile f(file);
  qint64 total = 0;
  if (f.open(QIODevice::ReadOnly)) {
    mz_zip_archive zip{};
    zip.m_pRead = readAt;
    zip.m_pIO_opaque = &f;
    if (mz_zip_reader_init(&zip, mz_uint64(f.size()), 0)) {
      const mz_uint n = mz_zip_reader_get_num_files(&zip);
      for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (mz_zip_reader_file_stat(&zip, i, &st) && !st.m_is_directory)
          total += qint64(st.m_uncomp_size);
      }
      mz_zip_reader_end(&zip);
    } else {
      total = fi.size(); // unreadable: at least what it takes packed
    }
  }
  const QMutexLocker lock(&mutex);
  seen.insert(file, Remembered{fi.size(), time, total});
  return total;
}

SpaceNeed tempSpaceFor(const Study &s, const Case &c, const QString &station,
                       const Recipe &r) {
  SpaceNeed need;
  if (r.tool != kToolFeatures)
    return need;
  const StationRef *st = stationOf(c, station);
  if (!st)
    return need;
  const WaveChoice chosen = chooseWaveFiles(s, *WaveIndex::shared(s.dir), *st,
                                            c.spanStart(), c.time);
  for (const QString &f : chosen.files) {
    const qint64 b = unpackedBytes(f);
    if (b > 0) {
      need.bytes += b;
      ++need.archives;
    }
  }
  return need;
}

qint64 freeBytesAt(const QString &dir) {
  // The folder may not exist yet: the nearest one that does.
  QString d = dir;
  while (!d.isEmpty() && !QDir(d).exists()) {
    const QString up = QFileInfo(d).path();
    if (up == d)
      break;
    d = up;
  }
  const QStorageInfo info(d);
  return info.isValid() && info.isReady() ? info.bytesAvailable() : -1;
}

qint64 peakNeed(QList<qint64> needs, int atOnce) {
  std::sort(needs.begin(), needs.end(), std::greater<qint64>());
  qint64 total = 0;
  for (int i = 0; i < int(needs.size()) && i < std::max(1, atOnce); ++i)
    total += needs[i];
  return total;
}

} // namespace skg::study
