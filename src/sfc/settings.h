// The settings of a classification project, as sfc's config.py defines them
// and its project.toml / settings.toml store them (lowercase keys, "none"
// for null). Only the settings the classification uses are here; sfc's
// feature-extraction settings are read and written back untouched so a
// project file shared with the Python tool keeps them.
#pragma once

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <optional>
#include <vector>

namespace skg::sfc {

struct ClassRow {
  int label = 0;
  int firstDay = 0, lastDay = 0; // days before the event, inclusive
  std::optional<int> step;       // nullopt: dates from fixedDates
  int count = 0;
};

// One grid: parameter name -> the values to try.
using Grid = QMap<QString, QVariantList>;
// Every combination of a grid, as scikit-learn's ParameterGrid orders them
// (keys sorted, the last key varying fastest).
QList<QVariantMap> expandGrid(const Grid &grid);
// "{C = 10, gamma = 'scale'}" as Python prints a dict: {'C': 10, 'gamma':
// 'scale'}
QString paramsText(const QVariantMap &params);

struct Settings {
  // data and event
  QString station = QStringLiteral("STATION");
  QString earthquakeName = QStringLiteral("event");
  QDate eqDate{2000, 1, 1};
  int eqCutoffHour = 12;
  // The event's time to the second (seconds since 1970 UTC), when the caller
  // knows it: no window that ends after it is used, on any day. Without it
  // the cut is at the start of `eqCutoffHour` on the event day. Not in the
  // settings file; a study run sets it from the case.
  std::optional<double> eqEpoch;
  QVariant maxShiftDays = QStringLiteral("step"); // "step" or an int
  double minDayHours = 20;
  QList<ClassRow> classTable{{0, 1, 30, 1, 30},
                             {1, 31, 90, 2, 30},
                             {2, 91, 365, 9, 30},
                             {3, 366, 1826, 49, 30}};
  QMap<int, QList<QDate>> fixedDates;
  QString csvSep = QStringLiteral(";"), csvDecimal = QStringLiteral(",");

  // features, merging, split, scaling
  QStringList featureColumns{"raw_mean", "raw_std",  "raw_min",   "raw_max",
                             "norm_min", "norm_max", "mean_freq", "mean_period",
                             "wolf_lye", "ros_lye",  "samp_ent",  "corr_dim"};
  std::optional<QMap<int, int>> labelMerge;
  QMap<int, QList<int>> classMonths;
  int randomSeed = 42;
  double trainSize = 0.6, valSize = 0.2;
  QString scaler = QStringLiteral("robust"); // minmax | robust | standard
  std::optional<double> clipAfterScaling = 5.0;

