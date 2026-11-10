// Runs and the comparison on a synthetic study: the indicators of two
// earthquakes and a reference date from the synthetic catalog, and a
// classification on fabricated feature tables (no waveforms are processed).
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>

#include "fx/config.h"
#include "fx/npy.h"
#include "study/compare.h"
#include "study/runs.h"
#include "study/study.h"

using namespace skg;
using namespace skg::study;

namespace {

// An fx-style table: one window every 200 s over the `days` whole UTC days
// before `event` and the event day, two feature columns that drift with
// the day (so the classes can be told apart) plus noise.
fx::NpyTable fxTable(const QDateTime &event, int days, quint32 seed) {
  fx::NpyTable t;
  t.fields = {
      fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")},
      fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")},
      fx::NpyField{QStringLiteral("N_RMS"), QStringLiteral("<f8")},
      fx::NpyField{QStringLiteral("N_WOLF_LYE"), QStringLiteral("<f8")}};
  QStringList ids;
  std::vector<double> zaman, rms, wolf;
  QRandomGenerator rng(seed);
  const QDate first = event.date().addDays(-days);
  const double t0 =
      double(QDateTime(first, QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch());
  int n = 0;
  for (int d = 0; d <= days; ++d)
    for (int w = 1; w <= 432; ++w) {
      ids << QStringLiteral("x_w%1").arg(++n);
      zaman.push_back((t0 + d * 86400.0 + w * 200.0) / 60.0);
      rms.push_back(100.0 + d + rng.generateDouble());
      wolf.push_back(0.1 * d + 0.5 * rng.generateDouble());
    }
  t.rows = n;
  t.strings = {ids, {}, {}, {}};
  t.numeric = {{}, zaman, rms, wolf};
  return t;
}

Case makeCase(Study &s, const QString &name, const QString &iso, double lat,
              double lon, double mag, bool reference = false) {
  Case c;
  c.name = name;
  c.id = s.freeCaseId(name);
  c.reference = reference;
  c.time = QDateTime::fromString(iso, Qt::ISODate).toUTC();
  c.lat = lat;
  c.lon = lon;
  if (!reference) {
    c.depthKm = 10;
    c.magnitude = mag;
  }
  c.spanDays = 270;
  c.zone.radiusKm = 150;
  StationRef st;
  st.code = QStringLiteral("SYN1");
  st.lat = lat + 0.2;
  st.lon = lon + 0.1;
  st.distanceKm = 24;
  st.azimuthDeg = 20;
  c.stations << st;
  return c;
}

} // namespace

class TstStudyRuns : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void layoutAndDefaults();
  void indicatorsRunAndGoStale();
  void blockedCellsSayWhy();
  void classificationOnAFabricatedTable();
  void comparisonReadsTheRuns();
  void aFailedRunIsRecorded();
  void eachToolHasItsOwnSignature();
  void aRunReplacesTheFolderOnlyWhenItFinishes();
  void theCatalogOffsetNeverLetsTheEventIn();
  void aChangedOrMissingCatalog();
  void recipeOrderIsExplained();
  void rerunFeaturesMakeTheClassificationStale();
  void featuresOnASmallStation();

private:
  QTemporaryDir dir_;
  Study s_;
  Recipe indicators_, features_, classification_;
};

void TstStudyRuns::initTestCase() {
  s_.dir = dir_.filePath("study");
  s_.name = QStringLiteral("Synthetic");
  QVERIFY(QDir().mkpath(s_.dir));
  QVERIFY(
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(s_.dir).filePath("catalog.csv")));
  s_.catalog = QStringLiteral("catalog.csv");
  s_.cases << makeCase(s_, QStringLiteral("Synthetic A"),
                       QStringLiteral("2019-12-30T22:10:00Z"), 38.4716, 39.3543,
                       5.8);
  s_.cases << makeCase(s_, QStringLiteral("Synthetic B"),
                       QStringLiteral("2020-07-07T07:07:07Z"), 38.5001, 39.4906,
                       6.4);
  s_.cases << makeCase(s_, QStringLiteral("Reference"),
                       QStringLiteral("2021-05-01T00:00:00Z"), 38.4716, 39.3543,
                       0, true);

  indicators_ = defaultRecipe(kToolIndicators);
  indicators_.id = s_.freeRecipeId(indicators_.name);
  indicators_.settings.insert(QStringLiteral("window_months"), 6);
  indicators_.settings.insert(QStringLiteral("ma_windows"), QVariantList{3});
  indicators_.settings.insert(QStringLiteral("figures"), false);
  s_.recipes << indicators_;
  features_ = defaultRecipe(kToolFeatures);
  features_.id = s_.freeRecipeId(features_.name);
  s_.recipes << features_;
  classification_ = defaultRecipe(kToolClassification);
  classification_.id = s_.freeRecipeId(classification_.name);
  classification_.settings.insert(QStringLiteral("features_recipe"),
                                  features_.id);
  classification_.settings.insert(QStringLiteral("classes"),
                                  QVariantList{QStringLiteral("1-10"),
                                               QStringLiteral("11-20"),
                                               QStringLiteral("21-30")});
  classification_.settings.insert(QStringLiteral("days_per_class"), 10);
  classification_.settings.insert(
      QStringLiteral("models"), QVariantList{QStringLiteral("logistic"),
                                             QStringLiteral("random_forest")});
  classification_.settings.insert(QStringLiteral("cv_folds"), 0);
  classification_.settings.insert(QStringLiteral("quick"), true);
  s_.recipes << classification_;
  s_.save();
}

