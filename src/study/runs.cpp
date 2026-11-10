#include "study/runs.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSaveFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "analysis/events.h"
#include "analysis/label.h"
#include "cbi/magnitudes.h"
#include "cbi/run.h"
#include "fx/config.h"
#include "fx/engine.h"
#include "sfc/dataset.h"
#include "sfc/experiments.h"
#include "sfc/run.h"
#include "sfc/settings.h"

namespace skg::study {

const QString kToolIndicators = QStringLiteral("indicators");
const QString kToolFeatures = QStringLiteral("features");
const QString kToolClassification = QStringLiteral("classification");
const QString RunRecord::kFileName = QStringLiteral("run.json");

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

// The events the case's indicators are computed on, as an AFAD-layout file:
// from one window before the span up to, not including, the event.
int writeSpanCatalog(const Study &s, const Case &c, int windowMonths,
                     const QString &path) {
  const std::vector<analysis::Event> events =
      analysis::loadEvents(s.catalogPath(), s.catalogUtcOffsetHours);
  const double t1 = double(c.time.toSecsSinceEpoch());
  const double t0 =
      double(c.spanStart().addMonths(-windowMonths).toSecsSinceEpoch());
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

void runIndicators(const Study &s, const Case &c, const Recipe &r,
                   const QString &dir, const RunHooks &hooks, RunRecord &rec) {
  if (s.catalog.isEmpty())
    fail(QStringLiteral("the study has no earthquake catalog"));
  const int windowMonths = std::max(1, intOf(r, "window_months", 12));
  const QString spanCsv = QDir(dir).filePath(QStringLiteral("catalog.csv"));
  const int n = writeSpanCatalog(s, c, windowMonths, spanCsv);
  if (hooks.log)
    hooks.log(
        QStringLiteral("%1 catalog events from %2 to %3")
            .arg(n)
            .arg(iso(c.spanStart().addMonths(-windowMonths)), iso(c.time)));
  if (n == 0)
    fail(QStringLiteral("the catalog has no events in the case's span"));

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
  o.dpi = intOf(r, "figure_dpi", 100);
  o.lang = strOf(r, "figure_language", QStringLiteral("en"));
  o.outRoot = dir;

  const cbi::Catalog selected =
      o.selection.apply(cbi::loadCatalog(spanCsv), nullptr);
  if (boolOf(r, "estimate_mc", false)) {
    if (selected.size() < 50)
      fail(QStringLiteral("only %1 events in the zone: too few to estimate "
                          "the completeness magnitude")
               .arg(selected.size()));
    o.config.mc = cbi::maxCurvatureMc(selected.magnitudes(), o.config.magBin);
    rec.fitted.insert(QStringLiteral("mc"), o.config.mc);
    if (hooks.log)
      hooks.log(QStringLiteral("completeness magnitude estimated: %1")
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
  rec.inputs.insert(s.catalogPath(), sha256Of(s.catalogPath()));
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
    fail(QStringLiteral("the case has no station %1").arg(station));
  const QString dataDir = s.dataDirFor(*st);
  if (dataDir.isEmpty() || !QDir(dataDir).exists())
    fail(QStringLiteral("no waveform folder for %1").arg(station));
  fx::Config cfg;
  const QString json = strOf(r, "config_json");
  if (!json.trimmed().isEmpty()) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (!doc.isObject())
      fail(QStringLiteral("the recipe's config is not valid JSON: %1")
               .arg(err.errorString()));
    cfg = fx::Config::fromJson(doc.object());
  }
  cfg.station = station;
  cfg.dataDir = dataDir;
  cfg.outputRoot = dir;
  cfg.calibrationFile.clear(); // every case calibrates for itself
  if (cfg.calibration.catalog.isEmpty())
    cfg.calibration.catalog = s.catalogPath();
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
      fail(QStringLiteral("the feature run wrote no table"));
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
  rec.inputs.insert(s.catalogPath(), sha256Of(s.catalogPath()));
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
    fail(QStringLiteral("\"%1\" is not a range of days such as 31-90")
             .arg(text));
  return {first, last};
}

// The feature table of the features recipe this recipe builds on.
QString featureTableFor(const Study &s, const Case &c, const QString &station,
                        const Recipe &r, QString *why) {
  const QString fid = strOf(r, "features_recipe");
  const Recipe *fr = s.findRecipe(fid);
  if (!fr || fr->tool != kToolFeatures) {
    *why = QStringLiteral("the recipe names no features recipe");
    return {};
  }
  const Cell cell = cellOf(s, c, station, *fr);
  if (cell.state != CellState::Done || !cell.record) {
    *why = QStringLiteral("needs the features of %1 first").arg(fr->name);
    return {};
  }
  const QString band = strOf(r, "band");
  const QString name = band.isEmpty() ? QStringLiteral("features")
                                      : QStringLiteral("features_") + band;
  const QString rel = cell.record->outputs.value(name).toString();
  if (rel.isEmpty()) {
    *why = QStringLiteral("the features run has no table %1").arg(name);
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
    fail(QStringLiteral("the recipe needs at least two classes"));
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
  const QString tablePath = featureTableFor(s, c, station, r, &why);
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
    fail(QStringLiteral("no window is left to train on (%1 dropped for "
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
      fail(QStringLiteral("step %1 failed: %2").arg(o.step, o.error));
  if (!QFileInfo::exists(experiments))
    fail(QStringLiteral("the run trained no model"));
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
                  {QStringLiteral("figure_dpi"), 100},
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

QString caseSignature(const Case &c, const QString &station) {
  QString sig =
      QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
          .arg(iso(c.time))
          .arg(c.lat, 0, 'f', 5)
          .arg(c.lon, 0, 'f', 5)
          .arg(c.spanDays)
          .arg(c.zone.kind == Zone::Kind::Fault ? QStringLiteral("f")
                                                : QStringLiteral("c"))
          .arg(c.zone.kind == Zone::Kind::Fault ? c.zone.bufferKm
                                                : c.zone.radiusKm,
               0, 'f', 2)
          .arg(c.zone.faultGeoJson, c.zone.strands.join(QLatin1Char(',')));
  if (const StationRef *st = stationOf(c, station))
    sig += QStringLiteral("|%1|%2|%3|%4")
               .arg(st->code)
               .arg(st->lat, 0, 'f', 5)
               .arg(st->lon, 0, 'f', 5)
               .arg(st->dataDir);
  return sig;
}

bool RunRecord::write(const QString &dir) const {
  QJsonObject o;
  o.insert(QStringLiteral("tool"), tool);
  o.insert(QStringLiteral("recipe"), recipe);
  o.insert(QStringLiteral("case"), caseId);
  o.insert(QStringLiteral("station"), station);
  o.insert(QStringLiteral("revision"), revision);
  o.insert(QStringLiteral("case_signature"), caseSignature);
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

Cell cellOf(const Study &s, const Case &c, const QString &station,
            const Recipe &r) {
  Cell cell;
  if (perStation(r.tool) && station.isEmpty()) {
    cell.state = CellState::NotApplicable;
    return cell;
  }
  cell.dir = runDir(s, c, station, r);
  cell.record = RunRecord::read(cell.dir);
  if (cell.record) {
    const bool current =
        cell.record->revision == r.revision &&
        cell.record->caseSignature ==
            caseSignature(c, perStation(r.tool) ? station : QString());
    if (!current) {
      cell.state = CellState::Stale;
    } else if (cell.record->ok) {
      cell.state = CellState::Done;
      return cell;
    } else {
      cell.state = CellState::Failed;
      cell.why = cell.record->error;
    }
  }
  // What a run would need that is not there yet.
  QString blocked;
  if (r.tool == kToolIndicators) {
    if (s.catalog.isEmpty())
      blocked = QStringLiteral("the study has no earthquake catalog");
  } else if (r.tool == kToolFeatures) {
    const StationRef *st = stationOf(c, station);
    const QString dir = st ? s.dataDirFor(*st) : QString();
    if (dir.isEmpty() || !QDir(dir).exists())
      blocked = QStringLiteral("no waveform folder for %1").arg(station);
  } else if (r.tool == kToolClassification) {
    QString why;
    if (featureTableFor(s, c, station, r, &why).isEmpty())
      blocked = why;
  }
  if (!blocked.isEmpty() && cell.state != CellState::Failed) {
    cell.state = CellState::Blocked;
    cell.why = blocked;
  }
  return cell;
}

bool runCell(const Study &s, const Case &c, const QString &station,
             const Recipe &r, const QString &appVersion,
             const RunHooks &hooks) {
  const QString dir = runDir(s, c, station, r);
  if (!QDir().mkpath(dir))
    fail(QStringLiteral("cannot create %1").arg(dir));
  RunRecord rec;
  rec.tool = r.tool;
  rec.recipe = r.id;
  rec.caseId = c.id;
  rec.station = perStation(r.tool) ? station : QString();
  rec.revision = r.revision;
  rec.caseSignature = caseSignature(c, rec.station);
  rec.appVersion = appVersion;
  rec.settings = r.settings;
  rec.started = QDateTime::currentDateTimeUtc();
  try {
    if (r.tool == kToolIndicators)
      runIndicators(s, c, r, dir, hooks, rec);
    else if (r.tool == kToolFeatures)
      runFeatures(s, c, station, r, dir, hooks, rec);
    else if (r.tool == kToolClassification)
      runClassification(s, c, station, r, dir, hooks, rec);
    else
      fail(QStringLiteral("unknown tool %1").arg(r.tool));
  } catch (const std::exception &e) {
    rec.finished = QDateTime::currentDateTimeUtc();
    rec.ok = false;
    rec.error = QString::fromUtf8(e.what());
    rec.write(dir);
    throw;
  }
  if (hooks.shouldStop && hooks.shouldStop())
    return false; // stopped: no record, the cell stays as it was
  rec.finished = QDateTime::currentDateTimeUtc();
  rec.ok = true;
  if (!rec.write(dir))
    fail(QStringLiteral("cannot write %1/run.json").arg(dir));
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

} // namespace skg::study