  // experiment
  QString runName = QStringLiteral("run_01");
  QStringList modelsToRun{"logistic", "svm_linear", "svm_poly", "svm_rbf"};
  std::optional<QString> classWeight = QStringLiteral("balanced");
  QString selectBy = QStringLiteral("f1_macro"); // f1_macro | accuracy
  std::optional<int> cvFolds = 5;
  bool saveModels = true;
  Grid logisticGrid{{"C", {1, 10, 100}}};
  int logisticMaxIter = 2000;
  QMap<QString, Grid> svmGrids{
      {"svm_linear", {{"C", {1, 10}}}},
      {"svm_poly", {{"C", {1, 10}}, {"degree", {3}}}},
      {"svm_rbf", {{"C", {1, 10}}, {"gamma", {QStringLiteral("scale"), 1}}}}};
  std::optional<int> svmMaxTrainWindows = 5000;
  Grid annGrid{{"hidden_layer_sizes", {QVariantList{16}, QVariantList{32, 16}}},
               {"batch_size", {256}},
               {"alpha", {0.001, 0.01}},
               {"learning_rate_init", {0.001}}};
  QVariantMap annFixed{{"solver", "adam"}, {"activation", "relu"}};
  int annMaxEpochs = 200, annPatience = 10;
  bool annBalanceClasses = true;
  QStringList treeModelsToRun{"random_forest", "gradient_boosting", "xgboost"};
  QMap<QString, Grid> treeGrids{{"random_forest",
                                 {{"n_estimators", {100}},
                                  {"max_depth", {10, 20}},
                                  {"min_samples_leaf", {1, 5}}}},
                                {"gradient_boosting",
                                 {{"n_estimators", {100}},
                                  {"learning_rate", {0.05, 0.1}},
                                  {"max_depth", {2, 3}}}},
                                {"xgboost",
                                 {{"n_estimators", {100}},
                                  {"learning_rate", {0.05, 0.1}},
                                  {"max_depth", {3, 6}}}}};
  std::optional<QStringList> compareRuns;
  QList<QStringList> scatterPairs{
      {"wolf_lye", "samp_ent"},    {"wolf_lye", "corr_dim"},
      {"wolf_lye", "ros_lye"},     {"ros_lye", "samp_ent"},
      {"raw_std", "ros_lye"},      {"mean_period", "ros_lye"},
      {"mean_freq", "raw_std"},    {"corr_dim", "samp_ent"},
      {"corr_dim", "ros_lye"},     {"raw_std", "wolf_lye"},
      {"raw_std", "samp_ent"},     {"raw_std", "corr_dim"},
      {"mean_period", "wolf_lye"}, {"mean_freq", "wolf_lye"},
      {"mean_freq", "samp_ent"}};
  QStringList scatterCombinationFeatures;
  QStringList scatterTripleFeatures{"wolf_lye", "ros_lye", "samp_ent",
                                    "corr_dim"};
  QList<QStringList> scatterTriples{{"raw_std", "wolf_lye", "samp_ent"},
                                    {"raw_std", "ros_lye", "samp_ent"},
                                    {"raw_std", "wolf_lye", "ros_lye"},
                                    {"mean_freq", "wolf_lye", "samp_ent"},
                                    {"mean_period", "ros_lye", "samp_ent"},
                                    {"mean_freq", "raw_std", "ros_lye"},
                                    {"mean_period", "raw_std", "wolf_lye"},
                                    {"mean_freq", "samp_ent", "corr_dim"},
                                    {"raw_std", "samp_ent", "corr_dim"}};
  QStringList runSteps{"data_understanding", "split",  "split_visualization",
                       "baseline",           "ann",    "trees",
                       "error_timeline",     "compare"};
  bool stopOnError = false;

  // Settings of the file this struct does not model (sfc's extraction
  // settings, raw data paths...), kept verbatim for writing back.
  QVariantMap passthrough;

  // label -> class after merging (identity without a merge). Throws
  // std::invalid_argument when the merge does not cover the labels.
  QMap<int, int> labelToClass() const;
  // "31-365 days" per class.
  QMap<int, QString> classNames() const;
  QStringList labelsOf() const; // the labels of the class table, as text

  // TOML, sfc's layout. Throws std::runtime_error on a bad file.
  static Settings fromToml(const QString &text);
  static Settings fromToml(const QString &text, const Settings &base);
  static Settings load(const QString &path);
  static Settings load(const QString &path, const Settings &base);
  QString toToml() const;
  bool save(const QString &path) const;
  // The lowercase key -> value map of every setting, for comparisons.
  QVariantMap asMap() const;
  // Keys that differ between two settings, run_name never counting.
  static QStringList differences(const QVariantMap &a, const QVariantMap &b,
                                 bool experimentOnly = true);
  // Settings of the project rather than of an experiment (sfc's
  // PROJECT_SETTINGS).
  static const QSet<QString> &projectKeys();
};

// SplitMix64, so a run picks the same days and subsets on every platform.
class Rng {
public:
  explicit Rng(quint64 seed) : s_(seed) {}
  quint64 next();
  double uniform(); // [0, 1)
  int below(int n);
  std::vector<int> permutation(int n);
  std::vector<int> choice(int n, int k, bool replace);

private:
  quint64 s_;
};

} // namespace skg::sfc
