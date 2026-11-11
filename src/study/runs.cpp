#include "study/runs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPair>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "analysis/events.h"
#include "analysis/label.h"
#include "cbi/magnitudes.h"
#include "cbi/run.h"
#include "core/defaults.h"
#include "fx/calibrate.h"
#include "fx/config.h"
#include "fx/engine.h"
#include "sfc/dataset.h"
#include "sfc/experiments.h"
#include "sfc/run.h"
#include "sfc/settings.h"
#include "study/catalogs.h"
#include "study/waveindex.h"

namespace skg::study {

const QString kToolIndicators = QStringLiteral("indicators");
const QString kToolFeatures = QStringLiteral("features");
const QString kToolClassification = QStringLiteral("classification");
const QString RunRecord::kFileName = QStringLiteral("run.json");
const QString kRunLogName = QStringLiteral("run.log");

// The reasons a cell waits, is stale or failed are shown to the user, so
// they are translated; this gives them a context.
class RunText {
  Q_DECLARE_TR_FUNCTIONS(skg::study::RunText)
};

namespace {

[[noreturn]] void fail(const QString &what) {
  throw std::runtime_error(what.toStdString());
}

QString iso(const QDateTime &t) {
  return t.isValid() ? t.toUTC().toString(Qt::ISODate) : QString();
}

int intOf(const Recipe &r, const char *key, int def) {
  const QVariant v = r.settings.value(QLatin1String(key));
  return v.isValid() ? v.toInt() : def;
}
double numOf(const Recipe &r, const char *key, double def) {
  const QVariant v = r.settings.value(QLatin1String(key));
  return v.isValid() ? v.toDouble() : def;
}
bool boolOf(const Recipe &r, const char *key, bool def) {
  const QVariant v = r.settings.value(QLatin1String(key));
  return v.isValid() ? v.toBool() : def;
}
QString strOf(const Recipe &r, const char *key, const QString &def = {}) {
  const QVariant v = r.settings.value(QLatin1String(key));
  return v.isValid() ? v.toString() : def;
}
QStringList listOf(const Recipe &r, const char *key) {
  QStringList out;
  for (const QVariant &v : r.settings.value(QLatin1String(key)).toList())
    out << v.toString();
  return out;
}

const StationRef *stationOf(const Case &c, const QString &code) {
  for (const StationRef &s : c.stations)
    if (s.code == code)
      return &s;
  return nullptr;
}

// ---- indicators ----

// Events from t0 up to, not including, t1 as an AFAD-layout file in UTC.
int writeEvents(const std::vector<analysis::Event> &events, double t0,
                double t1, const QString &path) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    fail(QStringLiteral("%1: %2").arg(path, f.errorString()));
  QTextStream out(&f);
  out << "Date,Longitude,Latitude,Depth,Rms,Type,Magnitude,Location,EventID\n";
  int n = 0;
  for (const analysis::Event &e : events) {
    if (e.epoch < t0 || e.epoch >= t1)
      continue;
    const QDateTime t = QDateTime::fromSecsSinceEpoch(
        qint64(std::floor(e.epoch)), QTimeZone::UTC);
    QString place = e.place;
    place.replace(QLatin1Char(','), QLatin1Char(' '));
    out << t.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")) << ','
        << QString::number(e.lon, 'f', 4) << ','
        << QString::number(e.lat, 'f', 4) << ','
        << QString::number(e.depthKm, 'f', 2) << ",," << e.type << ','
        << QString::number(e.mag, 'f', 1) << ',' << place << ',' << e.id
        << '\n';
    ++n;
  }
  out.flush();
  if (!f.commit())
    fail(QStringLiteral("%1: %2").arg(path, f.errorString()));
  return n;
}

// Why the study's catalog cannot be read (not chosen, or not where the
// study says it is); empty when it can.
QString catalogProblem(const Study &s) {
  if (s.catalog.isEmpty())
    return RunText::tr("the study has no earthquake catalog of its own: the "
                       "built-in list holds only the large earthquakes "
                       "(magnitude 5 and above), too few for this analysis. "
                       "Choose a full catalog or download one on the Cases "
                       "page");
  if (!QFileInfo::exists(s.catalogPath()))
    return RunText::tr("catalog not found: %1 (choose it again on the Cases "
                       "page)")
        .arg(QDir::toNativeSeparators(s.catalogPath()));
  return {};
}

QString dateText(const QDateTime &t) {
  return t.toUTC().date().toString(Qt::ISODate);
}

// Why the indicators cannot be computed from the study's catalog although
// it is there: its magnitudes start too far above the recipe's completeness
// magnitude. Empty when they can.
QString indicatorsCatalogProblem(const Study &s, const Recipe &r) {
  if (boolOf(r, "estimate_mc", false))
    return {};
  const CatalogSummary cs =
      catalogSummary(s.catalogPath(), s.catalogUtcOffsetHours);
  const double mc = numOf(r, "mc", 1.3);
  if (!cs.ok || cs.floor <= mc + defaults::kMcFloorMargin)
    return {};
  return RunText::tr("the catalog lists earthquakes from about magnitude %1 "
                     "up (%2 events), but the recipe counts them from the "
                     "completeness magnitude %3: the b-value needs the "
                     "earthquakes in between. Use a full catalog (Cases "
                     "page), or edit the recipe: raise its completeness "
                     "magnitude or let it be estimated per case")
      .arg(cs.floor, 0, 'f', 1)
      .arg(cs.events)
      .arg(mc, 0, 'f', 1);
}

// The waveform files a features run reads, as one short text: their names,
// sizes and times, not their contents and not where the folder is. New or
// replaced files for the span change it; a file elsewhere, or the same
// folder on another drive, does not.
QStringList linesOfFiles(const QStringList &files) {
  QStringList lines;
  for (const QString &f : files) {
    const QFileInfo fi(f);
    lines << QStringLiteral("%1|%2|%3")
                 .arg(fi.fileName())
                 .arg(fi.size())
                 .arg(fi.lastModified().toSecsSinceEpoch());
  }
  lines.sort();
  return lines;
}

QString stampOfFiles(const QStringList &files) {
  if (files.isEmpty())
    return {};
  const QStringList lines = linesOfFiles(files);
  return QStringLiteral("%1 files, %2")
      .arg(files.size())
      .arg(QString::fromLatin1(
          QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(),
                                   QCryptographicHash::Sha1)
              .toHex()
              .left(16)));
}

// Whether the files a features run would read now put a finished run out of
// date. Files taken on an estimate (not read through by the scan yet) prove
// nothing either way: a run is not called stale for a file that only may
// overlap its span, nor for having read, on such an estimate, more than the
// exact scan later says it needed.
bool filesChanged(const RunRecord &rec, const WaveChoice &now) {
  if (rec.waveFiles.isEmpty())
    return true; // recorded before the files were listed: only the stamp
  const QStringList nowLines = linesOfFiles(now.files);
  const QStringList unsure = linesOfFiles(now.uncertain);
  for (const QString &line : nowLines)
    if (!unsure.contains(line) && !rec.waveFiles.contains(line))
      return true; // a file known to hold the span that the run did not read
  if (!rec.waveEstimated)
    for (const QString &line : rec.waveFiles)
      if (!nowLines.contains(line))
        return true; // a file the run read is gone or was replaced
  return false;
}

QDateTime utcOf(double epoch) {
  return QDateTime::fromSecsSinceEpoch(qint64(std::floor(epoch)),
                                       QTimeZone::UTC);
}