void TstStudyRuns::layoutAndDefaults() {
  QCOMPARE(recipeTools().size(), 3);
  QVERIFY(!perStation(kToolIndicators));
  QVERIFY(perStation(kToolClassification));
  const Case &c = s_.cases[0];
  QCOMPARE(runDir(s_, c, QStringLiteral("SYN1"), indicators_),
           QDir(s_.dir).filePath(
               "synthetic-a/_case/indicators/indicators-12-month-window"));
  QCOMPARE(runDir(s_, c, QStringLiteral("SYN1"), classification_),
           QDir(s_.dir).filePath(
               "synthetic-a/SYN1/classification/days-before-the-event"));
  // The defaults survive the study file.
  const Study again = Study::load(s_.dir);
  QCOMPARE(again.recipes.size(), 3);
  QCOMPARE(again.findRecipe(classification_.id)
               ->settings.value("classes")
               .toList()
               .size(),
           3);
  QCOMPARE(again.findRecipe(indicators_.id)->settings.value("estimate_mc"),
           QVariant(true));
  QVERIFY(runSignature(s_, c, QStringLiteral("SYN1"), kToolFeatures) !=
          runSignature(s_, c, QString(), kToolFeatures));
  QCOMPARE(runTmpDir(QStringLiteral("/a/b")), QStringLiteral("/a/b.tmp"));
  QCOMPARE(runFailedDir(QStringLiteral("/a/b")), QStringLiteral("/a/b.failed"));
}

void TstStudyRuns::indicatorsRunAndGoStale() {
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &l) { log << l; };
  for (const Case &c : s_.cases) {
    QCOMPARE(cellOf(s_, c, QStringLiteral("SYN1"), indicators_).state,
             CellState::Missing);
    QVERIFY(runCell(s_, c, QStringLiteral("SYN1"), indicators_,
                    QStringLiteral("test"), hooks));
    const Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), indicators_);
    QCOMPARE(cell.state, CellState::Done);
    QVERIFY(cell.record.has_value());
    QCOMPARE(cell.record->appVersion, QStringLiteral("test"));
    QVERIFY(cell.record->started <= cell.record->finished);
    const QString table =
        QDir(cell.dir).filePath(cell.record->outputs.value("table").toString());
    QVERIFY2(QFileInfo::exists(table), qPrintable(table));
    // Mc was estimated for this case and recorded.
    const double mc = cell.record->fitted.value("mc").toDouble();
    QVERIFY2(mc > 0.9 && mc < 2.5, qPrintable(QString::number(mc)));
    QVERIFY(cell.record->fitted.value("events_in_zone").toInt() > 100);
    // The catalog's hash is in the record.
    QCOMPARE(cell.record->inputs.value(s_.catalogPath()).toString(),
             sha256Of(s_.catalogPath()));
    QCOMPARE(cell.record->inputs.value(s_.catalogPath()).toString().size(), 64);
    QCOMPARE(cell.record->catalogSha, sha256Cached(s_.catalogPath()));
    // The run's log is kept beside its record, and nothing is left beside
    // the folder.
    QCOMPARE(cell.log, QDir(cell.dir).filePath(kRunLogName));
    QFile runLog(cell.log);
    QVERIFY(runLog.open(QIODevice::ReadOnly));
    const QByteArray logged = runLog.readAll();
    QVERIFY(logged.contains("catalog events from"));
    QVERIFY(logged.contains("finished "));
    QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
    QVERIFY(!QDir(runFailedDir(cell.dir)).exists());
  }
  QVERIFY(!log.isEmpty());
  // The event itself is not among the events used: the span file ends
  // before it.
  QFile span(QDir(runDir(s_, s_.cases[1], QString(), indicators_))
                 .filePath("catalog.csv"));
  QVERIFY(span.open(QIODevice::ReadOnly));
  const QByteArray text = span.readAll();
  QVERIFY(!text.contains("07/07/2020 07:07:07"));
  QVERIFY(text.contains("/06/2020"));
  QVERIFY(!text.contains("/08/2020"));

  // A changed recipe, or a changed case, makes the run stale.
  Recipe edited = indicators_;
  edited.revision += 1;
  Cell stale = cellOf(s_, s_.cases[0], QStringLiteral("SYN1"), edited);
  QCOMPARE(stale.state, CellState::Stale);
  QVERIFY2(stale.why.contains(QStringLiteral("recipe was edited")),
           qPrintable(stale.why));
  Case moved = s_.cases[0];
  moved.spanDays = 100;
  stale = cellOf(s_, moved, QStringLiteral("SYN1"), indicators_);
  QCOMPARE(stale.state, CellState::Stale);
  QVERIFY2(stale.why.contains(QStringLiteral("case")), qPrintable(stale.why));
  QCOMPARE(cellOf(s_, s_.cases[0], QStringLiteral("SYN1"), indicators_).state,
           CellState::Done);
}

