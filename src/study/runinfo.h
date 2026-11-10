// What a study's run folders say, read only: what ran and how long it took,
// what the study takes on disk and what of it is left over, and how much
// temporary space a run would need. Nothing here starts, changes or judges a
// run (that is runs.h); the one thing it removes is what cleanUp() is asked
// to, and that never includes a finished result.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include "study/runs.h"
#include "study/study.h"

namespace skg::study {

// ---- what ran ----

// One attempt a run folder still knows of: the finished run in place, or the
// failed one kept beside it. A stopped run leaves nothing and is not listed.
struct Activity {
  QString caseId, caseName, station, recipeId, recipeName, tool;
  QDateTime started, finished; // UTC
  bool ok = false;
  QString error;        // a failed run: its reason
  QStringList warnings; // a finished run: what it was made under
  QString dir;          // where its files are
  qint64 seconds() const {
    return started.isValid() && finished.isValid()
               ? started.secsTo(finished)
               : -1;
  }
};
// Every attempt of the study's cases and recipes, latest first.
QList<Activity> activity(const Study &s);

// ---- runs that are going on ----

// The run folders being worked on by this application instance, so that
// nothing of theirs is taken for a leftover. The Runs page enters a folder
// when it queues or starts its cell and leaves it when the worker is out.
// Thread-safe.
class LiveRuns {
public:
  static void enter(const QString &runDir);
  static void leave(const QString &runDir);
  static bool contains(const QString &runDir);
  static QStringList all();
};

// ---- the study on disk ----

enum class DiskKind {
  Result,      // a finished run in place: never a leftover
  Working,     // <run>.tmp: a run's working folder
  Failed,      // <run>.failed: what a failed run left, with its log
  Calibration, // <run>.calibration: calibrations kept for later runs
  Unpacked,    // .unzipped-*: archives a run unpacked
  Orphan,      // results of a case, station or recipe no longer in the study
};
struct DiskItem {
  QString path;
  DiskKind kind = DiskKind::Result;
  QString caseName, station, tool, recipeName; // as far as known
  qint64 bytes = 0;
  // Can be removed without losing a finished result of the study as it is:
  // a working folder no run of this instance owns, a failed run's files,
  // unpacked archives, anything of a removed case or recipe.
  bool leftover = false;
  bool live = false; // belongs to a run that is queued or going on
};
// Everything under the study folder that is laid out like run folders
// (<case>/<station>/<tool>/…), with sizes. Other folders of the study (the
// catalog, downloads, exports) are not touched or listed.
QList<DiskItem> studyOnDisk(const Study &s);
qint64 folderBytes(const QString &dir);
// Removes those of `paths` that are, looked at again now, leftovers and not
// live. Returns the bytes freed; what could not be removed goes to `failed`.
qint64 cleanUp(const Study &s, const QStringList &paths,
               QStringList *failed = nullptr);

// ---- temporary space ----

struct SpaceNeed {
  qint64 bytes = 0; // what the run unpacks into its working folder
  int archives = 0; // how many archives that is
};
// A features run unpacks the zip archives it reads; other tools need next to
// nothing. Read from the archives' directories (remembered per file).
SpaceNeed tempSpaceFor(const Study &s, const Case &c, const QString &station,
                       const Recipe &r);
// The sum of the uncompressed sizes of a zip's members; 0 for another file.
qint64 unpackedBytes(const QString &file);
// Free bytes on the disk that holds `dir` (-1 when unknown).
qint64 freeBytesAt(const QString &dir);
// With `atOnce` runs at a time, the most temporary space the given needs can
// take together: the `atOnce` largest.
qint64 peakNeed(QList<qint64> needs, int atOnce);

} // namespace skg::study
