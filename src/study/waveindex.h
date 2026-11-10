// What a study's waveform folders hold, read from the miniSEED headers
// themselves: which station and channels each file is from, and which times
// it covers. Folder and file names are never trusted for that; they are only
// compared with the headers so a disagreement can be shown.
//
// A folder is scanned in two passes. The first reads one record per file (per
// member of a zip) and is fast: it says which stations are there. The second
// reads every record header and gives the exact coverage. Both keep what
// they found in <study>/waveforms.index.json, keyed by the folder's id and
// the file's path relative to that folder, with the file's size and
// modification time; a file that has not changed is not read again, and a
// folder that moved (another drive) only needs its path changed in the study.
//
// Accepted: miniSEED 2, plain files or zip archives of them, in any layout.
// Folders whose name starts with "_" or "." are not entered.
#pragma once

#include <QDateTime>
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
};

struct WaveFile {
  qint64 size = 0;
  double mtime = 0; // seconds since the epoch
  int pass = 0;     // 0 not read, 1 identity, 2 coverage
  QString skipped;  // why the file is not used; empty when it is
  QMap<QString, WaveChannel> channels; // "NET.STA.LOC.CHA"
  // The station codes in the file.
  QStringList stationCodes() const;
};

// One station as the folders show it, over every file it appears in.
struct WaveStation {
  QString network, code;
  QStringList channels; // "HHZ", or "00.HHZ" with a location code
  QList<double> rates;
  double first = 0, last = 0; // epoch seconds; last is exact after pass 2
  int days = 0;               // days with any data (after pass 2)
  int files = 0;
  bool coverageKnown = false; // every file of it has been through pass 2
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
  QList<WaveStation> stations() const;
  QList<SkippedFile> skipped() const;
  bool hasStation(const QString &code) const;

  // Days in [from, to) on which the station has data, and how many days the
  // period has. `approximate` is set when some of its files have only been
  // through the first pass (those count for the day they start on).
  int daysCovered(const QString &code, const QDateTime &from,
                  const QDateTime &to, bool *approximate = nullptr) const;

  // The files (root id, relative path) holding data of the station in
  // [from, to). A file not yet through the coverage pass is taken when its
  // first record starts in the period or up to `slackDays` before it.
  struct Ref {
    QString rootId, rel;
  };
  QList<Ref> filesFor(const QString &code, const QDateTime &from,
                      const QDateTime &to, int slackDays = 35) const;
};

struct ScanHooks {
  std::function<bool()> shouldStop;
  std::function<void(int done, int total, const QString &name)> progress;
  // Called now and then so the caller can save what is there so far.
  std::function<void()> checkpoint;
};

struct ScanStats {
  int listed = 0;  // files found under the folder
  int read = 0;    // files read in this call
  int skipped = 0; // files that are not miniSEED (or unreadable)
  int dropped = 0; // index entries whose file is gone
  bool stopped = false;
};

enum class ScanPass { Identity = 1, Coverage = 2 };

// Brings the index of one folder up to date for the pass asked for. Files
// whose size and time match their entry, and that already went through this
// pass, are left alone. Throws std::runtime_error when the folder is missing.
ScanStats scanRoot(WaveIndex &index, const QString &rootId,
                   const QString &rootPath, ScanPass pass,
                   const ScanHooks &hooks = {});

// The files Features should read for a station of a case: absolute paths of
// the indexed files that overlap [from, to); when the index does not know the
// station, the files of its folder (Study::dataDirFor) whose names carry a
// date in the period, or all of them when the names carry none. `how` says
// which of these it was, for the run log.
QStringList waveFilesFor(const Study &study, const WaveIndex &index,
                         const StationRef &station, const QDateTime &from,
                         const QDateTime &to, QString *how = nullptr);

// Whether a Features run would find anything to read for the station: its
// folder exists, or the index knows the station.
bool hasWaveforms(const Study &study, const StationRef &station);

// The start date a file name carries, as the two downloaders write it
// (<STA>_<yyyy-MM-dd>.zip, <NET>_<STA>_<ddMMyyyy>_...mseed); invalid if none.
QDate dateInFileName(const QString &fileName);

// Station coordinates from a StationXML file (FDSN): code, network, latitude,
// longitude of every <Station>. Throws std::runtime_error.
QList<ExtraStation> readStationXml(const QString &path);

} // namespace skg::study