void runIndicators(const Study &s, const Case &c, const Recipe &r,
                   const QString &dir, const RunHooks &hooks, RunRecord &rec) {
  QString problem = catalogProblem(s);
  if (problem.isEmpty())
    problem = indicatorsCatalogProblem(s, r);
  if (!problem.isEmpty())
    fail(problem);
  const int windowMonths = std::max(1, intOf(r, "window_months", 12));
  const QString spanCsv = QDir(dir).filePath(QStringLiteral("catalog.csv"));
  rec.catalogSha = sha256Cached(s.catalogPath());
  rec.inputs.insert(s.catalogPath(), rec.catalogSha);
  const std::vector<analysis::Event> events =
      analysis::loadEvents(s.catalogPath(), s.catalogUtcOffsetHours);
  // Up to, not including, the event. The earthquake itself is looked up in
  // the catalog: were the catalog's time offset wrong, its time there would
  // differ from the case's by hours and it would count as its own
  // forerunner.
  double t1 = double(c.time.toSecsSinceEpoch());
  if (const analysis::Event *own = eventOfCase(c, events)) {
    if (std::abs(own->epoch - t1) > 1.5 && hooks.log)
      hooks.log(
          RunText::tr("warning: the catalog places this earthquake at %1, "
                      "the case at %2. The catalog's time offset on the "
                      "Cases page may be wrong; events are used up to the "
                      "earlier of the two.")
              .arg(iso(utcOf(own->epoch)), iso(c.time)));
    t1 = std::min(t1, own->epoch);
  }
  const QDateTime from = c.spanStart().addMonths(-windowMonths);
  const int n =
      writeEvents(events, double(from.toSecsSinceEpoch()), t1, spanCsv);
  if (hooks.log)
    hooks.log(RunText::tr("%1 catalog events from %2 to %3")
                  .arg(n)
                  .arg(iso(from), iso(utcOf(t1))));
  if (n == 0)
    fail(RunText::tr("the catalog has no events in the case's span (%1 to "
                     "%2): check the span and the dates the catalog covers")
             .arg(iso(from), iso(c.time)));

  cbi::RunOptions o;
  o.catalogPath = spanCsv;
  if (c.zone.kind == Zone::Kind::Circle) {
    o.selection.zone = cbi::CatalogSelection::Zone::Circle;
    o.selection.centerLat = c.lat;
    o.selection.centerLon = c.lon;
    o.selection.radiusKm = c.zone.radiusKm;
  } else {
    o.selection.zone = c.zone.faultGeoJson.isEmpty()
                           ? cbi::CatalogSelection::Zone::Eafz
                           : cbi::CatalogSelection::Zone::GeoJson;
    o.selection.zoneFile = s.resolved(c.zone.faultGeoJson);
    o.selection.strands = c.zone.strands;
    o.selection.bufferKm = c.zone.bufferKm;
  }
  o.selection.homogenize = boolOf(r, "homogenize", false);
  o.selection.decluster = boolOf(r, "decluster", false);
  o.config.mc = numOf(r, "mc", 1.3);
  o.config.magBin = numOf(r, "mag_bin", cbi::kDefaultMagBin);
  o.config.window = cbi::Duration{windowMonths, QLatin1Char('M')};
  o.config.step =
      cbi::Duration{std::max(1, intOf(r, "step_months", 1)), QLatin1Char('M')};
  o.config.halfGaussSigma = numOf(r, "half_gauss_sigma", 0.4);
  o.maWindows.clear();
  for (const QVariant &v :
       r.settings.value(QStringLiteral("ma_windows")).toList())
    if (v.toInt() > 0)
      o.maWindows.push_back(v.toInt());
  o.bollinger = boolOf(r, "bollinger", false);
  o.bbWindow = intOf(r, "bb_window", 20);
  o.bbK = numOf(r, "bb_k", 2.0);
  o.plots = boolOf(r, "figures", true);
  o.dpi = intOf(r, "figure_dpi", defaults::kFigureDpi);
  o.labelMag = numOf(r, "label_mag", defaults::kRiskLabelMag);
  o.labelDays = std::max(1, intOf(r, "label_days", defaults::kRiskLabelDays));
  o.lang = strOf(r, "figure_language", QStringLiteral("en"));
  o.outRoot = dir;

  const cbi::Catalog selected =
      o.selection.apply(cbi::loadCatalog(spanCsv), nullptr);
  if (boolOf(r, "estimate_mc", false)) {
    if (selected.size() < 50)
      fail(RunText::tr("only %1 events in the zone: too few to estimate "
                          "the completeness magnitude")
               .arg(selected.size()));
    o.config.mc = cbi::maxCurvatureMc(selected.magnitudes(), o.config.magBin);
    rec.fitted.insert(QStringLiteral("mc"), o.config.mc);
    if (hooks.log)
      hooks.log(RunText::tr("completeness magnitude estimated: %1")
                    .arg(o.config.mc, 0, 'f', 2));
  }
  rec.fitted.insert(QStringLiteral("events_in_zone"), int(selected.size()));

  cbi::RunHooks h;
  h.log = hooks.log;
  h.shouldStop = hooks.shouldStop;
  h.progress = [&hooks](int done, int total, const QString &what) {
    if (hooks.progress && total > 0)
      hooks.progress(double(done) / total, what);
  };
  const cbi::RunResult res = cbi::runIndicators(o, h);
  if (res.stopped)
    return;
  rec.outputs.insert(QStringLiteral("table"),
                     QDir(dir).relativeFilePath(res.csvPath));
  rec.outputs.insert(QStringLiteral("folder"),
                     QDir(dir).relativeFilePath(res.outDir));
}

// ---- features ----

// What a calibration depends on, as the kept file lists it, and its hash.
struct CalibrationKey {
  QJsonObject parts;
  QString id;
};

CalibrationKey calibrationKey(const fx::Config &run, const QString &catalogId,
                              double utcOffsetHours, const StationRef &st,
                              const Case &c, const QString &waveStamp,
                              bool waveEstimated) {
  QJsonObject settings;
  settings.insert(QStringLiteral("band"), run.band);
  settings.insert(QStringLiteral("fs"), run.fs);
  settings.insert(QStringLiteral("win_sec"), run.winSec);
  settings.insert(QStringLiteral("freqmin"), run.freqmin);
  settings.insert(QStringLiteral("freqmax"), run.freqmax);
  settings.insert(QStringLiteral("sampent_r"), run.chaosSampentR);
  const fx::CalibrationConfig &k = run.calibration;
  settings.insert(QStringLiteral("min_mag"), k.minMag);
  settings.insert(QStringLiteral("max_dist_km"), k.maxDistKm);
  settings.insert(QStringLiteral("pre_sec"), k.preSec);
  settings.insert(QStringLiteral("min_post_sec"), k.minPostSec);
  settings.insert(QStringLiteral("coda_factor"), k.codaFactor);
  settings.insert(QStringLiteral("max_windows"), k.maxWindows);
  settings.insert(QStringLiteral("loud_factor"), k.loudFactor);
  settings.insert(QStringLiteral("seed"), QString::number(k.seed));
  settings.insert(QStringLiteral("max_tau"), k.maxTau);
  settings.insert(QStringLiteral("max_dim"), k.maxDim);
  CalibrationKey key;
  key.parts.insert(QStringLiteral("settings"), settings);
  key.parts.insert(QStringLiteral("catalog"),
                   QStringLiteral("%1 utc%2")
                       .arg(catalogId)
                       .arg(utcOffsetHours, 0, 'f', 2));
  key.parts.insert(QStringLiteral("station"),
                   QStringLiteral("%1 %2 %3")
                       .arg(st.code)
                       .arg(st.lat, 0, 'f', 5)
                       .arg(st.lon, 0, 'f', 5));
  key.parts.insert(QStringLiteral("span"),
                   iso(c.spanStart()) + QLatin1Char('/') + iso(c.time));
  key.parts.insert(QStringLiteral("waveforms"),
                   waveStamp + (waveEstimated ? QStringLiteral(" (estimated)")
                                              : QString()));
  key.id = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(key.parts).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex()
          .left(16));
  return key;
}

// The kept calibrations of a cell, newest first.
QFileInfoList keptCalibrations(const QString &store) {
  return QDir(store).entryInfoList({QStringLiteral("*.json")}, QDir::Files,
                                   QDir::Time);
}