void TstStudyRuns::blockedCellsSayWhy() {
  const Case &c = s_.cases[0];
  Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), features_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY(cell.why.contains(QStringLiteral("waveform")));
  // The reason says where it is put right.
  QVERIFY(cell.why.contains(QStringLiteral("Cases page")));
  QCOMPARE(cell.fix, Fix::Cases);
  // A folder that is named but is not there is named in the reason.
  Study withRoot = s_;
  withRoot.dataRoot = dir_.filePath("no-such-waveforms");
  cell = cellOf(withRoot, c, QStringLiteral("SYN1"), features_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY2(cell.why.contains(QStringLiteral("no-such-waveforms")),
           qPrintable(cell.why));
  cell = cellOf(s_, c, QStringLiteral("SYN1"), classification_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY(cell.why.contains(QStringLiteral("features")));
  QCOMPARE(cell.fix, Fix::RunFirst);
  // A case without stations: the per-station tools wait, and say for what.
  cell = cellOf(s_, c, QString(), classification_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY2(cell.why.contains(QStringLiteral("no stations")),
           qPrintable(cell.why));
  QCOMPARE(cell.fix, Fix::Cases);
  QCOMPARE(cellOf(s_, c, QString(), features_).state, CellState::Blocked);
  Study noCatalog = s_;
  noCatalog.catalog.clear();
  noCatalog.dir = dir_.filePath("other");
  cell = cellOf(noCatalog, c, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Blocked);
  QCOMPARE(cell.fix, Fix::Cases);
  QVERIFY(cell.why.contains(QStringLiteral("no earthquake catalog")));
}

void TstStudyRuns::classificationOnAFabricatedTable() {
  // Stand in for the feature runs: a table and its record per case.
  quint32 seed = 1;
  for (const Case &c : s_.cases) {
    const QString dir = runDir(s_, c, QStringLiteral("SYN1"), features_);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(fx::writeNpy(QDir(dir).filePath("SYN1_features.npy"),
                         fxTable(c.time, 40, seed++)));
    RunRecord rec;
    rec.tool = kToolFeatures;
    rec.recipe = features_.id;
    rec.caseId = c.id;
    rec.station = QStringLiteral("SYN1");
    rec.revision = features_.revision;
    rec.caseSignature =
        runSignature(s_, c, QStringLiteral("SYN1"), kToolFeatures);
    rec.ok = true;
    rec.started = rec.finished = QDateTime::currentDateTimeUtc();
    rec.outputs.insert(QStringLiteral("features"),
                       QStringLiteral("SYN1_features.npy"));
    rec.fitted.insert(QStringLiteral("tau"), 5);
    QVERIFY(rec.write(dir));
    QCOMPARE(cellOf(s_, c, QStringLiteral("SYN1"), features_).state,
             CellState::Done);
    QCOMPARE(cellOf(s_, c, QStringLiteral("SYN1"), classification_).state,
             CellState::Missing);
  }
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &l) { log << l; };
  double lastFraction = -1;
  hooks.progress = [&lastFraction](double f, const QString &) {
    lastFraction = f;
  };
  for (const Case &c : s_.cases) {
    QVERIFY(runCell(s_, c, QStringLiteral("SYN1"), classification_,
                    QStringLiteral("test"), hooks));
    const Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), classification_);
    QCOMPARE(cell.state, CellState::Done);
    QVERIFY(QFileInfo::exists(QDir(cell.dir).filePath(
        cell.record->outputs.value("experiments").toString())));
    // Three classes of 10 days, plus the event day up to the event's hour
    // (sfc counts it with the nearest class).
    QCOMPARE(cell.record->fitted.value("days").toInt(), 31);
    QCOMPARE(cell.record->fitted.value("features").toInt(), 2);
    QVERIFY(cell.record->fitted.value("windows").toInt() > 10000);
    // Which features run it read is recorded.
    QCOMPARE(cell.record->upstream,
             cellOf(s_, c, QStringLiteral("SYN1"), features_)
                 .record->finished.toString(Qt::ISODate));
  }
  QVERIFY(lastFraction > 0);
}

