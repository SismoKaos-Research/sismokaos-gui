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
// when its run.json says so for the recipe and the case as they are now.
//
// The tools and their recipe settings:
//
//   indicators (one run per case; station folder "_case")
//     The catalog events of the case's zone from one window before the span
//     up to, not including, the event; cbi's indicator table and figures.
//     estimate_mc (fitted per case when true), mc, mag_bin, window_months,
//     step_months, half_gauss_sigma, ma_windows, bollinger, bb_window, bb_k,
//     homogenize, decluster, figures, figure_dpi, figure_language.
//
//   features (one run per station)
//     fx on the station's waveform folder; the chaos parameters are
//     calibrated per station (fitted). config_json: an fx config.json as
//     text (empty: fx's defaults). The whole folder is processed.
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

struct RunRecord {
  QString tool, recipe, caseId, station;
  int revision = 0;
  QString caseSignature; // what of the case the run depended on
  QString appVersion;
  QDateTime started, finished; // UTC
  bool ok = false;
  QString error;
  QVariantMap settings; // the recipe's settings as run
  QVariantMap inputs;   // path -> sha256
  QVariantMap fitted;   // what this case fitted for itself
  QVariantMap outputs;  // name -> path relative to the run folder

  static const QString kFileName; // "run.json"
  bool write(const QString &dir) const;
  static std::optional<RunRecord> read(const QString &dir);
};

// Everything of a case a run depends on, as one line.
QString caseSignature(const Case &c, const QString &station);

enum class CellState {
  Done,          // run.json: ok, for this recipe revision and this case
  Missing,       // never run
  Stale,         // run, but the recipe or the case has changed since
  Failed,        // the last run ended with an error
  Blocked,       // cannot run yet (`why` says what is needed)
  NotApplicable, // the tool does not apply to this row
};
struct Cell {
  CellState state = CellState::Missing;
  QString why; // Blocked, Failed: the reason
  QString dir; // the run folder
  std::optional<RunRecord> record;
};
// `station` empty: the case has no stations.
Cell cellOf(const Study &s, const Case &c, const QString &station,
            const Recipe &r);

struct RunHooks {
  std::function<void(const QString &)> log;
  std::function<bool()> shouldStop;
  std::function<void(double fraction, const QString &what)> progress;
};
// Runs the cell and writes run.json (also when it fails). Throws
// std::runtime_error with the reason on failure; returns false when stopped.
bool runCell(const Study &s, const Case &c, const QString &station,
             const Recipe &r, const QString &appVersion, const RunHooks &hooks);

QString sha256Of(const QString &path); // empty when unreadable

} // namespace skg::study