// Why no kept calibration fits: what differs from the newest one kept.
QString whyRecalibrated(const QString &store, const CalibrationKey &key) {
  const QFileInfoList kept = keptCalibrations(store);
  if (kept.isEmpty())
    return RunText::tr("none is kept for this cell yet");
  QFile f(kept.first().filePath());
  const QJsonObject was =
      f.open(QIODevice::ReadOnly)
          ? QJsonDocument::fromJson(f.readAll())
                .object()
                .value(QStringLiteral("kept_for"))
                .toObject()
          : QJsonObject();
  const auto differs = [&](const char *part) {
    return was.value(QLatin1String(part)) != key.parts.value(QLatin1String(part));
  };
  if (was.isEmpty())
    return RunText::tr("the one kept cannot be read");
  if (differs("catalog"))
    return RunText::tr("the catalog changed");
  if (differs("station"))
    return RunText::tr("the station's position changed");
  if (differs("span"))
    return RunText::tr("the span changed");
  if (differs("waveforms"))
    return RunText::tr("the waveform files changed");
  if (differs("settings"))
    return RunText::tr("the calibration settings changed");
  return RunText::tr("the one kept cannot be read");
}

// Copies a calibration fx has just written into the cell's store, with what
// it was made for; the latest two are kept. Failing to keep it is not a
// reason to fail the run.
bool keepCalibration(const QString &written, const QString &store,
                     const CalibrationKey &key) {
  QFile in(written);
  if (!in.open(QIODevice::ReadOnly))
    return false;
  QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
  if (o.isEmpty() || !QDir().mkpath(store))
    return false;
  o.insert(QStringLiteral("kept_key"), key.id);
  o.insert(QStringLiteral("kept_for"), key.parts);
  o.insert(QStringLiteral("kept_utc"), iso(QDateTime::currentDateTimeUtc()));
  const QString target = QDir(store).filePath(key.id + QStringLiteral(".json"));
  QSaveFile out(target);
  const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Indented);
  if (!out.open(QIODevice::WriteOnly) || out.write(json) != json.size() ||
      !out.commit())
    return false;
  const QFileInfoList kept = keptCalibrations(store);
  int others = 0;
  for (const QFileInfo &fi : kept) {
    if (fi.fileName() == key.id + QStringLiteral(".json"))
      continue;
    if (++others > 1)
      QFile::remove(fi.filePath());
  }
  return true;
}

// The sentence for a station that has no waveform files in the case's span:
// with the dates its data does run over when the index knows the station.
QString noSpanData(const Study &s, const Case &c, const StationRef &st) {
  const SpanData d =
      spanData(*WaveIndex::shared(s.dir), st.code, c.spanStart(), c.time);
  if (d.known && d.first.isValid())
    return RunText::tr("%1 has waveforms from %2 to %3, none in this case's "
                       "span (%4 to %5): untick the station on the Cases "
                       "page, or get its waveforms for the span on the Data "
                       "page")
        .arg(st.code, d.first.toString(Qt::ISODate),
             d.last.toString(Qt::ISODate), dateText(c.spanStart()),
             dateText(c.time));
  return RunText::tr("no waveform files for %1 in this case's span (%2 to "
                     "%3): add or scan its waveform folder on the Cases "
                     "page, or get the waveforms on the Data page")
      .arg(st.code, dateText(c.spanStart()), dateText(c.time));
}

void runFeatures(const Study &s, const Case &c, const QString &station,
                 const Recipe &r, const QString &dir, const RunHooks &hooks,
                 RunRecord &rec) {
  const StationRef *st = stationOf(c, station);
  if (!st)
    fail(RunText::tr("the case has no station %1").arg(station));
  // Only the files that overlap the case's span are read (waveindex.h).
  // While the exact scan is still running, some are taken on an estimate
  // with a day of margin: more may be read than needed, never less.
  const WaveChoice chosen = chooseWaveFiles(s, *WaveIndex::shared(s.dir), *st,
                                            c.spanStart(), c.time);
  const QString chosenHow = chosen.how;
  const QStringList inputFiles = chosen.files;
  if (inputFiles.isEmpty())
    fail(noSpanData(s, c, *st) +
         QStringLiteral(" (%1)").arg(chosenHow));
  const QString dataDir = QFileInfo(inputFiles.first()).absolutePath();
  if (hooks.log)
    hooks.log(RunText::tr(" Waveforms from %1").arg(chosenHow));
  rec.waveStamp = stampOfFiles(inputFiles);
  rec.waveFiles = linesOfFiles(inputFiles);
  rec.waveEstimated = !chosen.uncertain.isEmpty();
  fx::Config cfg;
  const QString json = strOf(r, "config_json");
  if (!json.trimmed().isEmpty()) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (!doc.isObject())
      fail(RunText::tr("the recipe's config is not valid JSON: %1")
               .arg(err.errorString()));
    cfg = fx::Config::fromJson(doc.object());
  }
  cfg.station = station;
  cfg.dataDir = dataDir;
  cfg.inputFiles = inputFiles;
  cfg.outputRoot = dir;
  cfg.calibrationFile.clear(); // every case calibrates for itself
  QString catalogId; // SHA-256 of the catalog the calibration reads
  double catalogOffset = 0.0;
  if (cfg.calibration.catalog.isEmpty()) {
    const QString problem = catalogProblem(s);
    if (!problem.isEmpty())
      fail(problem);
    rec.catalogSha = sha256Cached(s.catalogPath());
    rec.inputs.insert(s.catalogPath(), rec.catalogSha);
    catalogId = rec.catalogSha;
    catalogOffset = s.catalogUtcOffsetHours;
    if (s.catalogUtcOffsetHours == 0.0) {
      cfg.calibration.catalog = s.catalogPath();
    } else {
      // fx reads the catalog's times as UTC, so it is given a UTC copy.
      const QString utc = QDir(dir).filePath(QStringLiteral("catalog_utc.csv"));
      const int n = writeEvents(
          analysis::loadEvents(s.catalogPath(), s.catalogUtcOffsetHours),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::infinity(), utc);
      if (hooks.log)
        hooks.log(RunText::tr("calibration catalog: %1 events moved to UTC "
                              "(the study's catalog is %2 h from UTC)")
                      .arg(n)
                      .arg(s.catalogUtcOffsetHours, 0, 'g', 3));
      cfg.calibration.catalog = utc;
    }
  } else {
    // The recipe's config names a catalog of its own: an explicit override
    // of the study's, read as it is (fx takes its times as UTC).
    if (!QFileInfo::exists(cfg.calibration.catalog))
      fail(RunText::tr("the recipe's own calibration catalog is not found: "
                       "%1 (edit the recipe; clearing it there uses the "
                       "study's catalog)")
               .arg(QDir::toNativeSeparators(cfg.calibration.catalog)));
    if (hooks.log)
      hooks.log(RunText::tr("calibration catalog: the recipe's own, %1 (not "
                            "the study's)")
                    .arg(cfg.calibration.catalog));
    catalogId = sha256Cached(cfg.calibration.catalog);
    rec.inputs.insert(cfg.calibration.catalog, catalogId);
  }
  cfg.calibration.stationLat = st->lat;
  cfg.calibration.stationLon = st->lon;
  const QStringList problems = cfg.validate();
  if (!problems.isEmpty())
    fail(problems.join(QStringLiteral("; ")));
  const QList<fx::Config> runs = cfg.bandRuns();
  const QString store = runCalibrationDir(runDir(s, c, station, r));
  QStringList keys;
  bool allReused = !runs.isEmpty();
  for (int i = 0; i < int(runs.size()); ++i) {
    fx::Config run = runs[i];
    // The calibration this cell made before, for exactly these inputs and
    // settings, is used again (runs.h, "Calibrations are kept").
    const CalibrationKey key = calibrationKey(
        run, catalogId, catalogOffset, *st, c, rec.waveStamp,
        rec.waveEstimated);
    keys << key.id;
    const QString kept = QDir(store).filePath(key.id + QStringLiteral(".json"));
    bool reused = false;
    if (!hooks.freshCalibration && QFileInfo::exists(kept)) {
      try {
        const fx::Calibration cal = fx::Calibration::load(kept);
        QFile f(kept);
        const QString made =
            f.open(QIODevice::ReadOnly)
                ? QJsonDocument::fromJson(f.readAll())
                      .object()
                      .value(QStringLiteral("kept_utc"))
                      .toString()
                : QString();
        run.calibrationFile = kept;
        reused = true;
        if (hooks.log)
          hooks.log(RunText::tr(" Calibration reused: the one this cell made "
                                "on %1 for the same data, catalog and "
                                "settings (%2)")
                        .arg(made, key.id));
        Q_UNUSED(cal);
      } catch (const std::exception &) {
        reused = false; // unreadable: made again below
      }
    }
    if (!reused && hooks.log)
      hooks.log(RunText::tr(" Calibration redone: %1")
                    .arg(hooks.freshCalibration
                             ? RunText::tr("asked for")
                             : whyRecalibrated(store, key)));
    allReused = allReused && reused;
    fx::RunHooks h;
    h.calibrated = [&hooks, store, key](const QString &path) {
      if (!keepCalibration(path, store, key) && hooks.log)
        hooks.log(RunText::tr(" warning: the calibration could not be kept "
                              "in %1")
                      .arg(QDir::toNativeSeparators(store)));
    };
    h.log = hooks.log;
    h.shouldStop = hooks.shouldStop;
    h.phase = [&hooks](const QString &p) {
      if (hooks.log)
        hooks.log(QStringLiteral(" %1").arg(p));
    };
    // One bar for the whole run: the phases of each band (unpacking,
    // calibrating, extracting) in their shares, the bands one after the
    // other. It never starts again from zero.
    h.overall = [&hooks, i, &runs, &run](const fx::RunProgress &p) {
      if (!hooks.progress)
        return;
      const QString band =
          runs.size() > 1
              ? RunText::tr("band %1 of %2 (%3) · ")
                    .arg(i + 1)
                    .arg(runs.size())
                    .arg(run.band)
              : QString();
      hooks.progress((i + p.fraction) / double(runs.size()), band + p.text);
    };
    const fx::RunResult res = fx::runPipeline(run, h);
    if (res.stopped)
      return;
    if (res.featuresNpy.isEmpty())
      fail(RunText::tr("the feature run wrote no table"));
    const QString suffix =
        run.band.isEmpty() ? QString() : QLatin1Char('_') + run.band;
    rec.outputs.insert(QStringLiteral("features") + suffix,
                       QDir(dir).relativeFilePath(res.featuresNpy));
    if (i == 0 && !run.band.isEmpty())
      rec.outputs.insert(QStringLiteral("features"),
                         QDir(dir).relativeFilePath(res.featuresNpy));
    rec.fitted.insert(QStringLiteral("windows") + suffix, res.windows);
    // The calibrated chaos parameters, as the run wrote them.
    QFile cal(res.calibration);
    if (cal.open(QIODevice::ReadOnly)) {
      const QJsonObject o = QJsonDocument::fromJson(cal.readAll()).object();
      for (auto it = o.begin(); it != o.end(); ++it)
        if (it.value().isDouble())
          rec.fitted.insert(it.key() + suffix, it.value().toDouble());
    }
  }
  rec.inputs.insert(dataDir, QStringLiteral("(folder)"));
  rec.calibrationKey = keys.join(QLatin1Char(','));
  rec.calibrationReused = allReused;
}

