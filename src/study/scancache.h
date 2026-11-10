// Exact scan results of waveform files, shared by every study of this user:
// a file whose coverage was read once is not read again for a second study,
// for the same folder added again, or after the data moved to another drive.
//
// The study's own index (waveindex.h) stays the source of truth; this is only
// a shortcut past reading the file. It lives in the user's cache folder and
// may be deleted at any time.
//
// One small JSON file per waveform file, written atomically (a temporary
// file renamed into place), so several running applications can read and
// write the same cache without a lock: the worst that happens is that two of
// them write the same result. Each result is filed twice:
//   - by the file's real path, size and modification time;
//   - by its name, size, modification time (to within two seconds, since
//     some file systems keep no finer time) and the first record of the file
//     as the quick pass read it (of every member, for a zip), for a file
//     that moved. Two files that only look alike from outside therefore
//     never share a result.
#pragma once

#include <QString>

#include "study/waveindex.h"

namespace skg::study {

class ScanCache {
public:
  // The cache the application uses; empty (no cache) until the application
  // sets it at start. Tests leave it empty or point it at a temporary folder.
  static QString sharedDir();
  static void setSharedDir(const QString &dir);
  // Where the application keeps it by default: the user's cache folder.
  static QString defaultDir();

  explicit ScanCache(const QString &dir);
  QString dir() const { return dir_; }

  // The exact result for the file at this path, if it was scanned before
  // with the same size and time.
  bool lookup(const QString &absolutePath, qint64 size, double mtime,
              WaveFile *out) const;
  // The same for a file that is somewhere else now. `identity` is its entry
  // from the quick pass (first record of the file, or of each zip member).
  bool lookupMoved(const QString &fileName, qint64 size, double mtime,
                   const WaveFile &identity, WaveFile *out) const;
  // Files an exact result. `identity` as above; without channels in it the
  // result is filed by path only.
  void store(const QString &absolutePath, qint64 size, double mtime,
             const WaveFile &exact, const WaveFile &identity) const;

  struct Usage {
    int files = 0;    // waveform files with a result
    qint64 bytes = 0; // on disk
  };
  Usage usage() const;
  // Removes every result. False when something could not be removed.
  bool clear() const;

private:
  QString dir_;
};

} // namespace skg::study