void TstStudyRuns::comparisonReadsTheRuns() {
  const QList<ScoreRow> rows = scoreRows(s_, classification_);
  QCOMPARE(rows.size(), 3);
  for (const ScoreRow &r : rows) {
    QCOMPARE(r.state, CellState::Done);
    QCOMPARE(r.station, QStringLiteral("SYN1"));
    QVERIFY(!r.bestModel.isEmpty());
    QCOMPARE(r.baselineModel, QStringLiteral("logistic"));
    QVERIFY(isBaselineModel(r.baselineModel));
    // The fabricated features drift with the day, so the classes separate.
    QVERIFY2(r.testF1 > 0.8 && r.testF1 <= 1.0,
             qPrintable(QString::number(r.testF1)));
    QVERIFY(r.baseF1 > 0.5 && r.baseF1 <= 1.0);
    QVERIFY(r.testAuc > 0.8 && r.testAuc <= 1.0);
    QCOMPARE(r.distanceKm, 24.0);
  }
  QVERIFY(rows[2].reference);
  QVERIFY(std::isnan(rows[2].magnitude));
  const QStringList csv = scoresCsv(rows).split('\n', Qt::SkipEmptyParts);
  QCOMPARE(csv.size(), 4);
  QVERIFY(csv[0].startsWith(QStringLiteral("case;station;kind;magnitude")));
  QVERIFY(csv[1].startsWith(
      QStringLiteral("Synthetic A;SYN1;earthquake;5.8;24.0;")));
  QVERIFY(csv[3].startsWith(QStringLiteral("Reference;SYN1;reference;;24.0;")));

  // A row whose run is missing has no numbers, not zeros.
  Study more = s_;
  more.cases << makeCase(more, QStringLiteral("Not run"),
                         QStringLiteral("2020-01-01T00:00:00Z"), 38.4, 39.3,
                         5.0);
  const QList<ScoreRow> withMissing = scoreRows(more, classification_);
  QCOMPARE(withMissing.size(), 4);
  QVERIFY(withMissing[3].state != CellState::Done);
  QVERIFY(std::isnan(withMissing[3].testF1));
  QVERIFY(withMissing[3].bestModel.isEmpty());
  QVERIFY(scoresCsv(withMissing)
              .contains(QStringLiteral(
                  "Not run;SYN1;earthquake;5.0;24.0;;;;;;;;\n")));

  // Series: indicators per case, on days before the event.
  const QStringList columns = seriesColumns(s_, indicators_);
  QVERIFY(columns.contains(QStringLiteral("b_value")));
  const QList<Series> b = seriesOf(s_, indicators_, QStringLiteral("b_value"));
  QCOMPARE(b.size(), 3);
  for (const Series &series : b) {
    QVERIFY(series.station.isEmpty());
    QVERIFY(series.daysBefore.size() >= 5);
    QCOMPARE(series.daysBefore.size(), series.values.size());
    for (size_t i = 0; i < series.daysBefore.size(); ++i) {
      QVERIFY(series.daysBefore[i] <= 0.0);
      QVERIFY(series.daysBefore[i] >= -271.0);
      if (i > 0)
        QVERIFY(series.daysBefore[i] > series.daysBefore[i - 1]);
    }
  }
  QVERIFY(b[2].reference);
  QVERIFY(
      seriesOf(s_, indicators_, QStringLiteral("no_such_column")).isEmpty());

  // Series: a feature column as daily medians.
  QCOMPARE(
      seriesColumns(s_, features_),
      (QStringList{QStringLiteral("N_RMS"), QStringLiteral("N_WOLF_LYE")}));
  const QList<Series> rms = seriesOf(s_, features_, QStringLiteral("N_RMS"));
  QCOMPARE(rms.size(), 3);
  QCOMPARE(rms[0].station, QStringLiteral("SYN1"));
  QVERIFY(rms[0].daysBefore.size() >= 40);
  // N_RMS is 100 + day + noise in [0, 1): the medians rise by one a day.
  const size_t n = rms[0].values.size();
  QVERIFY(std::abs((rms[0].values[n - 1] - rms[0].values[n - 11]) - 10.0) <
          0.5);

  const QList<FittedRow> fitted = fittedRows(s_, indicators_);
  QCOMPARE(fitted.size(), 3);
  QVERIFY(fitted[0].fitted.contains(QStringLiteral("mc")));
  QCOMPARE(fittedRows(s_, features_)[0].fitted.value("tau").toInt(), 5);
}