// ---- classification ----

// "31-90" -> (31, 90); throws on anything else.
QPair<int, int> dayRange(const QString &text) {
  const QStringList parts = text.split(QLatin1Char('-'));
  bool a = false, b = false;
  const int first = parts.value(0).trimmed().toInt(&a);
  const int last = parts.value(1).trimmed().toInt(&b);
  if (parts.size() != 2 || !a || !b || first < 0 || last < first)
    fail(RunText::tr("\"%1\" is not a range of days such as 31-90")
             .arg(text));
  return {first, last};
}

// The feature table of the features recipe this recipe builds on.
QString featureTableFor(const Study &s, const Case &c, const QString &station,
                        const Recipe &r, QString *why, Fix *fix = nullptr,
                        QString *upstream = nullptr) {
  const QString fid = strOf(r, "features_recipe");
  const Recipe *fr = s.findRecipe(fid);
  if (!fr || fr->tool != kToolFeatures) {
    *why = fid.isEmpty()
               ? RunText::tr("the recipe names no features recipe: choose "
                             "one with Edit recipe")
               : RunText::tr("its features recipe \"%1\" is no longer in "
                             "the study: choose another with Edit recipe")
                     .arg(fid);
    if (fix)
      *fix = Fix::Recipe;
    return {};
  }
  const Cell cell = cellOf(s, c, station, *fr);
  if (cell.state != CellState::Done || !cell.record) {
    *why = RunText::tr("needs the features of \"%1\" first: run that cell "
                       "(Run missing and stale does it in order)")
               .arg(fr->name);
    if (fix)
      *fix = Fix::RunFirst;
    return {};
  }
  if (upstream)
    *upstream = iso(cell.record->finished);
  const QString band = strOf(r, "band");
  const QString name = band.isEmpty() ? QStringLiteral("features")
                                      : QStringLiteral("features_") + band;
  const QString rel = cell.record->outputs.value(name).toString();
  if (rel.isEmpty()) {
    *why = RunText::tr("the features run has no table %1").arg(name);
    return {};
  }
  return QDir(cell.dir).filePath(rel);
}

sfc::Settings sfcSettings(const Case &c, const QString &station,
                          const Recipe &r, const fx::NpyTable &table) {
  sfc::Settings s;
  const QString extra = strOf(r, "sfc_toml");
  if (!extra.trimmed().isEmpty())
    s = sfc::Settings::fromToml(extra, s);
  s.station = station;
  s.earthquakeName = c.id;
  s.eqDate = c.time.date();
  s.eqCutoffHour = c.time.time().hour();
  const int perClass = std::max(1, intOf(r, "days_per_class", 30));
  s.classTable.clear();
  s.fixedDates.clear();
  int label = 0;
  for (const QString &text : listOf(r, "classes")) {
    const QPair<int, int> range = dayRange(text);
    const int span = range.second - range.first + 1;
    const int count = std::min(perClass, span);
    sfc::ClassRow row;
    row.label = label++;
    row.firstDay = range.first;
    row.lastDay = range.second;
    row.step = std::max(1, span / count);
    row.count = count;
    s.classTable << row;
  }
  if (s.classTable.size() < 2)
    fail(RunText::tr("the recipe needs at least two classes"));
  s.labelMerge.reset();
  s.classMonths.clear();
  QStringList columns = listOf(r, "feature_columns");
  if (columns.isEmpty())
    for (const fx::NpyField &f : table.fields)
      if (f.dtype.contains(QLatin1Char('f')) &&
          f.name != QLatin1String("Zaman_Dk"))
        columns << f.name;
  s.featureColumns = columns;
  s.trainSize = numOf(r, "train_size", s.trainSize);
  s.valSize = numOf(r, "val_size", s.valSize);
  s.scaler = strOf(r, "scaler", s.scaler);
  s.randomSeed = intOf(r, "random_seed", s.randomSeed);
  const int folds = intOf(r, "cv_folds", 0);
  if (folds >= 2)
    s.cvFolds = folds;
  else
    s.cvFolds.reset();
  s.runName = QStringLiteral("run");
  s.saveModels = false;
  s.minDayHours = numOf(r, "min_day_hours", s.minDayHours);

  QStringList models = listOf(r, "models");
  if (models.isEmpty())
    models = {QStringLiteral("logistic"), QStringLiteral("svm_rbf"),
              QStringLiteral("random_forest"), QStringLiteral("xgboost")};
  s.modelsToRun.clear();
  s.treeModelsToRun.clear();
  bool ann = false;
  for (const QString &m : models) {
    if (m == QLatin1String("logistic") || m.startsWith(QLatin1String("svm_")))
      s.modelsToRun << m;
    else if (m == QLatin1String("ann"))
      ann = true;
    else
      s.treeModelsToRun << m;
  }
  s.runSteps = {QStringLiteral("split")};
  if (!s.modelsToRun.isEmpty())
    s.runSteps << QStringLiteral("baseline");
  if (ann)
    s.runSteps << QStringLiteral("ann");
  if (!s.treeModelsToRun.isEmpty())
    s.runSteps << QStringLiteral("trees");
  s.runSteps << QStringLiteral("compare");

  if (boolOf(r, "quick", false)) {
    // One small setting per model: for a first look and for the tests.
    s.logisticGrid = sfc::Grid{{"C", {1}}};
    s.svmGrids = {{"svm_linear", sfc::Grid{{"C", {1}}}},
                  {"svm_poly", sfc::Grid{{"C", {1}}, {"degree", {3}}}},
                  {"svm_rbf", sfc::Grid{{"C", {1}},
                                        {"gamma", {QStringLiteral("scale")}}}}};
    s.treeGrids = {{"random_forest", sfc::Grid{{"n_estimators", {30}},
                                               {"max_depth", {6}},
                                               {"min_samples_leaf", {1}}}},
                   {"gradient_boosting", sfc::Grid{{"n_estimators", {30}},
                                                   {"learning_rate", {0.1}},
                                                   {"max_depth", {2}}}},
                   {"xgboost", sfc::Grid{{"n_estimators", {30}},
                                         {"learning_rate", {0.1}},
                                         {"max_depth", {3}}}}};
    s.annGrid = sfc::Grid{{"hidden_layer_sizes", {QVariantList{8}}},
                          {"batch_size", {256}},
                          {"alpha", {0.001}},
                          {"learning_rate_init", {0.001}}};
    s.annMaxEpochs = 30;
    s.svmMaxTrainWindows = 2000;
  }
  return s;
}

