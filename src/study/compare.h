// What the Compare page shows, read back from the runs of one recipe:
//
//   - the scores of a classification recipe, one row per case and station:
//     the model that did best on the validation days, its test F1 and AUC,
//     and beside them the same two numbers of the best model of sfc's
//     baseline step (logistic regression and the SVMs), also chosen on the
//     validation days. A number that is not in the run's experiments.csv is
//     NaN ("missing"), never estimated;
//   - a series of an indicators or features recipe, every case on one axis
//     of days before its event (the event at zero);
//   - the values each case fitted for itself, from run.json.
//
// What is read from a run's files is remembered until that run's run.json
// changes, so showing the page again reads nothing twice. Everything here
// may be called from a worker thread.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <vector>

#include "study/runs.h"
#include "study/study.h"

namespace skg::study {

struct ScoreRow {
  QString caseId, caseName, station;
  bool reference = false;
  double magnitude = 0, distanceKm = 0; // NaN when not known
  CellState state = CellState::Missing;
  QString bestModel, baselineModel; // empty when missing
  double testF1 = 0, testAuc = 0;   // of the best model; NaN when missing
  double baseF1 = 0, baseAuc = 0;   // of the baseline; NaN when missing
};
// Whether a model belongs to sfc's baseline step.
bool isBaselineModel(const QString &model);
QList<ScoreRow> scoreRows(const Study &s, const Recipe &classification);
// The rows as CSV (';' separated, '.' decimal), missing numbers empty.
QString scoresCsv(const QList<ScoreRow> &rows);

struct Series {
  QString caseId, caseName, station;
  bool reference = false;
  std::vector<double> daysBefore; // <= 0, increasing; the event is 0
  std::vector<double> values;     // NaN where the run has none
};
// The numeric columns the recipe's finished runs offer.
QStringList seriesColumns(const Study &s, const Recipe &r);
// One series per case (indicators) or per case and station (features: the
// median of the column per day), limited to the case's span. A finished run
// whose series cannot be read is named in `skipped` with the reason, not
// dropped silently.
QList<Series> seriesOf(const Study &s, const Recipe &r, const QString &column,
                       QStringList *skipped = nullptr);

struct FittedRow {
  QString caseName, station;
  QVariantMap fitted;
};
QList<FittedRow> fittedRows(const Study &s, const Recipe &r);

// Everything the Compare page shows, in one call (made off the GUI thread).
struct CompareRequest {
  QString scoreRecipe;  // id of a classification recipe, or empty
  QString seriesRecipe; // id of an indicators or features recipe, or empty
  QString column;       // empty: b_value when there is one, else the first
};
struct CompareData {
  bool hasScoreRecipe = false, hasSeriesRecipe = false;
  QList<ScoreRow> scores;
  QStringList columns;
  QString column; // the column the series are of
  QList<Series> series;
  QStringList skipped; // "case · station: why", for runs that could not be read
  QList<FittedRow> fitted;
};
CompareData loadCompare(const Study &s, const CompareRequest &q);

} // namespace skg::study
