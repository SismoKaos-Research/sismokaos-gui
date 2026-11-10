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
// median of the column per day), limited to the case's span.
QList<Series> seriesOf(const Study &s, const Recipe &r, const QString &column);

struct FittedRow {
  QString caseName, station;
  QVariantMap fitted;
};
QList<FittedRow> fittedRows(const Study &s, const Recipe &r);

} // namespace skg::study