void TstStudyRuns::aFailedRunIsRecorded() {
  Recipe bad = classification_;
  bad.id = QStringLiteral("bad");
  bad.settings.insert(QStringLiteral("classes"),
                      QVariantList{QStringLiteral("nonsense")});
  const Case &c = s_.cases[0];
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      runCell(s_, c, QStringLiteral("SYN1"), bad, QStringLiteral("test"), {}));
  const Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), bad);
  QCOMPARE(cell.state, CellState::Failed);
  QVERIFY(cell.why.contains(QStringLiteral("range of days")));
  // The failed attempt is set aside with its record and its log; the run
  // folder itself was never made.
  QVERIFY(!cell.record.has_value());
  QVERIFY(!QDir(cell.dir).exists());
  QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
  const std::optional<RunRecord> failed =
      RunRecord::read(runFailedDir(cell.dir));
  QVERIFY(failed.has_value());
  QVERIFY(!failed->ok);
  QCOMPARE(cell.log, QDir(runFailedDir(cell.dir)).filePath(kRunLogName));
  QFile log(cell.log);
  QVERIFY(log.open(QIODevice::ReadOnly));
  QVERIFY(log.readAll().contains("error: "));
}

void TstStudyRuns::eachToolHasItsOwnSignature() {
  const Case &c = s_.cases[0];
  const QString st = QStringLiteral("SYN1");
  for (const Recipe *r : {&indicators_, &features_, &classification_})
    QCOMPARE(cellOf(s_, c, st, *r).state, CellState::Done);

  // The zone belongs to the indicators only: the hours of feature
  // extraction are not thrown away by a change of radius.
  Study zone = s_;
  zone.cases[0].zone.radiusKm = 90;
  QCOMPARE(cellOf(zone, zone.cases[0], st, indicators_).state,
           CellState::Stale);
  QCOMPARE(cellOf(zone, zone.cases[0], st, features_).state, CellState::Done);
  QCOMPARE(cellOf(zone, zone.cases[0], st, classification_).state,
           CellState::Done);
  QCOMPARE(runsMadeStale(s_, zone), 1);

  // The span belongs to the indicators and the features; the classification
  // follows its features.
  Study span = s_;
  span.cases[0].spanDays = 200;
  QCOMPARE(runSignature(s_, c, st, kToolClassification),
           runSignature(span, span.cases[0], st, kToolClassification));
  QCOMPARE(cellOf(span, span.cases[0], st, indicators_).state,
           CellState::Stale);
  // (stale, and with no waveform folder here it also waits)
  QVERIFY(cellOf(span, span.cases[0], st, features_).state != CellState::Done);
  const Cell cls = cellOf(span, span.cases[0], st, classification_);
  QCOMPARE(cls.state, CellState::Blocked); // stale, and its features first
  QCOMPARE(runsMadeStale(s_, span), 3);

  // The catalog's time offset belongs to the two tools that read it.
  Study offset = s_;
  offset.catalogUtcOffsetHours = 3;
  QVERIFY(runSignature(s_, c, QString(), kToolIndicators) !=
          runSignature(offset, c, QString(), kToolIndicators));
  QVERIFY(runSignature(s_, c, st, kToolFeatures) !=
          runSignature(offset, c, st, kToolFeatures));
  QCOMPARE(cellOf(offset, c, st, indicators_).state, CellState::Stale);
  // Every case: three indicators, three features, three classifications.
  QCOMPARE(runsMadeStale(s_, offset), 9);
  QCOMPARE(runsMadeStale(s_, s_), 0);

  // A removed recipe or case leaves its finished runs behind.
  Study fewer = s_;
  fewer.recipes.removeFirst();
  QCOMPARE(runsMadeStale(s_, fewer), 3);
}

