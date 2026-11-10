// Runs and the comparison on a synthetic study: the indicators of two
// earthquakes and a reference date from the synthetic catalog, and a
// classification on fabricated feature tables (no waveforms are processed).
#include <QDir>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>

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
  QVERIFY(caseSignature(c, QStringLiteral("SYN1")) !=
          caseSignature(c, QString()));
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
  QCOMPARE(cellOf(s_, s_.cases[0], QStringLiteral("SYN1"), edited).state,
           CellState::Stale);
  Case moved = s_.cases[0];
  moved.spanDays = 100;
  QCOMPARE(cellOf(s_, moved, QStringLiteral("SYN1"), indicators_).state,
           CellState::Stale);
  QCOMPARE(cellOf(s_, s_.cases[0], QStringLiteral("SYN1"), indicators_).state,
           CellState::Done);
}

void TstStudyRuns::blockedCellsSayWhy() {
  const Case &c = s_.cases[0];
  Cell cell = cellOf(s_, c, QStringLiteral("SYN1"), features_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY(cell.why.contains(QStringLiteral("waveform")));
  cell = cellOf(s_, c, QStringLiteral("SYN1"), classification_);
  QCOMPARE(cell.state, CellState::Blocked);
  QVERIFY(cell.why.contains(QStringLiteral("features")));
  // A case without stations has nothing for the per-station tools.
  QCOMPARE(cellOf(s_, c, QString(), classification_).state,
           CellState::NotApplicable);
  Study noCatalog = s_;
  noCatalog.catalog.clear();
  noCatalog.dir = dir_.filePath("other");
  QCOMPARE(cellOf(noCatalog, c, QString(), indicators_).state,
           CellState::Blocked);
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
    rec.caseSignature = caseSignature(c, QStringLiteral("SYN1"));
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
  QVERIFY(!cell.record->ok);
}

QTEST_MAIN(TstStudyRuns)
#include "tst_study_runs.moc"
