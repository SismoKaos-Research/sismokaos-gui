// The classifiers sfc compares, behind one interface: logistic regression,
// random forest, gradient boosting and XGBoost through XGBoost's C API
// (gblinear for the first, parallel trees for the forest), the SVMs through
// libsvm, and the neural network of mlp.h. Each knows its name and
// parameters as sfc logs them and saves itself to a JSON envelope.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

#include "sfc/mlp.h"
#include "sfc/settings.h"
#include "sfc/split.h"

namespace skg::sfc {

// Per-row weights for class_weight = "balanced": n / (k * count of the class).
std::vector<double> balancedWeights(const std::vector<int> &y, int nClasses);

class Model {
public:
  virtual ~Model() = default;
  QString name; // sfc's model name: logistic, svm_rbf, random_forest, ann, ...
  QVariantMap params; // the tuned parameters
  int nClasses = 0;
  // `weights` may be empty. Throws std::runtime_error on a library error.
  virtual void fit(const Matrix &m, const std::vector<double> &weights) = 0;
  // rows x nClasses probabilities (SVMs: Platt estimates).
  virtual std::vector<double> proba(const Matrix &m) const = 0;
  virtual std::vector<int> predict(const Matrix &m) const;
  virtual QByteArray serialize() const = 0;
  virtual void deserialize(const QByteArray &data) = 0;
  // The JSON envelope with name, params, classes and the model.
  QByteArray save() const;
  bool saveTo(const QString &path) const;
};

// A model for sfc's name with its fixed settings and tuned parameters.
// Throws std::invalid_argument for an unknown name.
std::unique_ptr<Model> makeModel(const QString &name, const QVariantMap &params,
                                 const Settings &s);
// Reads an envelope back. Throws std::runtime_error.
std::unique_ptr<Model> loadModel(const QString &path, const Settings &s);

// --- the three families, exposed for the tests
// -------------------------------------

class XgbModel : public Model {
public:
  // kind: logistic | random_forest | gradient_boosting | xgboost
  XgbModel(const QString &kind, const QVariantMap &params, int seed,
           int threads = 0);
  ~XgbModel() override;
  void fit(const Matrix &m, const std::vector<double> &weights) override;
  std::vector<double> proba(const Matrix &m) const override;
  QByteArray serialize() const override;
  void deserialize(const QByteArray &data) override;
  int rounds() const { return rounds_; }

private:
  QString kind_;
  int seed_, threads_;
  int rounds_ = 0;
  void *booster_ = nullptr; // BoosterHandle
  void setParams(void *booster, int nFeatures) const;
};

class SvmModel : public Model {
public:
  // kernel: linear | poly | rbf; params C, degree, gamma ("scale", "auto" or a
  // number)
  SvmModel(const QString &kernel, const QVariantMap &params,
           std::optional<QString> classWeight, int seed);
  ~SvmModel() override;
  void fit(const Matrix &m, const std::vector<double> &weights) override;
  std::vector<double> proba(const Matrix &m) const override;
  std::vector<int> predict(const Matrix &m) const override;
  QByteArray serialize() const override;
  void deserialize(const QByteArray &data) override;

private:
  QString kernel_;
  std::optional<QString> classWeight_;
  int seed_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class MlpModel : public Model {
public:
  MlpModel(const MlpOptions &opt);
  // One epoch; the tuning drives early stopping.
  void epoch(const Matrix &m);
  void fit(const Matrix &m,
           const std::vector<double> &weights) override; // `epochs` of them
  int epochs = 1;
  std::vector<double> proba(const Matrix &m) const override;
  QByteArray serialize() const override;
  void deserialize(const QByteArray &data) override;
  const Mlp &net() const { return net_; }
  void reset(int nFeatures, int nClasses);
  MlpModel clone() const;

private:
  MlpOptions opt_;
  Mlp net_;
};

MlpOptions mlpOptions(const QVariantMap &params, const Settings &s, int nTrain);

} // namespace skg::sfc
