// The train / validation / test split by day within each label, the scalers
// (fitted on the training set only) with clipping, and grouped stratified
// folds for cross-validation. Port of preprocessing4classification.py and
// the splitter tuning.py uses.
#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

#include <vector>

#include "sfc/dataset.h"
#include "sfc/settings.h"

namespace skg::sfc {

// "train" / "val" / "test" per row of the dataset, chosen per day within each
// label.
std::vector<QString> splitByDay(const Dataset &d, const Settings &s);

struct Scaler {
  QString kind; // minmax | standard | robust
  std::vector<double> center, scale;
  std::optional<double> clip;
  void fit(const Dataset &d, const std::vector<int> &rows, const QString &kind,
           std::optional<double> clip);
  // One scaled row.
  std::vector<double> transform(const double *row, int n) const;
  QString toJson() const;
  static Scaler fromJson(const QString &json);
};

struct Matrix {
  int rows = 0, cols = 0;
  std::vector<double> X; // row-major
  std::vector<int> y;
  std::vector<int> index; // row of the dataset
  const double *row(int r) const { return X.data() + size_t(r) * size_t(cols); }
};

struct SplitData {
  Matrix train, val, test;
  Scaler scaler;
  std::vector<QString> split; // per dataset row
  const Matrix &part(const QString &name) const {
    return name == "train" ? train : name == "val" ? val : test;
  }
};

// Splits, scales and clips. Throws std::invalid_argument on an unknown scaler.
SplitData prepareData(const Dataset &d, const Settings &s);

// Days and windows per class and split, as sfc prints them.
QStringList splitSummary(const Dataset &d, const std::vector<QString> &split,
                         const Settings &s);

// Folds of a grouped, stratified k-fold over the given rows: a day is never
// split between folds and every fold gets a share of every class. Returns
// (train rows, validation rows) per fold, indices into `rows`' order.
struct Fold {
  std::vector<int> train, val;
};
std::vector<Fold> stratifiedGroupKFold(const std::vector<int> &y,
                                       const std::vector<QDate> &groups, int k,
                                       quint64 seed);

// The matrix of some rows of a split (train + val concatenated, folds...).
Matrix concat(const Matrix &a, const Matrix &b);
Matrix take(const Matrix &m, const std::vector<int> &rows);

} // namespace skg::sfc
