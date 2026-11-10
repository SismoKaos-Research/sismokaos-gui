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
#include "fx/config.h"
#include "fx/engine.h"
#include "sfc/dataset.h"
#include "sfc/experiments.h"
#include "sfc/run.h"
#include "sfc/settings.h"
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
    return RunText::tr("the study has no earthquake catalog: choose one on "
                       "the Cases page");
  if (!QFileInfo::exists(s.catalogPath()))
    return RunText::tr("catalog not found: %1 (choose it again on the Cases "
                       "page)")
        .arg(QDir::toNativeSeparators(s.catalogPath()));
  return {};
}

// The waveform files a features run reads, as one short text: their names,
// sizes and times, not their contents and not where the folder is. New or
// replaced files for the span change it; a file elsewhere, or the same
// folder on another drive, does not.
QString stampOfFiles(const QStringList &files) {
  if (files.isEmpty())
    return {};
  QStringList lines;
  for (const QString &f : files) {
    const QFileInfo fi(f);
    lines << QStringLiteral("%1|%2|%3")
                 .arg(fi.fileName())
                 .arg(fi.size())
                 .arg(fi.lastModified().toSecsSinceEpoch());
  }
  lines.sort();
  return QStringLiteral("%1 files, %2")
      .arg(files.size())
      .arg(QString::fromLatin1(
          QCryptographicHash::hash(lines.join(QLatin1Char('\n')).toUtf8(),
                                   QCryptographicHash::Sha1)
              .toHex()
              .left(16)));
}

QDateTime utcOf(double epoch) {
  return QDateTime::fromSecsSinceEpoch(qint64(std::floor(epoch)),
                                       QTimeZone::UTC);
}

void runIndicators(const Study &s, const Case &c, const Recipe &r,
                   const QString &dir, const RunHooks &hooks, RunRecord &rec) {
  const QString problem = catalogProblem(s);
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

void runFeatures(const Study &s, const Case &c, const QString &station,
                 const Recipe &r, const QString &dir, const RunHooks &hooks,
                 RunRecord &rec) {
  const StationRef *st = stationOf(c, station);
  if (!st)
    fail(RunText::tr("the case has no station %1").arg(station));
  // Only the files that overlap the case's span are read (waveindex.h).
  QString chosenHow;
  const QStringList inputFiles =
      waveFilesFor(s, *WaveIndex::shared(s.dir), *st, c.spanStart(), c.time,
                   &chosenHow);
  if (inputFiles.isEmpty())
    fail(RunText::tr("no waveform files for %1 in the span %2 to %3 (%4): "
                     "add or scan its waveform folder on the Cases page, or "
                     "get the waveforms on the Data page")
             .arg(station, c.spanStart().toString(Qt::ISODate),
                  c.time.toString(Qt::ISODate), chosenHow));
  const QString dataDir = QFileInfo(inputFiles.first()).absolutePath();
  if (hooks.log)
    hooks.log(RunText::tr(" Waveforms from %1").arg(chosenHow));
  rec.waveStamp = stampOfFiles(inputFiles);
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
  if (cfg.calibration.catalog.isEmpty()) {
    const QString problem = catalogProblem(s);
    if (!problem.isEmpty())
      fail(problem);
    rec.catalogSha = sha256Cached(s.catalogPath());
    rec.inputs.insert(s.catalogPath(), rec.catalogSha);
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
    rec.inputs.insert(cfg.calibration.catalog,
                      sha256Cached(cfg.calibration.catalog));
  }
  cfg.calibration.stationLat = st->lat;
  cfg.calibration.stationLon = st->lon;
  const QStringList problems = cfg.validate();
  if (!problems.isEmpty())
    fail(problems.join(QStringLiteral("; ")));
  const QList<fx::Config> runs = cfg.bandRuns();
  for (int i = 0; i < int(runs.size()); ++i) {
    const fx::Config &run = runs[i];
    fx::RunHooks h;
    h.log = hooks.log;
    h.shouldStop = hooks.shouldStop;
    h.phase = [&hooks](const QString &p) {
      if (hooks.log)
        hooks.log(QStringLiteral(" %1").arg(p));
    };
    h.progress = [&hooks, i, &runs](int done, int total) {
      if (hooks.progress && total > 0)
        hooks.progress((i + double(done) / total) / double(runs.size()),
                       QStringLiteral("%1/%2").arg(done).arg(total));
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
  o.insert(QStringLiteral("app_version"), appVersion);
  o.insert(QStringLiteral("started"), iso(started));
  o.insert(QStringLiteral("finished"), iso(finished));
  o.insert(QStringLiteral("ok"), ok);
  o.insert(QStringLiteral("error"), error);
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
  r.appVersion = o.value(QStringLiteral("app_version")).toString();
  r.started = QDateTime::fromString(
      o.value(QStringLiteral("started")).toString(), Qt::ISODate);
  r.finished = QDateTime::fromString(
      o.value(QStringLiteral("finished")).toString(), Qt::ISODate);
  r.ok = o.value(QStringLiteral("ok")).toBool();
  r.error = o.value(QStringLiteral("error")).toString();
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
  Fix fix = Fix::None;
  bool studyCatalog = false; // whether the run reads the study's catalog
  if (r.tool == kToolIndicators) {
    studyCatalog = true;
    blocked = catalogProblem(s);
    if (!blocked.isEmpty())
      fix = Fix::Cases;
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
        waveNow = stampOfFiles(waveFilesFor(s, *WaveIndex::shared(s.dir), *st,
                                            c.spanStart(), c.time));
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
               !waveNow.isEmpty() && rec.waveStamp != waveNow) {
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
  if (const std::optional<RunRecord> failed = RunRecord::read(failedDir);
      failed && failed->revision == r.revision && failed->caseSignature == sig &&
      (!cell.record || failed->finished >= cell.record->finished)) {
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
  if (cell.state == CellState::Done)
    return cell;
  if (!blocked.isEmpty() && cell.state != CellState::Failed) {
    cell.state = CellState::Blocked;
    cell.why = blocked;
    cell.fix = fix;
  }
  return cell;
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