void runClassification(const Study &s, const Case &c, const QString &station,
                       const Recipe &r, const QString &dir,
                       const RunHooks &hooks, RunRecord &rec) {
  QString why;
  const QString tablePath =
      featureTableFor(s, c, station, r, &why, nullptr, &rec.upstream);
  if (tablePath.isEmpty())
    fail(why);
  const fx::NpyTable table = analysis::loadTable(tablePath);
  const sfc::Settings st = sfcSettings(c, station, r, table);
  const QMap<QDate, int> perDay = sfc::fxWindowsPerDay(table);
  int typical = 0;
  for (int n : perDay)
    typical = std::max(typical, n);
  const sfc::DaySelection sel =
      sfc::selectDays(st, perDay, int(typical * st.minDayHours / 24.0));
  if (hooks.log)
    for (const QString &l : sel.log)
      hooks.log(l);
  sfc::Dataset d = sfc::fromFxTable(table, st, sel, st.featureColumns);
  QStringList prep;
  const int dropped = sfc::prepareClasses(d, st, &prep);
  if (hooks.log)
    for (const QString &l : prep)
      hooks.log(l);
  if (d.rows() == 0)
    fail(RunText::tr("no window is left to train on (%1 dropped for "
                        "missing values)")
             .arg(dropped));
  QDir().mkpath(sfc::resultsDir(dir, st));
  sfc::saveSfcCsv(sfc::resultsDir(dir, st) + QLatin1Char('/') + st.station +
                      QStringLiteral("_N_features_labeled.csv"),
                  d, st);
  // A run starts from an empty experiments table, so the table holds this
  // run only.
  const QString experiments =
      sfc::modelsDir(dir, st) + QStringLiteral("/experiments.csv");
  QFile::remove(experiments);

  sfc::RunHooks h;
  h.log = hooks.log;
  h.shouldStop = hooks.shouldStop;
  h.progress = [&hooks](int done, int total, const QString &what) {
    if (hooks.progress && total > 0)
      hooks.progress(double(done) / total, what);
  };
  const std::vector<sfc::StepOutcome> outcomes =
      sfc::runSteps(dir, st, d, st.runSteps, h);
  if (hooks.shouldStop && hooks.shouldStop())
    return;
  for (const sfc::StepOutcome &o : outcomes)
    if (o.status == QLatin1String("FAILED"))
      fail(RunText::tr("step %1 failed: %2").arg(o.step, o.error));
  if (!QFileInfo::exists(experiments))
    fail(RunText::tr("the run trained no model"));
  rec.inputs.insert(tablePath, sha256Of(tablePath));
  rec.outputs.insert(QStringLiteral("experiments"),
                     QDir(dir).relativeFilePath(experiments));
  rec.fitted.insert(QStringLiteral("windows"), d.rows());
  rec.fitted.insert(QStringLiteral("days"), int(sel.rows.size()));
  rec.fitted.insert(QStringLiteral("features"), int(st.featureColumns.size()));
}

} // namespace

bool waveFilesChanged(const RunRecord &record, const WaveChoice &now) {
  return filesChanged(record, now);
}

QStringList waveFileLines(const QStringList &files) {
  return linesOfFiles(files);
}

QStringList recipeTools() {
  return {kToolIndicators, kToolFeatures, kToolClassification};
}

bool perStation(const QString &tool) { return tool != kToolIndicators; }

Recipe defaultRecipe(const QString &tool) {
  Recipe r;
  r.tool = tool;
  if (tool == kToolIndicators) {
    r.name = QStringLiteral("Indicators 12 month window");
    r.settings = {{QStringLiteral("estimate_mc"), true},
                  {QStringLiteral("mc"), 1.3},
                  {QStringLiteral("mag_bin"), 0.1},
                  {QStringLiteral("window_months"), 12},
                  {QStringLiteral("step_months"), 1},
                  {QStringLiteral("half_gauss_sigma"), 0.4},
                  {QStringLiteral("ma_windows"), QVariantList{7, 15, 23}},
                  {QStringLiteral("bollinger"), false},
                  {QStringLiteral("bb_window"), 20},
                  {QStringLiteral("bb_k"), 2.0},
                  {QStringLiteral("homogenize"), false},
                  {QStringLiteral("decluster"), false},
                  {QStringLiteral("figures"), true},
                  {QStringLiteral("figure_dpi"), defaults::kFigureDpi},
                  {QStringLiteral("label_mag"), defaults::kRiskLabelMag},
                  {QStringLiteral("label_days"), defaults::kRiskLabelDays},
                  {QStringLiteral("figure_language"), QStringLiteral("en")}};
    r.fitted = {QStringLiteral("mc")};
  } else if (tool == kToolFeatures) {
    r.name = QStringLiteral("Features");
    r.settings = {{QStringLiteral("config_json"), QString()}};
    r.fitted = {QStringLiteral("chaos parameters (calibration)")};
  } else if (tool == kToolClassification) {
    r.name = QStringLiteral("Days before the event");
    r.settings = {
        {QStringLiteral("features_recipe"), QString()},
        {QStringLiteral("band"), QString()},
        {QStringLiteral("classes"),
         QVariantList{QStringLiteral("1-30"), QStringLiteral("31-90"),
                      QStringLiteral("91-180"), QStringLiteral("181-360")}},
        {QStringLiteral("days_per_class"), 30},
        {QStringLiteral("models"),
         QVariantList{QStringLiteral("logistic"), QStringLiteral("svm_rbf"),
                      QStringLiteral("random_forest"),
                      QStringLiteral("xgboost")}},
        {QStringLiteral("feature_columns"), QVariantList{}},
        {QStringLiteral("train_size"), 0.6},
        {QStringLiteral("val_size"), 0.2},
        {QStringLiteral("scaler"), QStringLiteral("robust")},
        {QStringLiteral("random_seed"), 42},
        {QStringLiteral("cv_folds"), 5},
        {QStringLiteral("quick"), false},
        {QStringLiteral("sfc_toml"), QString()}};
    r.fitted = {QStringLiteral("scaler"),
                QStringLiteral("feature table (calibration)")};
  }
  return r;
}

QString runDir(const Study &s, const Case &c, const QString &station,
               const Recipe &r) {
  return QDir(s.dir).filePath(QStringLiteral("%1/%2/%3/%4")
                                  .arg(c.id,
                                       perStation(r.tool) && !station.isEmpty()
                                           ? station
                                           : QStringLiteral("_case"),
                                       r.tool, r.id));
}

QString runTmpDir(const QString &dir) {
  return dir + QStringLiteral(".tmp");
}

QString runCalibrationDir(const QString &dir) {
  return dir + QStringLiteral(".calibration");
}

QString runFailedDir(const QString &dir) {
  return dir + QStringLiteral(".failed");
}

