// Running a recipe on a case, and knowing afterwards what has been run.
//
// Every (case, station, recipe) has one folder under the study,
//
//   <study>/<case id>/<station code, or "_case">/<tool>/<recipe id>/
//
// holding the tool's own files, unchanged, plus run.json: what was run
// (recipe revision, the settings in effect, the case as it was), on what
// (inputs with their SHA-256), by which version, when, how it ended, and
// the values the case fitted for itself. A cell of the Runs page is "done"
// when its run.json says so for the recipe, the case and the catalog as they
// are now.
//
// A run never works in that folder. It writes into <folder>.tmp, and only a
// run that finishes takes the folder's place; one that is stopped leaves
// nothing, and one that fails is kept as <folder>.failed (its files, run.log
// and run.json with the error) until the next attempt. So a finished run
// stays as it was until a newer one has succeeded. Every run also writes its
// log to run.log.
//
// The tools and their recipe settings:
//
//   indicators (one run per case; station folder "_case")
//     The catalog events of the case's zone from one window before the span
//     up to, not including, the event (the earthquake is looked up in the
//     catalog, so it stays out even when the catalog's time offset is wrong);
//     cbi's indicator table and figures.
//     estimate_mc (fitted per case when true), mc, mag_bin, window_months,
//     step_months, half_gauss_sigma, ma_windows, bollinger, bb_window, bb_k,
//     homogenize, decluster, figures, figure_dpi, figure_language.
//
//   features (one run per station)
//     fx on the station's waveform folder; the chaos parameters are
//     calibrated per station (fitted) on the study's catalog, in UTC.
//     config_json: an fx config.json as text (empty: fx's defaults); a
//     catalog named in it is an explicit override of the study's.
//
//   classification (one run per station)
//     sfc on the feature table of a features recipe. The classes are ranges
//     of days before the event, the same for every case.
//     features_recipe, band, classes (e.g. ["1-30", "31-90"]),
//     days_per_class, models, feature_columns (empty: every numeric column),
//     train_size, val_size, scaler, random_seed, cv_folds, quick (small
//     grids), sfc_toml (anything else, in sfc's own settings layout).
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <optional>

#include "analysis/events.h"
#include "study/study.h"

namespace skg::study {

extern const QString kToolIndicators;     // "indicators"
extern const QString kToolFeatures;       // "features"
extern const QString kToolClassification; // "classification"
QStringList recipeTools();

// Whether the tool runs once per station (features, classification) or
// once per case (indicators).
bool perStation(const QString &tool);
// A new recipe of the tool with its default settings and fitted list.
Recipe defaultRecipe(const QString &tool);

// <study>/<case>/<station or _case>/<tool>/<recipe>
QString runDir(const Study &s, const Case &c, const QString &station,
               const Recipe &r);
// Beside a run folder: where a run works, and where a failed one is kept.
QString runTmpDir(const QString &runDir);    // <runDir>.tmp
QString runFailedDir(const QString &runDir); // <runDir>.failed
extern const QString kRunLogName;            // "run.log"

struct RunRecord {
  QString tool, recipe, caseId, station;
  int revision = 0;
  QString caseSignature; // what of the case and study the run depended on
  QString catalogSha;    // SHA-256 of the study's catalog as the run read it
  QString upstream;      // classification: when its features run finished
  QString waveStamp;     // features: the waveform files read (names, sizes,
                         // times), so new files for the span are noticed
  QStringList waveFiles; // the same, one "name|size|time" line per file
  // Some of them were taken on an estimate from their names, with margin,
  // because the exact scan had not read them yet: the run may have read
  // more than the span needed.
  bool waveEstimated = false;
  QString appVersion;
  QDateTime started, finished; // UTC
  bool ok = false;
  QString error;
  // A failure the page can offer a remedy for: "no_data", "little_data",
  // "gappy_data" (the station has too little waveform data in the span) or
  // "events" (the catalog's earthquakes leave no quiet windows); else empty.
  QString errorReason;
  // What the run was made under that the reader of its results should know
  // (a sparse catalog, a station with little data in the span). Shown beside
  // the cell and beside its scores on Compare.
  QStringList warnings;
  // Features: which calibration the run used (its key, one per band joined
  // by commas), and whether it was one kept from an earlier run of this
  // cell (see "Calibrations are kept" below) or computed by this run.
  QString calibrationKey;
  bool calibrationReused = false;
  QVariantMap settings; // the recipe's settings as run
  QVariantMap inputs;   // path -> sha256
  QVariantMap fitted;   // what this case fitted for itself
  QVariantMap outputs;  // name -> path relative to the run folder

