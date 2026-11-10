// What a study's waveform folders hold, read from the miniSEED headers
// themselves: which station and channels each file is from, and which times
// it covers. Folder and file names are never trusted for that; they are only
// compared with the headers so a disagreement can be shown.
//
// A folder is scanned in two passes. The first is quick: it reads the zip
// directory and one record per file (per member of a zip), which says which
// stations are there, and takes an ESTIMATE of the period from the name where
// the name carries one (..._ddMMyyyy_HHmmss_ddMMyyyy_HHmmss_..., as both
// downloaders write) and the first record agrees with it. The second reads
// every record header and gives the exact coverage; it runs on several files
// at once and replaces the estimates as it goes. Everything that uses the
// coverage works on the estimates meanwhile and can tell which is which.
//
// Both passes keep what they found in <study>/waveforms.index.json, keyed by
// the folder's id and the file's path relative to that folder, with the
// file's size and modification time; a file that has not changed is not read
// again, and a folder that moved (another drive) only needs its path changed
// in the study. Exact results are also kept in a cache shared by all studies
// of this user (scancache.h), so the same files are never read twice.
//
// Accepted: miniSEED 2, plain files or zip archives of them, in any layout.
// Folders whose name starts with "_" or "." are not entered.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "study/study.h"
#include "tdvms/coverage.h"

namespace skg::study {

struct WaveChannel {
  double rate = 0;
  double firstStart = 0; // start of the first record seen, epoch seconds
  tdvms::Spans spans;    // filled by the coverage pass
  // What the names say the file covers, until the coverage pass has read it
  // (then empty). Gaps are not known; the true extent may differ.
  tdvms::Spans estimate;
};

struct WaveFile {
  qint64 size = 0;
  double mtime = 0; // seconds since the epoch
  int pass = 0;     // 0 not read, 1 identity, 2 coverage
  QString skipped;  // why the file is not used; empty when it is
  QMap<QString, WaveChannel> channels; // "NET.STA.LOC.CHA"
  // The station codes in the file.
  QStringList stationCodes() const;
  bool exact() const { return pass >= 2; }
  // Read by the quick pass only, with a period taken from the names.
  bool estimated() const;
};

// One station as the folders show it, over every file it appears in.
struct WaveStation {
  QString network, code;
  QStringList channels; // "HHZ", or "00.HHZ" with a location code
  QList<double> rates;
  double first = 0, last = 0; // epoch seconds; last is exact after pass 2
  int days = 0;               // days with any data (exact after pass 2)
  int files = 0;
  bool coverageKnown = false; // every file of it has been through pass 2
  // Not all exact yet, but every file still waiting has an estimate: `last`
  // and `days` are then "about"; without it they are a lower bound.
  bool extentEstimated = false;
  bool mixedRates = false;    // one channel at more than one sample rate
  bool sharedFiles = false;   // some file also holds another station
  QStringList otherFolders;   // folders named differently from the code
};

struct SkippedFile {
  QString rootId, rel, why;
};

class WaveIndex {
public:
  static const QString kFileName; // "waveforms.index.json"

  // The index of a study folder; empty when there is none or it is broken.
  static WaveIndex load(const QString &studyDir);
  bool save(const QString &studyDir, QString *error = nullptr) const;
  // The same, kept in memory and re-read only when the file changes. Safe to
  // call from any thread; what it returns never changes.
  static std::shared_ptr<const WaveIndex> shared(const QString &studyDir);

  QMap<QString, QMap<QString, WaveFile>> roots; // root id -> rel path -> file

  bool isEmpty() const;
  int fileCount() const;
  // Files still waiting for the coverage pass.
  int pendingCoverage() const;
  // Of those, the ones whose period is estimated from their names.
  int estimatedFiles() const;
  QList<WaveStation> stations() const;
  QList<SkippedFile> skipped() const;
  bool hasStation(const QString &code) const;

  // Days in [from, to) on which the station has data. `approximate` is set
  // when some of its files have only been through the quick pass: those
  // count for the days their estimate covers. `atLeast` is set when some of
  // them have no estimate either (they count for the day they start on), so
  // the number is a lower bound.
  int daysCovered(const QString &code, const QDateTime &from,
                  const QDateTime &to, bool *approximate = nullptr,
                  bool *atLeast = nullptr) const;