QString calibrationOverride(const Recipe &features) {
  const QString json =
      features.settings.value(QStringLiteral("config_json")).toString();
  if (json.trimmed().isEmpty())
    return {};
  return QJsonDocument::fromJson(json.toUtf8())
      .object()
      .value(QStringLiteral("chaos_calibration"))
      .toObject()
      .value(QStringLiteral("catalog"))
      .toString();
}

QString runSignature(const Study &s, const Case &c, const QString &station,
                     const QString &tool) {
  const QString offset = QString::number(s.catalogUtcOffsetHours, 'f', 2);
  if (tool == kToolIndicators)
    return QStringLiteral("indicators|%1|%2|%3|%4|%5|%6|%7|%8|utc%9")
        .arg(iso(c.time))
        .arg(c.lat, 0, 'f', 5)
        .arg(c.lon, 0, 'f', 5)
        .arg(c.spanDays)
        .arg(c.zone.kind == Zone::Kind::Fault ? QStringLiteral("f")
                                              : QStringLiteral("c"))
        .arg(c.zone.kind == Zone::Kind::Fault ? c.zone.bufferKm
                                              : c.zone.radiusKm,
             0, 'f', 2)
        .arg(c.zone.faultGeoJson, c.zone.strands.join(QLatin1Char(',')), offset);
  const StationRef *st = stationOf(c, station);
  if (tool == kToolFeatures) {
    // The zone is not used; the span is (only its days are processed).
    QString sig = QStringLiteral("features|%1|%2|utc%3")
                      .arg(iso(c.time))
                      .arg(c.spanDays)
                      .arg(offset);
    if (st)
      sig += QStringLiteral("|%1|%2|%3|%4")
                 .arg(st->code)
                 .arg(st->lat, 0, 'f', 5)
                 .arg(st->lon, 0, 'f', 5)
                 .arg(st->dataDir);
    return sig;
  }
  // Classification: the classes are days before the case's time, on one
  // station's table. Neither the zone nor the span enters.
  return QStringLiteral("%1|%2|%3")
      .arg(tool, iso(c.time), st ? st->code : QString());
}

bool RunRecord::write(const QString &dir) const {
  QJsonObject o;
  o.insert(QStringLiteral("tool"), tool);
  o.insert(QStringLiteral("recipe"), recipe);
  o.insert(QStringLiteral("case"), caseId);
  o.insert(QStringLiteral("station"), station);
  o.insert(QStringLiteral("revision"), revision);
  o.insert(QStringLiteral("case_signature"), caseSignature);
  o.insert(QStringLiteral("catalog_sha256"), catalogSha);
  o.insert(QStringLiteral("upstream"), upstream);
  o.insert(QStringLiteral("waveforms"), waveStamp);
  if (!waveFiles.isEmpty()) {
    o.insert(QStringLiteral("waveform_files"),
             QJsonArray::fromStringList(waveFiles));
    o.insert(QStringLiteral("waveforms_estimated"), waveEstimated);
  }
  o.insert(QStringLiteral("app_version"), appVersion);
  o.insert(QStringLiteral("started"), iso(started));
  o.insert(QStringLiteral("finished"), iso(finished));
  o.insert(QStringLiteral("ok"), ok);
  o.insert(QStringLiteral("error"), error);
  if (!errorReason.isEmpty())
    o.insert(QStringLiteral("error_reason"), errorReason);
  if (!warnings.isEmpty())
    o.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
  if (!calibrationKey.isEmpty()) {
    o.insert(QStringLiteral("calibration_key"), calibrationKey);
    o.insert(QStringLiteral("calibration_reused"), calibrationReused);
  }
  o.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings));
  o.insert(QStringLiteral("inputs"), QJsonObject::fromVariantMap(inputs));
  o.insert(QStringLiteral("fitted"), QJsonObject::fromVariantMap(fitted));
  o.insert(QStringLiteral("outputs"), QJsonObject::fromVariantMap(outputs));
  QSaveFile f(QDir(dir).filePath(kFileName));
  return f.open(QIODevice::WriteOnly) &&
         f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) >= 0 &&
         f.commit();
}

std::optional<RunRecord> RunRecord::read(const QString &dir) {
  QFile f(QDir(dir).filePath(kFileName));
  if (!f.open(QIODevice::ReadOnly))
    return std::nullopt;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject())
    return std::nullopt;
  const QJsonObject o = doc.object();
  RunRecord r;
  r.tool = o.value(QStringLiteral("tool")).toString();
  r.recipe = o.value(QStringLiteral("recipe")).toString();
  r.caseId = o.value(QStringLiteral("case")).toString();
  r.station = o.value(QStringLiteral("station")).toString();
  r.revision = o.value(QStringLiteral("revision")).toInt();
  r.caseSignature = o.value(QStringLiteral("case_signature")).toString();
  r.catalogSha = o.value(QStringLiteral("catalog_sha256")).toString();
  r.upstream = o.value(QStringLiteral("upstream")).toString();
  r.waveStamp = o.value(QStringLiteral("waveforms")).toString();
  for (const QJsonValue v :
       o.value(QStringLiteral("waveform_files")).toArray())
    r.waveFiles << v.toString();
  r.waveEstimated = o.value(QStringLiteral("waveforms_estimated")).toBool();
  r.appVersion = o.value(QStringLiteral("app_version")).toString();
  r.started = QDateTime::fromString(
      o.value(QStringLiteral("started")).toString(), Qt::ISODate);
  r.finished = QDateTime::fromString(
      o.value(QStringLiteral("finished")).toString(), Qt::ISODate);
  r.ok = o.value(QStringLiteral("ok")).toBool();
  r.error = o.value(QStringLiteral("error")).toString();
  r.errorReason = o.value(QStringLiteral("error_reason")).toString();
  for (const QJsonValue v : o.value(QStringLiteral("warnings")).toArray())
    r.warnings << v.toString();
  r.calibrationKey = o.value(QStringLiteral("calibration_key")).toString();
  r.calibrationReused =
      o.value(QStringLiteral("calibration_reused")).toBool();
  r.settings = o.value(QStringLiteral("settings")).toObject().toVariantMap();
  r.inputs = o.value(QStringLiteral("inputs")).toObject().toVariantMap();
  r.fitted = o.value(QStringLiteral("fitted")).toObject().toVariantMap();
  r.outputs = o.value(QStringLiteral("outputs")).toObject().toVariantMap();
  return r;
}

const analysis::Event *
eventOfCase(const Case &c, const std::vector<analysis::Event> &events) {
  if (c.reference || std::isnan(c.magnitude))
    return nullptr;
  const double t = double(c.time.toSecsSinceEpoch());
  const analysis::Event *best = nullptr;
  for (const analysis::Event &e : events) {
    if (std::abs(e.epoch - t) > 15 * 3600.0 || std::abs(e.lat - c.lat) > 1e-3 ||
        std::abs(e.lon - c.lon) > 1e-3 || std::abs(e.mag - c.magnitude) > 0.051)
      continue;
    if (!best || std::abs(e.epoch - t) < std::abs(best->epoch - t))
      best = &e;
  }
  if (best)
    return best;
  // A case picked from another catalog than the one in use (the built-in
  // list, then a denser catalog of another agency) differs a little in time,
  // place and magnitude. The same earthquake is then the largest event within
  // two minutes and about a degree of the case.
  for (const analysis::Event &e : events) {
    if (std::abs(e.epoch - t) > 120.0 || std::abs(e.lat - c.lat) > 1.0 ||
        std::abs(e.lon - c.lon) > 1.0 || std::abs(e.mag - c.magnitude) > 1.0)
      continue;
    if (!best || e.mag > best->mag)
      best = &e;
  }
  return best;
}