void TstStudyRuns::aRunReplacesTheFolderOnlyWhenItFinishes() {
  const Case &c = s_.cases[0];
  const Cell before = cellOf(s_, c, QString(), indicators_);
  QCOMPARE(before.state, CellState::Done);
  const QString table =
      QDir(before.dir).filePath(before.record->outputs.value("table").toString());
  const QByteArray tableHash = sha256Of(table).toLatin1();
  QVERIFY(!tableHash.isEmpty());

  // Stopped part way: nothing is left, the finished run is as it was.
  RunHooks stop;
  bool stopNow = false;
  stop.log = [&stopNow](const QString &) { stopNow = true; };
  stop.shouldStop = [&stopNow] { return stopNow; };
  QVERIFY(!runCell(s_, c, QString(), indicators_, QStringLiteral("test"), stop));
  Cell cell = cellOf(s_, c, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Done);
  QCOMPARE(cell.record->finished, before.record->finished);
  QCOMPARE(sha256Of(table).toLatin1(), tableHash);
  QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
  QVERIFY(!QDir(runFailedDir(cell.dir)).exists());
  QVERIFY(cell.note.isEmpty());

  // Failed part way: the attempt is set aside, the finished run is as it
  // was and still counts, and the cell says that a later run failed.
  RunHooks breaking;
  breaking.log = [](const QString &line) {
    if (line.contains(QStringLiteral("catalog events")))
      throw std::runtime_error("the disk is full");
  };
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      runCell(s_, c, QString(), indicators_, QStringLiteral("test"), breaking));
  cell = cellOf(s_, c, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Done);
  QCOMPARE(cell.record->finished, before.record->finished);
  QCOMPARE(sha256Of(table).toLatin1(), tableHash);
  QVERIFY2(cell.note.contains(QStringLiteral("the disk is full")),
           qPrintable(cell.note));
  QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
  QVERIFY(QFileInfo::exists(
      QDir(runFailedDir(cell.dir)).filePath(RunRecord::kFileName)));
  QCOMPARE(cell.log, QDir(runFailedDir(cell.dir)).filePath(kRunLogName));
  // The comparison still reads the finished run.
  QCOMPARE(seriesOf(s_, indicators_, QStringLiteral("b_value")).size(), 3);

  // A run that finishes takes the folder's place and clears the failure.
  QTest::qWait(1100); // run.json keeps whole seconds
  QVERIFY(runCell(s_, c, QString(), indicators_, QStringLiteral("test"), {}));
  cell = cellOf(s_, c, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY(cell.record->finished > before.record->finished);
  QVERIFY(cell.note.isEmpty());
  QVERIFY(QFileInfo::exists(table));
  QVERIFY(!QDir(runFailedDir(cell.dir)).exists());
  QVERIFY(!QDir(cell.dir + QStringLiteral(".old")).exists());
}

void TstStudyRuns::theCatalogOffsetNeverLetsTheEventIn() {
  // The case's time was taken from the catalog read as UTC. Read the same
  // catalog three hours off, either way, and the earthquake's own time
  // there differs from the case's; it must still not be one of the events
  // "before" itself.
  for (double offset : {3.0, -3.0}) {
    Study shifted = s_;
    shifted.dir = dir_.filePath(offset > 0 ? "shifted-east" : "shifted-west");
    shifted.catalog = s_.catalogPath();
    shifted.catalogUtcOffsetHours = offset;
    const Case &c = shifted.cases[1]; // 2020-07-07T07:07:07Z, M 6.4
    QStringList log;
    RunHooks hooks;
    hooks.log = [&log](const QString &l) { log << l; };
    QVERIFY(runCell(shifted, c, QString(), indicators_, QStringLiteral("test"),
                    hooks));
    QFile span(QDir(runDir(shifted, c, QString(), indicators_))
                   .filePath("catalog.csv"));
    QVERIFY(span.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = span.readAll().split('\n');
    QVERIFY(lines.size() > 100);
    const QDateTime event = c.time;
    for (const QByteArray &line : lines.mid(1)) {
      if (line.isEmpty())
        continue;
      const QList<QByteArray> cols = line.split(',');
      // Not the earthquake itself (M 6.4 at the case's place) ...
      QVERIFY2(!(cols.value(6) == "6.4" && cols.value(1) == "39.4906"),
               line.constData());
      // ... and nothing later than the case's time.
      const QDateTime t = QDateTime::fromString(
          QString::fromLatin1(cols.value(0)),
          QStringLiteral("dd/MM/yyyy HH:mm:ss"));
      QVERIFY(QDateTime(t.date(), t.time(), QTimeZone::UTC) < event);
    }
    // The disagreement is said in the log.
    QVERIFY2(log.join('\n').contains(QStringLiteral("time offset")),
             qPrintable(log.join('\n')));
  }
}

void TstStudyRuns::aChangedOrMissingCatalog() {
  Study own = s_;
  own.dir = dir_.filePath("own-catalog");
  QVERIFY(QDir().mkpath(own.dir));
  own.catalog = QStringLiteral("catalog.csv");
  QVERIFY(QFile::copy(s_.catalogPath(), own.catalogPath()));
  const Case &done = own.cases[0], &never = own.cases[1];
  QVERIFY(runCell(own, done, QString(), indicators_, QStringLiteral("test"), {}));
  QCOMPARE(cellOf(own, done, QString(), indicators_).state, CellState::Done);

  // The file is gone: what has not been run waits and names the path; what
  // is finished stays finished (it cannot be told to be out of date).
  QVERIFY(QFile::rename(own.catalogPath(), own.catalogPath() + ".away"));
  Cell cell = cellOf(own, never, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Blocked);
  QCOMPARE(cell.fix, Fix::Cases);
  QVERIFY2(cell.why.contains(QStringLiteral("catalog not found")) &&
               cell.why.contains(QDir::toNativeSeparators(own.catalogPath())),
           qPrintable(cell.why));
  QCOMPARE(cellOf(own, done, QString(), indicators_).state, CellState::Done);
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           runCell(own, never, QString(), indicators_,
                                   QStringLiteral("test"), {}));

  // The same file back: still finished. A different file under the same
  // name: stale, and the reason says it is the catalog.
  QVERIFY(QFile::rename(own.catalogPath() + ".away", own.catalogPath()));
  QCOMPARE(cellOf(own, done, QString(), indicators_).state, CellState::Done);
  QFile f(own.catalogPath());
  QVERIFY(f.open(QIODevice::Append));
  f.write("01/01/2022 00:00:00,39.0000,38.0000,5.00,,ML,2.0,Added,x1\n");
  f.close();
  cell = cellOf(own, done, QString(), indicators_);
  QCOMPARE(cell.state, CellState::Stale);
  QVERIFY2(cell.why.contains(QStringLiteral("catalog file has changed")),
           qPrintable(cell.why));
  QCOMPARE(sha256Cached(own.catalogPath()), sha256Of(own.catalogPath()));
}

