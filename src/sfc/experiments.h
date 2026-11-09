// The experiments of a project as sfc leaves them: models/experiments.csv
// (one row per trained model, ';' separated), models/<run>/settings.toml,
// the confusion matrices as CSV, model_comparison.csv. Port of
// experiments.py and the logging part of evaluation.py.
#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>
#include <vector>

#include "sfc/metrics.h"
#include "sfc/settings.h"

namespace skg::sfc {

struct Trial {
  QString time, run, model, params, features;
  int nFeatures = 0;
  double trainAcc = 0, trainF1 = 0, trainAuc = 0;
  double valAcc = 0, valF1 = 0, valAuc = 0;
  double testAcc = 0, testF1 = 0, testAuc = 0;
  double metric(const QString &name) const; // "val_f1", "test_auc", ...
};

// <project>/results/<station>/<event>/N and its models/ folder.
QString resultsDir(const QString &projectDir, const Settings &s);
QString modelsDir(const QString &projectDir, const Settings &s);
QString runDir(const QString &projectDir,
               const Settings &s); // models/<run_name>

// Appends one row. `features` are the feature columns, metrics per split.
bool logExperiment(const QString &experimentsCsv, const QString &run,
                   const QString &model, const QVariantMap &params,
                   const QStringList &features, const Metrics &train,
                   const Metrics &val, const Metrics &test);
// All rows, or the latest per (run, model).
std::vector<Trial> loadTrials(const QString &experimentsCsv,
                              bool latest = true);
// Runs with rows or a folder, in the order first run (folders by mtime after).
QStringList runNames(const QString &modelsDir);
std::optional<QVariantMap>
runSettings(const QString &modelsDir,
            const QString &run); // settings.toml as a map

struct RunSummary {
  QString run;
  int models = 0;
  QString time, bestModel;
  double trainF1 = 0, valF1 = 0, testF1 = 0, testAcc = 0, testAuc = 0;
  std::optional<QStringList> changes; // settings different from the project
                                      // (nullopt: no settings.toml)
};
std::vector<RunSummary> summarize(const QString &modelsDir,
                                  const QVariantMap &projectSettings);
// "feature_columns -corr_dim +x" / "cv_folds = 3" style change descriptions.
QStringList changeTexts(const QVariantMap &run, const QVariantMap &project);

// Confusion matrix CSV (';', "true c" rows, "pred c" columns).
bool writeConfusionCsv(const QString &path,
                       const std::vector<std::vector<int>> &cm,
                       const QStringList &labels);
std::optional<std::vector<std::vector<int>>>
readConfusionCsv(const QString &path, QStringList *labels = nullptr);

// The best trial of each model by validation F1, as model_comparison.csv.
std::vector<Trial> bestPerModel(const std::vector<Trial> &trials);
bool writeComparisonCsv(const QString &path, const std::vector<Trial> &best);

// Deletes the run's folder and its rows. Returns the rows removed.
int removeRun(const QString &modelsDir, const QString &run);

} // namespace skg::sfc