Cell cellOf(const Study &s, const Case &c, const QString &station,
            const Recipe &r) {
  Cell cell;
  if (perStation(r.tool) && station.isEmpty()) {
    cell.state = CellState::Blocked;
    cell.why = RunText::tr("the case has no stations: tick them on the Cases "
                           "page");
    cell.fix = Fix::Cases;
    return cell;
  }
  cell.dir = runDir(s, c, station, r);
  const QString sig =
      runSignature(s, c, perStation(r.tool) ? station : QString(), r.tool);

  // What a run would need that is not there.
  QString blocked, upstream, waveNow;
  WaveChoice waveChoice;
  Fix fix = Fix::None;
  bool studyCatalog = false; // whether the run reads the study's catalog
  if (r.tool == kToolIndicators) {
    studyCatalog = true;
    blocked = catalogProblem(s);
    if (!blocked.isEmpty()) {
      fix = Fix::Cases;
    } else {
      blocked = indicatorsCatalogProblem(s, r);
      if (!blocked.isEmpty())
        fix = Fix::Recipe;
    }
  } else if (r.tool == kToolFeatures) {
    studyCatalog = calibrationOverride(r).isEmpty();
    const StationRef *st = stationOf(c, station);
    if (!st || !hasWaveforms(s, *st)) {
      const QString dir = st ? s.dataDirFor(*st) : QString();
      blocked =
          dir.isEmpty()
              ? RunText::tr("no waveforms for %1: add a waveform folder on "
                            "the Cases page, or get them on the Data page")
                    .arg(station)
              : RunText::tr("no waveforms for %1: %2 does not exist and no "
                            "scanned waveform folder holds the station (the "
                            "folders are set on the Cases page)")
                    .arg(station, QDir::toNativeSeparators(dir));
      fix = Fix::Cases;
    } else {
      if (studyCatalog)
        blocked = catalogProblem(s);
      if (!blocked.isEmpty()) {
        fix = Fix::Cases;
      } else {
        // The files a run would read now, to tell a finished run that read
        // other ones.
        waveChoice = chooseWaveFiles(s, *WaveIndex::shared(s.dir), *st,
                                     c.spanStart(), c.time);
        waveNow = stampOfFiles(waveChoice.files);
        // The station has data, but none of it in this case's span (it
        // started recording after the earthquake, say): a run would fail at
        // once, so it is not offered. With only estimates in the index the
        // choice above errs on the side of taking a file, so this errs on
        // the side of letting the run start.
        if (waveChoice.files.isEmpty()) {
          blocked = noSpanData(s, c, *st);
          fix = Fix::Cases;
        }
      }
    }
  } else if (r.tool == kToolClassification) {
    featureTableFor(s, c, station, r, &blocked, &fix, &upstream);
  }

  cell.record = RunRecord::read(cell.dir);
  if (cell.record) {
    const RunRecord &rec = *cell.record;
    QString stale;
    if (!rec.caseSignature.startsWith(r.tool + QLatin1Char('|'))) {
      // Written before each tool had its own signature.
      stale = RunText::tr("recorded with an earlier version of the "
                          "application, which noted less about a run: run it "
                          "again");
    } else if (rec.revision != r.revision) {
      stale = RunText::tr("the recipe was edited after this run (revision "
                          "%1, now %2)")
                  .arg(rec.revision)
                  .arg(r.revision);
    } else if (rec.caseSignature != sig) {
      stale = RunText::tr("the case, its station or the catalog's time "
                          "offset was changed after this run");
    } else if (const QString now =
                   studyCatalog && !rec.catalogSha.isEmpty()
                       ? sha256Cached(s.catalogPath())
                       : QString();
               !now.isEmpty() && now != rec.catalogSha) {
      // (A catalog that cannot be read now proves nothing either way.)
      stale = RunText::tr("the catalog file has changed since this run");
    } else if (r.tool == kToolFeatures && !rec.waveStamp.isEmpty() &&
               !waveNow.isEmpty() && rec.waveStamp != waveNow &&
               filesChanged(rec, waveChoice)) {
      stale = RunText::tr("the waveform files for the case's span have "
                          "changed since this run (new or replaced files)");
    } else if (r.tool == kToolClassification && rec.ok) {
      if (!blocked.isEmpty())
        stale = RunText::tr("the features it was trained on are no longer "
                            "current");
      else if (!rec.upstream.isEmpty() && rec.upstream != upstream)
        stale = RunText::tr("its features were run again after this run");
    }
    if (!stale.isEmpty()) {
      cell.state = CellState::Stale;
      cell.why = stale;
    } else if (rec.ok) {
      cell.state = CellState::Done;
    } else {
      cell.state = CellState::Failed; // a failure kept in the folder itself
      cell.why = rec.error;
    }
    const QString log = QDir(cell.dir).filePath(kRunLogName);
    if (QFileInfo::exists(log))
      cell.log = log;
  }
  // The last attempt, when it failed: kept beside the folder.
  const QString failedDir = runFailedDir(cell.dir);
  QString failedReason;
  if (const std::optional<RunRecord> failed = RunRecord::read(failedDir);
      failed && failed->revision == r.revision && failed->caseSignature == sig &&
      (!cell.record || failed->finished >= cell.record->finished)) {
    failedReason = failed->errorReason;
    const QString log = QDir(failedDir).filePath(kRunLogName);
    if (QFileInfo::exists(log))
      cell.log = log;
    if (cell.state == CellState::Done) {
      cell.note = RunText::tr("a later run failed and was set aside: %1")
                      .arg(failed->error);
    } else {
      cell.state = CellState::Failed;
      cell.why = failed->error;
    }
  }
  if (cell.state == CellState::Done) {
    // What the finished run was made under, as it noted itself.
    cell.warnings = cell.record->warnings;
    return cell;
  }
  if (!blocked.isEmpty()) {
    // What a run needs is not there: that is the thing to say, also of a
    // cell whose last run failed (for that very reason, as a rule). Its log
    // stays at hand.
    cell.state = CellState::Blocked;
    cell.why = blocked;
    cell.fix = fix;
    return cell;
  }
  if (cell.state == CellState::Failed) {
    // A failure with a known remedy points to where it is applied.
    const QString reason = cell.record && !cell.record->ok
                               ? cell.record->errorReason
                               : failedReason;
    if (reason == QLatin1String("events"))
      cell.fix = Fix::Recipe;
    else if (!reason.isEmpty())
      cell.fix = Fix::Cases;
  }
  cell.warnings = runWarnings(s, c, station, r);
  return cell;
}

bool SpanData::thin() const {
  return known && !atLeast && spanDays > 0 && days > 0 &&
         double(days) < defaults::kMinSpanCoverage * double(spanDays);
}

int spanCalendarDays(const QDateTime &from, const QDateTime &to) {
  if (!from.isValid() || !to.isValid() || to <= from)
    return 0;
  // The last instant of the period is just before `to`.
  const QDate first = from.toUTC().date();
  const QDate last = to.toUTC().addMSecs(-1).date();
  return int(first.daysTo(last)) + 1;
}

SpanData spanData(const WaveIndex &index, const QString &station,
                  const QDateTime &from, const QDateTime &to) {
  SpanData d;
  d.spanDays = spanCalendarDays(from, to);
  for (const WaveStation &ws : index.stations()) {
    if (ws.code.compare(station, Qt::CaseInsensitive) != 0)
      continue;
    d.known = true;
    // `last` is where the data ends: a station whose data stops at midnight
    // has its last day the day before.
    const QDate first = utcOf(ws.first).date(),
                last = utcOf(std::max(ws.first, ws.last - 1.0)).date();
    if (!d.first.isValid() || first < d.first)
      d.first = first;
    if (!d.last.isValid() || last > d.last)
      d.last = last;
  }
  if (d.known)
    d.days = index.daysCovered(station, from, to, &d.approximate, &d.atLeast);
  return d;
}