void TstStudyRuns::recipeOrderIsExplained() {
  const Case &c = s_.cases[0];
  Recipe unbound = classification_;
  unbound.id = QStringLiteral("unbound");
  unbound.settings.insert(QStringLiteral("features_recipe"), QString());
  Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), unbound);
  QCOMPARE(cell.state, CellState::Blocked);
  QCOMPARE(cell.fix, Fix::Recipe);
  QVERIFY2(cell.why.contains(QStringLiteral("Edit recipe")),
           qPrintable(cell.why));
  unbound.settings.insert(QStringLiteral("features_recipe"),
                          QStringLiteral("gone"));
  cell = cellOf(s_, c, QStringLiteral("SYN1"), unbound);
  QCOMPARE(cell.fix, Fix::Recipe);
  QVERIFY2(cell.why.contains(QStringLiteral("gone")), qPrintable(cell.why));

  // A features config that names a catalog of its own is seen as such.
  QVERIFY(calibrationOverride(features_).isEmpty());
  Recipe own = features_;
  own.settings.insert(
      QStringLiteral("config_json"),
      QStringLiteral("{\"chaos_calibration\": {\"catalog\": \"/x/y.csv\"}}"));
  QCOMPARE(calibrationOverride(own), QStringLiteral("/x/y.csv"));
}

void TstStudyRuns::rerunFeaturesMakeTheClassificationStale() {
  const Case &c = s_.cases[0];
  const QString st = QStringLiteral("SYN1");
  QCOMPARE(cellOf(s_, c, st, classification_).state, CellState::Done);
  // The features are run again (a new record, a later time).
  const QString dir = runDir(s_, c, st, features_);
  RunRecord rec = *RunRecord::read(dir);
  rec.finished = rec.finished.addSecs(3600);
  QVERIFY(rec.write(dir));
  QCOMPARE(cellOf(s_, c, st, features_).state, CellState::Done);
  const Cell cell = cellOf(s_, c, st, classification_);
  QCOMPARE(cell.state, CellState::Stale);
  QVERIFY2(cell.why.contains(QStringLiteral("features were run again")),
           qPrintable(cell.why));
}