  static const QString kFileName; // "run.json"
  bool write(const QString &dir) const;
  static std::optional<RunRecord> read(const QString &dir);
};

// Everything of the case and of the study's inputs a run of `tool` depends
// on, as one line; a run whose line differs from today's is stale. Each tool
// has its own: the indicators depend on the case's time, place, span and
// zone; the features on the time, the span and the station (not the zone);
// the classification on the time and the station. The catalog's time offset
// is part of the first two.
QString runSignature(const Study &s, const Case &c, const QString &station,
                     const QString &tool);

// Whether the waveform files a features run would read now (`now`, from
// chooseWaveFiles) put the finished run `record` out of date. Files taken on
// an estimate, which the exact scan has not read yet, prove nothing either
// way: a run is not called stale for a file that only may overlap its span,
// nor for having read, on such an estimate, more than the exact scan later
// says the span needed. A file known to hold the span that the run did not
// read, or a file it read that is gone or replaced, does make it stale.
struct WaveChoice;
bool waveFilesChanged(const RunRecord &record, const WaveChoice &now);
// The form RunRecord::waveFiles keeps the files in: "name|size|time", sorted.
QStringList waveFileLines(const QStringList &files);

enum class CellState {
  Done,          // run.json: ok, for this recipe revision and this case
  Missing,       // never run
  Stale,         // run, but the recipe or the case has changed since
  Failed,        // the last run ended with an error
  Blocked,       // cannot run yet (`why` says what is needed)
  NotApplicable, // the tool does not apply to this row
};
// Where a blocked cell is put right.
enum class Fix {
  None,
  Cases,    // on the Cases page (catalog, stations, waveform folder)
  Recipe,   // by editing this recipe
  RunFirst, // by running another cell first
};
struct Cell {
  CellState state = CellState::Missing;
  QString why; // Blocked, Failed, Stale: the reason, with the path checked
  Fix fix = Fix::None;
  QString dir; // the run folder
  std::optional<RunRecord> record; // the finished run in `dir`, if any
  QString note; // Done: a later attempt that failed, if any
  QString log;  // the run log to show (of the last attempt), if any
  // Not a reason to stop, but to know about: of a finished run, what it was
  // made under (its record's warnings); of a cell still to run, what a run
  // made now would be under. One sentence per line.
  QStringList warnings;
};
// What a run of the cell made now would be under (see Cell::warnings): a
// catalog too sparse for the tool, a station with little data in the span.
QStringList runWarnings(const Study &s, const Case &c, const QString &station,
                        const Recipe &r);
// `station` empty: the case has no stations (a per-station tool then waits
// for one).
Cell cellOf(const Study &s, const Case &c, const QString &station,
            const Recipe &r);
// What the waveform index knows of a station for a period [from, to): the
// days with data in it, and the dates the station's data runs over at all.
// `known` is false when the index has not seen the station (its data may
// still sit in a folder of its own, Study::dataDirFor).
class WaveIndex;
struct SpanData {
  bool known = false;
  int days = 0;      // days of the period with data
  int spanDays = 0;  // days in the period
  bool approximate = false, atLeast = false; // as WaveIndex::daysCovered
  QDate first, last; // the station's data anywhere (UTC dates)
  // Too little of the period to rely on (defaults::kMinSpanCoverage), though
  // not nothing.
  bool thin() const;
};
SpanData spanData(const WaveIndex &index, const QString &station,
                  const QDateTime &from, const QDateTime &to);
// How many UTC calendar days the period [from, to) touches: what the days
// with data are counted out of, so "N of M days" can never have N above M.
// A year from midnight to midnight is 365 (366 in a leap year); the same
// year from 14:00 to 14:00 touches one day more; a period of less than a
// day is 1, or 2 across a midnight; an empty or reversed one is 0.
int spanCalendarDays(const QDateTime &from, const QDateTime &to);

// How many finished runs of `before` are no longer finished and current in
// `after` (the same study after an edit): what a change would make stale.
int runsMadeStale(const Study &before, const Study &after);
// The catalog event a case was made from: same place and magnitude, within
// 15 hours of the case's time (a wrong time offset moves it by hours, not
// more). nullptr for a reference date or when the catalog has none.
const analysis::Event *eventOfCase(const Case &c,
                                   const std::vector<analysis::Event> &events);
// The catalog a features recipe names for itself in its config, or empty.
QString calibrationOverride(const Recipe &features);

struct RunHooks {
  std::function<void(const QString &)> log;
  std::function<bool()> shouldStop;
  std::function<void(double fraction, const QString &what)> progress;
  // Features: compute the calibration anew although a valid one is kept.
  bool freshCalibration = false;
};

// Calibrations are kept. The calibration of a Features run is a pass over
// all its data; a run that is stopped, or fails in the extraction, should
// not have to make it again. As soon as it is computed it is copied to
//
//   <run folder>.calibration/<key>.json
//
// beside the run folder (not in it: the folder is replaced by each finished
// run, and <folder>.tmp is cleared by each new one). The key is a hash of
// everything the calibration depends on, which the file also lists
// ("kept_for"): the settings that enter it (sample rate, filter band, window
// length, sample entropy radius, the calibration settings), the catalog (its
// SHA-256 and time offset), the station's position, the span, and the
// waveform files read (names, sizes, times, and whether some were chosen on
// an estimate). A later run of the same cell reuses the file only when its
// own key is the same; otherwise it calibrates again and its log says what
// changed. The latest two are kept. Never shared between cells: another
// case, station or recipe has another folder.
QString runCalibrationDir(const QString &runDir); // <runDir>.calibration
// Runs the cell in <folder>.tmp and puts it in the folder's place when it
// has finished (see the top of this file). Throws std::runtime_error with the
// reason on failure; returns false when stopped.
bool runCell(const Study &s, const Case &c, const QString &station,
             const Recipe &r, const QString &appVersion, const RunHooks &hooks);

QString sha256Of(const QString &path); // empty when unreadable
// The same, remembered per path until the file's size or time changes, so
// asking on every refresh costs nothing. Thread-safe.
QString sha256Cached(const QString &path);

} // namespace skg::study
