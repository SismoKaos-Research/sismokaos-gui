// Driving ayzek (third_party/ayzek, GPL-3.0-or-later; AYZEK-PROVENANCE.md).
// ayzek is not ported: the GUI starts the bundled executable as a separate
// process. This file knows where a copy keeps its models, how to turn the
// Early warning tab's settings into ayzek's command line (app/ayzek.cpp in
// the submodule lists the options), and how to read the lines it prints
// while running and the summary of its final report. No Widgets here.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace skg::ayzek {

// An ayzek installation: the executable and the directory its weights are
// in (`models/` beside the executable, or the executable's own directory
// when it is a loose binary in a checkout).
struct Install {
  QString executable;
  QString dir;       // directory holding the executable
  QString modelsDir; // "" when no models directory was found
  bool hasModels() const { return !modelsDir.isEmpty(); }
};
// Describes the installation an executable belongs to; nullopt when the
// path is not an executable file.
std::optional<Install> installAt(const QString &executable);

enum class Mode { Replay, Live };
enum class Detector { Model, Transformer, StaLta, FilterPicker };
QString detectorArg(Detector d);
enum class Locate { Default, Geometry, Picks };

struct Site {
  QString name;
  double lat = 0, lon = 0;
};

// What the user can set; -1 or 0 means "ayzek's default" for the numbers.
struct Options {
  Mode mode = Mode::Replay;
  // Replay: one miniSEED file per station.
  QStringList files;
  double speed = 1; // 1 real time, 0 as fast as possible
  QString from;     // UTC "yyyy-MM-ddTHH:mm:ss", or ""
  // Live: a SeedLink server and its streams.
  QString server;  // HOST:PORT
  QString streams; // NET.STA[,NET.STA...]
  QString archiveDir;
  double durationS = 0; // 0: until stopped
  // Both.
  QString modelsDir; // --models; "" lets ayzek look for ./models
  QString stations;  // --stations FILE
  QString pdGains;   // --pd-gains FILE
  QString catalog;   // --catalog CSV (AFAD export)
  Detector detector = Detector::Model;
  int minStations = 0;
  double threshold = -1;
  int triggerWindows = 0;
  double instantThreshold = -1;
  double release = -1;
  Locate locate = Locate::Default;
  bool assess = false;
  QString assessCsv;
  QString scoresDir;
  QString recordFile;
  bool noMagnitude = false;
  bool noPick = false;
  bool verbose = false;
  QList<Site> sites;
  QStringList extra; // anything else, passed through as given
};

// The command line for `opt`, without the program. Always --no-color: the
// output goes to the jobs log, not a terminal.
QStringList arguments(const Options &opt);
// "" when `opt` can run, else what is missing (translated).
QString validate(const Options &opt);
// "NAME,LAT,LON" lines, one site each; malformed lines are skipped.
QList<Site> parseSites(const QString &text);
QString sitesText(const QList<Site> &sites);

// One tagged line of ayzek's running output, such as
//   ALARM    #3   18:21:09.00  earthquake detected by KULA, SIMA  (...)
// Tags: ALARM, PLOC, PDMAG, LOCATE, ASSESS, EVENT, ISSUED, MERGE, RANGE.
struct Event {
  QString tag;
  int id = -1;  // the alarm number after '#', -1 when the line has none
  QString time; // "HH:MM:SS.ss" when the line carries one
  QString text;
};
QString stripAnsi(const QString &line);
std::optional<Event> parseEventLine(const QString &line);

// The counts of the final report's "Alarms compared with the AFAD catalogue"
// and "AFAD events within N km" sections; -1 when a line was not seen (no
// catalogue given, or the run ended early).
struct Summary {
  int alarms = -1; // ALARM lines counted
  int correct = -1, falseAlarms = -1;
  int events = -1, detected = -1, detectedBeforeS = -1, missed = -1;
  bool any() const { return alarms >= 0 || correct >= 0 || events >= 0; }
};
Summary parseSummary(const QStringList &log);

} // namespace skg::ayzek