QStringList runWarnings(const Study &s, const Case &c, const QString &station,
                        const Recipe &r) {
  QStringList out;
  if (r.tool == kToolClassification) {
    // The scores stand on the features: what those were made under is said
    // of the scores too.
    const Recipe *fr = s.findRecipe(strOf(r, "features_recipe"));
    if (!fr)
      return out;
    const std::optional<RunRecord> rec =
        RunRecord::read(runDir(s, c, station, *fr));
    if (rec && rec->ok)
      for (const QString &w : rec->warnings)
        out << RunText::tr("its features: %1").arg(w);
    return out;
  }
  const bool studyCatalog =
      r.tool == kToolIndicators || calibrationOverride(r).isEmpty();
  const CatalogSummary cs =
      studyCatalog && !s.catalog.isEmpty()
          ? catalogSummary(s.catalogPath(), s.catalogUtcOffsetHours)
          : CatalogSummary();
  if (r.tool == kToolIndicators && cs.ok) {
    const double mc = numOf(r, "mc", 1.3);
    if (boolOf(r, "estimate_mc", false)) {
      if (cs.floor > defaults::kSparseCatalogMag)
        out << RunText::tr("the catalog lists earthquakes from about "
                           "magnitude %1 up (%2 events): the completeness "
                           "magnitude will be estimated near that, and the "
                           "indicators describe the large earthquakes only")
                   .arg(cs.floor, 0, 'f', 1)
                   .arg(cs.events);
    } else if (cs.floor > mc) {
      out << RunText::tr("the catalog thins out below about magnitude %1, "
                         "above the recipe's completeness magnitude %2: the "
                         "b-value is computed on a range the catalog does "
                         "not fill")
                 .arg(cs.floor, 0, 'f', 1)
                 .arg(mc, 0, 'f', 1);
    }
  }
  if (r.tool == kToolFeatures) {
    if (cs.ok && cs.floor > defaults::kSparseCatalogMag)
      out << RunText::tr("the catalog lists earthquakes from about magnitude "
                         "%1 up (%2 events): the quiet windows of the "
                         "calibration only avoid earthquakes of that size "
                         "and larger; smaller local ones are not excluded")
                 .arg(cs.floor, 0, 'f', 1)
                 .arg(cs.events);
    const SpanData d =
        spanData(*WaveIndex::shared(s.dir), station, c.spanStart(), c.time);
    if (d.thin())
      out << (d.approximate
                  ? RunText::tr("%1 has waveforms on about %2 of the %3 days "
                                "of the span")
                  : RunText::tr("%1 has waveforms on only %2 of the %3 days "
                                "of the span"))
                 .arg(station)
                 .arg(d.days)
                 .arg(d.spanDays);
  }
  return out;
}

int runsMadeStale(const Study &before, const Study &after) {
  int n = 0;
  for (const Case &c : before.cases) {
    const Case *ac = after.findCase(c.id);
    for (const Recipe &r : before.recipes) {
      const Recipe *ar = after.findRecipe(r.id);
      QStringList stations;
      if (perStation(r.tool))
        for (const StationRef &st : c.stations)
          stations << st.code;
      else
        stations << QString();
      for (const QString &station : stations) {
        if (cellOf(before, c, station, r).state != CellState::Done)
          continue;
        if (!ac || !ar ||
            cellOf(after, *ac, station, *ar).state != CellState::Done)
          ++n;
      }
    }
  }
  return n;
}

bool runCell(const Study &s, const Case &c, const QString &station,
             const Recipe &r, const QString &appVersion,
             const RunHooks &hooks) {
  const QString dir = runDir(s, c, station, r);
  const QString tmp = runTmpDir(dir), failedDir = runFailedDir(dir);
  // The run works beside the folder; what an earlier, unfinished run left
  // there is cleared first.
  QDir(tmp).removeRecursively();
  if (!QDir().mkpath(tmp))
    fail(RunText::tr("cannot create %1").arg(QDir::toNativeSeparators(tmp)));

  RunRecord rec;
  rec.tool = r.tool;
  rec.recipe = r.id;
  rec.caseId = c.id;
  rec.station = perStation(r.tool) ? station : QString();
  rec.revision = r.revision;
  rec.caseSignature = runSignature(s, c, rec.station, r.tool);
  rec.appVersion = appVersion;
  rec.settings = r.settings;
  rec.started = QDateTime::currentDateTimeUtc();

  // Everything the tool says also goes to run.log, from whichever thread.
  QFile logFile(QDir(tmp).filePath(kRunLogName));
  if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text) && hooks.log)
    hooks.log(RunText::tr("warning: the run log cannot be written: %1")
                  .arg(logFile.errorString()));
  QMutex logMutex;
  const auto toFile = [&logFile, &logMutex](const QString &line) {
    const QMutexLocker lock(&logMutex);
    if (!logFile.isOpen())
      return;
    logFile.write(line.toUtf8());
    logFile.write("\n");
    logFile.flush();
  };
  RunHooks h = hooks;
  h.log = [&hooks, &toFile](const QString &line) {
    toFile(line);
    if (hooks.log)
      hooks.log(line);
  };
  toFile(QStringLiteral("%1 %2 (revision %3) on %4%5, started %6, version %7")
             .arg(r.tool, r.id)
             .arg(r.revision)
             .arg(c.id, rec.station.isEmpty()
                            ? QString()
                            : QLatin1Char(' ') + rec.station,
                  iso(rec.started), appVersion));
  // What the run is made under goes into its log and its record, so it is
  // still said when the results are read later.
  rec.warnings = runWarnings(s, c, rec.station, r);
  for (const QString &w : rec.warnings)
    h.log(RunText::tr("warning: %1").arg(w));
  try {
    if (r.tool == kToolIndicators)
      runIndicators(s, c, r, tmp, h, rec);
    else if (r.tool == kToolFeatures)
      runFeatures(s, c, station, r, tmp, h, rec);
    else if (r.tool == kToolClassification)
      runClassification(s, c, station, r, tmp, h, rec);
    else
      fail(RunText::tr("unknown tool %1").arg(r.tool));
  } catch (const std::exception &e) {
    // A failed run is set aside with its files, log and reason; the
    // finished run in the folder, if there is one, stays.
    rec.finished = QDateTime::currentDateTimeUtc();
    rec.ok = false;
    rec.error = QString::fromUtf8(e.what());
    if (const auto *cal = dynamic_cast<const fx::CalibrationError *>(&e))
      rec.errorReason = fx::CalibrationError::code(cal->reason());
    toFile(QStringLiteral("error: %1").arg(rec.error));
    logFile.close();
    rec.write(tmp);
    QDir(failedDir).removeRecursively();
    QDir().rename(tmp, failedDir);
    throw;
  }
  if (hooks.shouldStop && hooks.shouldStop()) {
    // Stopped: nothing is kept, the cell stays as it was.
    logFile.close();
    QDir(tmp).removeRecursively();
    return false;
  }
  rec.finished = QDateTime::currentDateTimeUtc();
  rec.ok = true;
  toFile(QStringLiteral("finished %1").arg(iso(rec.finished)));
  logFile.close();
  if (!rec.write(tmp))
    fail(RunText::tr("cannot write %1")
             .arg(QDir::toNativeSeparators(
                 QDir(tmp).filePath(RunRecord::kFileName))));

  // The finished run takes the folder's place.
  const QString old = dir + QStringLiteral(".old");
  QDir(old).removeRecursively();
  const bool had = QDir(dir).exists();
  if (had && !QDir().rename(dir, old))
    fail(RunText::tr("cannot replace %1 with the new run, which is kept in "
                     "%2 (is a file of the folder open?)")
             .arg(QDir::toNativeSeparators(dir), QDir::toNativeSeparators(tmp)));
  if (!QDir().rename(tmp, dir)) {
    if (had)
      QDir().rename(old, dir);
    fail(RunText::tr("cannot move the finished run from %1 into place")
             .arg(QDir::toNativeSeparators(tmp)));
  }
  QDir(old).removeRecursively();
  QDir(failedDir).removeRecursively();
  return true;
}

QString sha256Of(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash h(QCryptographicHash::Sha256);
  h.addData(&f);
  return QString::fromLatin1(h.result().toHex());
}

QString sha256Cached(const QString &path) {
  struct Entry {
    qint64 size = 0, mtime = 0;
    QString sha;
  };
  static QMutex mutex;
  static QHash<QString, Entry> cache;
  const QFileInfo fi(path);
  if (!fi.isFile())
    return {};
  const qint64 size = fi.size();
  const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
  {
    const QMutexLocker lock(&mutex);
    const auto it = cache.constFind(path);
    if (it != cache.constEnd() && it->size == size && it->mtime == mtime)
      return it->sha;
  }
  const QString sha = sha256Of(path);
  const QMutexLocker lock(&mutex);
  cache.insert(path, Entry{size, mtime, sha});
  return sha;
}

} // namespace skg::study