  // The files (root id, relative path) holding data of the station in
  // [from, to). A file not yet through the coverage pass is taken when its
  // estimate, widened by a day at both ends, overlaps the period; one with
  // no estimate when its first record starts in the period or up to
  // `slackDays` before it. Such a choice is marked `exact == false`: it errs
  // on the side of taking a file that is not needed, never of dropping one.
  struct Ref {
    QString rootId, rel;
    bool exact = true;
  };
  QList<Ref> filesFor(const QString &code, const QDateTime &from,
                      const QDateTime &to, int slackDays = 35) const;
};

// All hooks are called from the thread that called the scan, never from its
// workers.
struct ScanHooks {
  std::function<bool()> shouldStop;
  // After each file: how many of the files to read are done, and its name.
  std::function<void(int done, int total, const QString &name)> progress;
  // A few times a second while files are being read: bytes read of the
  // bytes this call has to read (sizes on disk), and the files done.
  std::function<void(qint64 bytesDone, qint64 bytesTotal, int filesDone,
                     int filesTotal)>
      bytes;
  // Called now and then so the caller can save what is there so far.
  std::function<void()> checkpoint;
};

class ScanCache;

struct ScanOptions {
  // Files read at the same time. Each worker streams one file; none holds a
  // whole file in memory.
  int workers = 1;
  // Station codes whose files are read first (the ones cases wait for).
  QStringList firstStations;
  // Exact results of earlier scans, of any study; may be null.
  ScanCache *cache = nullptr;
  // `checkpoint` is called after this many files or seconds, whichever
  // comes first.
  int checkpointFiles = 50;
  double checkpointSeconds = 5.0;
  // How often the scan looks at its workers and at `shouldStop`, in
  // milliseconds: a stop request reaches a worker inside a file within this
  // and one chunk of the file.
  int pollMs = 200;
};

struct ScanStats {
  int listed = 0;      // files found under the folders
  int read = 0;        // files read in this call
  int cached = 0;      // files whose exact coverage came from the cache
  int skipped = 0;     // files that are not miniSEED (or unreadable)
  int dropped = 0;     // index entries whose file is gone
  qint64 bytes = 0;    // sizes on disk of the files this call had to read
  QStringList missing; // folders that do not exist (scanRoots only)
  bool stopped = false;
};

enum class ScanPass { Identity = 1, Coverage = 2 };

// As many workers as suit this machine: two cores are left alone, and more
// than six do not help once the disk is the limit.
int defaultScanWorkers();
// The application setting that overrides it (an integer; 0 is automatic).
inline constexpr char kScanWorkersSetting[] = "waveforms/scanWorkers";

struct ScanFolder {
  QString id, path;
};

// Brings the index of the folders up to date for the pass asked for. Files
// whose size and time match their entry, and that already went through this
// pass, are left alone. The files are read in one order over all folders:
// those of `firstStations` first, then station by station. A file a stop
// request interrupts keeps its earlier entry. A missing folder is named in
// `missing` and its entries are kept.
ScanStats scanRoots(WaveIndex &index, const QList<ScanFolder> &folders,
                    ScanPass pass, const ScanHooks &hooks = {},
                    const ScanOptions &options = {});
// One folder; throws std::runtime_error when it is missing.
ScanStats scanRoot(WaveIndex &index, const QString &rootId,
                   const QString &rootPath, ScanPass pass,
                   const ScanHooks &hooks = {},
                   const ScanOptions &options = {});

// The files Features should read for a station of a case: absolute paths of
// the indexed files that overlap [from, to); when the index does not know the
// station, the files of its folder (Study::dataDirFor) whose names carry a
// date in the period, or all of them when the names carry none. `how` says
// which of these it was, for the run log. `uncertain` are the ones taken on
// an estimate, because the coverage pass has not read them yet.
struct WaveChoice {
  QStringList files;
  QStringList uncertain;
  QString how;
};
WaveChoice chooseWaveFiles(const Study &study, const WaveIndex &index,
                           const StationRef &station, const QDateTime &from,
                           const QDateTime &to);
QStringList waveFilesFor(const Study &study, const WaveIndex &index,
                         const StationRef &station, const QDateTime &from,
                         const QDateTime &to, QString *how = nullptr);

// A short text that changes when files are added to or removed from the
// folder or the sub-folders a scan enters (their names and modification
// times), without reading any file: tells whether a scan is due.
QString folderStamp(const QString &rootPath);

// Whether a Features run would find anything to read for the station: its
// folder exists, or the index knows the station.
bool hasWaveforms(const Study &study, const StationRef &station);

// The start date a file name carries, as the two downloaders write it
// (<STA>_<yyyy-MM-dd>.zip, <NET>_<STA>_<ddMMyyyy>_...mseed); invalid if none.
QDate dateInFileName(const QString &fileName);
// The period a name claims, "..._ddMMyyyy_HHmmss_ddMMyyyy_HHmmss_..." (UTC),
// as the members of the TDVMS archives and the FDSN day files are named.
// False when the name carries none or the two times are not in order.
bool periodInFileName(const QString &fileName, double *start, double *end);

// One file's entry as the index and the shared cache store it.
QJsonObject waveFileToJson(const WaveFile &file);
WaveFile waveFileFromJson(const QJsonObject &object);

// Station coordinates from a StationXML file (FDSN): code, network, latitude,
// longitude of every <Station>. Throws std::runtime_error.
QList<ExtraStation> readStationXml(const QString &path);

} // namespace skg::study
