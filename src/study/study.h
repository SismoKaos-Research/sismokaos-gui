// A study: the same analyses run on several earthquakes so they can be
// compared. It is a folder with one file, study.toml, that lists
//
//   - where the inputs are (the earthquake catalog, the station list, the
//     folder holding one sub-folder of miniSEED per station);
//   - the cases: an earthquake, or a reference date with no earthquake, and
//     the place it is studied from (stations with distance and azimuth, a
//     catalog zone, and the span: how long before the event is studied);
//   - the recipes: one tool's saved settings under a name, with the list of
//     settings each case fits for itself.
//
// Results are written under the study folder in a fixed layout,
// <case>/<station>/<tool>/<recipe>/, each with a run.json record
// (study/runs.h), so nothing about a run needs to be kept in study.toml.
//
// study.toml (schema 1):
//
//   schema = 1
//   name = "East Anatolian comparison"
//   catalog = "catalog_afad.csv"       # relative to the folder, or absolute
//   catalog_utc_offset_hours = 0.0
//   station_list = ""                  # empty: the built-in AFAD list
//   data_root = ""                     # <data_root>/<STATION>/ miniSEED
//   waveform_roots = []                # every such folder, data_root first;
//                                      # a station is looked for in each
//
// Paths are stored relative to the study folder when they lie inside it, and
// also when they lie beside it under a common folder (so a study and its data
// can move together to another disk); otherwise absolute. Comments and keys
// the application does not know are not kept when it saves the file.
//
//   [[case]]
//   id = "sivrice-2020"                # folder name; never changes
//   name = "Sivrice 2020"
//   kind = "earthquake"                # or "reference"
//   time = "2020-01-24T17:55:11Z"      # the event, or the reference date
//   latitude = 38.36
//   longitude = 39.06
//   depth_km = 8.0                     # earthquakes only
//   magnitude = 6.8                    # earthquakes only
//   place = "Sivrice (Elazig)"
//   span_days = 365
//   zone = "circle"                    # or "fault"
//   radius_km = 100.0
//   fault_geojson = ""                 # zone "fault"; empty: the shipped EAFZ
//   strands = []
//   buffer_km = 30.0
//     [[case.station]]
//     code = "ELZG"
//     latitude = 38.67
//     longitude = 39.19
//     distance_km = 36.4
//     azimuth_deg = 18.0               # from the epicentre to the station
//     data_dir = ""                    # overrides <data_root>/<code>
//
//   [[recipe]]
//   id = "indicators-12mo"
//   name = "12 month window"
//   tool = "indicators"                # indicators, classification, features
//   revision = 3                       # +1 on every edit; older runs go stale
//   fitted = ["mc"]                    # settings each case fits for itself
//     [recipe.settings]
//     window_months = 12
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <limits>
#include <optional>
#include <vector>

#include "analysis/events.h"

namespace skg::study {

struct StationRef {
  QString code;
  double lat = 0, lon = 0;
  double distanceKm = 0;
  double azimuthDeg = 0; // from the epicentre to the station
  QString dataDir;       // empty: <data_root>/<code>
};

// An input the study names that is not on disk (a moved catalog, an unplugged
// data disk).
struct MissingInput {
  enum class Kind { Catalog, StationList, WaveformRoot, StationData, FaultFile };
  Kind kind;
  QString path; // absolute, as resolved
};

struct Zone {
  enum class Kind { Circle, Fault };
  Kind kind = Kind::Circle;
  double radiusKm = 100.0;
  QString faultGeoJson; // empty: the shipped East Anatolian traces
  QStringList strands;
  double bufferKm = 30.0;
};

struct Case {
  QString id; // slug, the folder name
  QString name;
  bool reference = false; // a date with no earthquake
  QDateTime time;         // UTC
  double lat = 0, lon = 0;
  double depthKm = std::numeric_limits<double>::quiet_NaN();
  double magnitude = std::numeric_limits<double>::quiet_NaN();
  QString place;
  int spanDays = 365;
  Zone zone;
  QList<StationRef> stations;

  QDateTime spanStart() const { return time.addDays(-spanDays); }
};

struct Recipe {
  QString id;
  QString name;
  QString tool; // "indicators", "classification", "features"
  int revision = 1;
  QStringList fitted;
  QVariantMap settings; // strings, numbers, booleans and lists of those
};

class Study {
public:
  static const QString kFileName; // "study.toml"

  // Reads <dir>/study.toml; throws std::runtime_error with the reason.
  static Study load(const QString &dir);
  // Writes <dir>/study.toml (through a temporary file); throws.
  void save() const;

  QString dir;  // absolute
  QString name; // shown in the header
  QString catalog;
  double catalogUtcOffsetHours = 0.0;
  QString stationList;
  QString dataRoot;           // the first waveform folder
  QStringList moreDataRoots;  // further waveform folders, as stored
  QList<Case> cases;
  QList<Recipe> recipes;

  // Paths as stored may be relative to the study folder.
  QString resolved(const QString &path) const;
  QString stored(const QString &absolutePath) const;
  QString catalogPath() const { return resolved(catalog); }
  // The station list to read: the study's, or the built-in AFAD one.
  QString stationListPath() const;
  // The station's own folder if it names one; otherwise <root>/<code> in the
  // first waveform root that has it (the first root when none has).
  QString dataDirFor(const StationRef &s) const;
  // Every waveform folder, absolute, `dataRoot` first.
  QStringList waveformRoots() const;
  // Adds a waveform folder (it becomes `dataRoot` when there is none yet).
  // False when the study already lists it.
  bool addWaveformRoot(const QString &absolutePath);

  // The inputs that are named but not found on disk.
  QList<MissingInput> missingInputs() const;
  // The user found `oldPath` at `newPath`: what the two have in common at
  // the end is dropped to get the folder that moved (/old/disk -> /mnt/usb),
  // and every missing input under it that exists at the new place is
  // rewritten. Returns how many paths changed.
  int relocate(const QString &oldPath, const QString &newPath);

  const Case *findCase(const QString &id) const;
  const Recipe *findRecipe(const QString &id) const;
  // A slug of `name` no case (or recipe) uses yet.
  QString freeCaseId(const QString &name) const;
  QString freeRecipeId(const QString &name) const;

  QString toToml() const;
  static Study fromToml(const QString &text, const QString &dir); // throws
};

// "Sivrice (Elazığ) 2020" -> "sivrice-elazig-2020"; never empty.
QString slug(const QString &text);

// A case from a catalog event: named after its place and year, 365 days,
// a 100 km circle, no stations yet.
Case caseFromEvent(const analysis::Event &e);
// A reference date at a place: no earthquake.
Case referenceCase(const QString &name, const QDateTime &timeUtc, double lat,
                   double lon);
// Stations within maxKm of the case, nearest first, at most `limit`.
QList<StationRef> nearestStations(const std::vector<analysis::Station> &all,
                                  double lat, double lon, double maxKm,
                                  int limit = 12);

} // namespace skg::study
