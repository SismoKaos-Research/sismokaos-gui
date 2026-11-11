// Exact scan results of waveform files, shared by every study of this user:
// a file whose coverage was read once is not read again for a second study,
// for the same folder added again, or after the data moved to another drive.
//
// The study's own index (waveindex.h) stays the source of truth; this is only
// a shortcut past reading the file. It lives in the user's cache folder and
// may be deleted at any time.
//
// One small JSON file per waveform file, written beside its place and renamed
// into it, so several running applications can read and write the same cache
// without a lock: the worst that happens is that two of them write the same
// result. Each result is filed twice:
//   - by the file's real path, size and modification time;
//   - by its name, size, modification time (to within two seconds, since
//     some file systems keep no finer time) and the first record of the file
//     as the quick pass read it (of every member, for a zip), for a file
//     that moved.
//
// A result is only ever handed out for a file whose content matches the one
// it was made from: every result carries a fingerprint of that file (below),
// which is compared with the file asked about; on a mismatch the file is
// simply scanned. So a file rewritten under the same name, size and time, or
// two files that look alike from outside, never get each other's coverage.
// Results by name are kept one file per fingerprint, so look-alikes do not
// overwrite each other either.
#pragma once

#include <QString>

#include <atomic>

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

  // What tells this file's content from another's without reading it all:
  // a hash of its size and of the whole file when it is at most 1 MiB,
  // otherwise of its first and last 64 KiB and six 4 KiB blocks in between.
  // The end of a zip archive is its directory, which holds the checksum of
  // every member, so for archives this covers all of the content; for a
  // large plain file it is a sample. Empty when the file cannot be read.
  static QString fingerprint(const QString &absolutePath);

  // One line that names a file by what it is, not by where or when:
  // "<file name>|<size>|<start of its fingerprint>". The same file on
  // another drive gives the same line whatever that drive makes of its
  // modification time (FAT rounds to two seconds, which a line holding the
  // time would not survive); a file replaced under the same name and size
  // gives another. For stamps that say "these input files" (a run's inputs,
  // a kept calibration). The fingerprint is read once per file and kept in
  // memory by path, size and time, so asking again costs a stat. Empty when
  // the file cannot be read. Any thread.
  // With `onlyIfKnown`, nothing is read: the line when this session already
  // has the file's fingerprint, else empty (for callers on the GUI thread).
  static QString identityLine(const QString &absolutePath,
                              bool onlyIfKnown = false);

  // The exact result for the file at this path, if it was scanned before
  // with the same size, time and content.
  bool lookup(const QString &absolutePath, qint64 size, double mtime,
              WaveFile *out) const;
  // The same for a file that was somewhere else then. `identity` is its
  // entry from the quick pass (first record of the file, or of each member).
  bool lookupMoved(const QString &absolutePath, qint64 size, double mtime,
                   const WaveFile &identity, WaveFile *out) const;
  // Files an exact result. `identity` as above; without channels in it the
  // result is filed by path only. False when it could not be written (the
  // scan goes on without it; see writeFailures).
  bool store(const QString &absolutePath, qint64 size, double mtime,
             const WaveFile &exact, const WaveFile &identity) const;
  // Results this object could not file, for the scan's log.
  int writeFailures() const { return failures_.load(); }

  struct Usage {
    int files = 0;    // waveform files with a result
    qint64 bytes = 0; // on disk
  };
  Usage usage() const;
  // Removes every result. False when something could not be removed.
  bool clear() const;

private:
  QString dir_;
  mutable std::atomic<int> failures_{0};
};

} // namespace skg::study