// A features recipe run for real, on the two hours of synthetic waveforms
// of the fx tests: with the study's catalog as it is, with a catalog that
// is not in UTC, and with a configuration that names a catalog of its own.
void TstStudyRuns::featuresOnASmallStation() {
  const QString data = QStringLiteral(SKG_TEST_DATA "/fx/enz");
  Study s;
  s.dir = dir_.filePath("waves-study");
  s.name = QStringLiteral("Waves");
  const QString station = QStringLiteral("SYN1");
  QVERIFY(QDir().mkpath(QDir(s.dir).filePath("waves/" + station)));
  for (const QString &name : QDir(data).entryList(
           {QStringLiteral("*.mseed"), QStringLiteral("*.zip")}, QDir::Files))
    QVERIFY(QFile::copy(QDir(data).filePath(name),
                        QDir(s.dir).filePath("waves/" + station + "/" + name)));
  QVERIFY(QFile::copy(QDir(data).filePath("catalog.csv"),
                      QDir(s.dir).filePath("catalog.csv")));
  s.catalog = QStringLiteral("catalog.csv");
  s.dataRoot = QStringLiteral("waves");
  // A reference date just after the data (2024-03-05).
  Case c = referenceCase(QStringLiteral("After the data"),
                         QDateTime(QDate(2024, 3, 6), QTime(0, 0), QTimeZone::UTC),
                         38.60, 39.30);
  c.id = s.freeCaseId(c.name);
  c.spanDays = 30;
  StationRef st;
  st.code = station;
  st.lat = 38.60;
  st.lon = 39.20;
  c.stations << st;
  s.cases << c;

  // The configuration of the fx tests: 5 Hz, 200 s windows side by side.
  fx::Config cfg;
  cfg.fs = 5.0;
  cfg.winSec = 200;
  cfg.stepSec = 200;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.calibration.minMag = 2.0;
  Recipe features = defaultRecipe(kToolFeatures);
  features.id = QStringLiteral("features");
  features.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson()));
  s.recipes << features;
  QVERIFY(calibrationOverride(features).isEmpty());

  QCOMPARE(cellOf(s, c, station, features).state, CellState::Missing);
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &l) { log << l; };
  QVERIFY(runCell(s, c, station, features, QStringLiteral("test"), hooks));
  Cell cell = cellOf(s, c, station, features);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY(cell.record->fitted.value("windows").toInt() > 10);
  QCOMPARE(cell.record->catalogSha, sha256Of(s.catalogPath()));
  const QString table =
      QDir(cell.dir).filePath(cell.record->outputs.value("features").toString());
  QVERIFY2(QFileInfo::exists(table), qPrintable(table));
  QVERIFY(QFileInfo::exists(cell.log));
  QVERIFY(!QFileInfo::exists(QDir(cell.dir).filePath("catalog_utc.csv")));
  // The zone is none of its business; the span is.
  Study changed = s;
  changed.cases[0].zone.radiusKm = 55;
  QCOMPARE(cellOf(changed, changed.cases[0], station, features).state,
           CellState::Done);
  changed.cases[0].spanDays = 60;
  QCOMPARE(cellOf(changed, changed.cases[0], station, features).state,
           CellState::Stale);

  // The same catalog said to be three hours ahead of UTC: the run is stale,
  // and the new one calibrates on a copy moved to UTC.
  Study ahead = s;
  ahead.catalogUtcOffsetHours = 3;
  QCOMPARE(cellOf(ahead, c, station, features).state, CellState::Stale);
  log.clear();
  QVERIFY(runCell(ahead, c, station, features, QStringLiteral("test"), hooks));
  cell = cellOf(ahead, c, station, features);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY2(log.join('\n').contains(QStringLiteral("moved to UTC")),
           qPrintable(log.join('\n')));
  QFile utc(QDir(cell.dir).filePath("catalog_utc.csv"));
  QVERIFY(utc.open(QIODevice::ReadOnly));
  const QByteArray moved = utc.readAll();
  // 05/03/2024 00:45:00 at UTC+3 is 04/03/2024 21:45:00 UTC.
  QVERIFY2(moved.contains("04/03/2024 21:45:00"), moved.constData());
  QVERIFY(!moved.contains("05/03/2024 00:45:00"));

  // A configuration with a catalog of its own: used, said, and not passed
  // off as the study's.
  fx::Config own = cfg;
  own.calibration.catalog = QDir(data).filePath("catalog.csv");
  Recipe overriding = features;
  overriding.id = QStringLiteral("features-own");
  overriding.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(own.toJson()).toJson()));
  QCOMPARE(calibrationOverride(overriding), own.calibration.catalog);
  log.clear();
  QVERIFY(runCell(s, c, station, overriding, QStringLiteral("test"), hooks));
  cell = cellOf(s, c, station, overriding);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY(cell.record->catalogSha.isEmpty());
  QVERIFY(cell.record->inputs.contains(own.calibration.catalog));
  QVERIFY2(log.join('\n').contains(QStringLiteral("the recipe's own")),
           qPrintable(log.join('\n')));
  // With an override the study's catalog is not needed, and replacing it
  // does not make the run stale.
  Study without = s;
  without.catalog.clear();
  QCOMPARE(cellOf(without, c, station, overriding).state, CellState::Done);
  // An override that is not there fails with its path.
  own.calibration.catalog = dir_.filePath("no-such-catalog.csv");
  overriding.id = QStringLiteral("features-lost");
  overriding.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(own.toJson()).toJson()));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           runCell(s, c, station, overriding,
                                   QStringLiteral("test"), {}));
  cell = cellOf(s, c, station, overriding);
  QCOMPARE(cell.state, CellState::Failed);
  QVERIFY2(cell.why.contains(QStringLiteral("no-such-catalog.csv")),
           qPrintable(cell.why));
}

QTEST_MAIN(TstStudyRuns)
#include "tst_study_runs.moc"
