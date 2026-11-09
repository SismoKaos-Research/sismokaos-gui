// A multilayer perceptron classifier with scikit-learn's MLPClassifier
// semantics as sfc uses them: dense layers, relu/tanh/logistic, softmax
// output with cross-entropy, Adam or SGD on shuffled mini-batches, L2
// penalty alpha, Glorot initialisation; trained one epoch at a time so the
// caller can stop early on the validation loss.
#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

#include "sfc/settings.h"

namespace skg::sfc {

struct MlpOptions {
  std::vector<int> hidden{16};
  QString activation = QStringLiteral("relu"); // relu | tanh | logistic
  QString solver = QStringLiteral("adam");     // adam | sgd
  int batchSize = 256;
  double alpha = 1e-4;
  double learningRate = 1e-3;
  double beta1 = 0.9, beta2 = 0.999, epsilon = 1e-8;
  quint64 seed = 42;
};

class Mlp {
public:
  Mlp() = default;
  Mlp(const MlpOptions &opt, int nFeatures, int nClasses);
  // One pass over the data in shuffled mini-batches.
  void epoch(const std::vector<double> &X, const std::vector<int> &y);
  std::vector<double> proba(const double *X, int rows) const; // rows x nClasses
  int nClasses() const { return nClasses_; }
  int nFeatures() const { return nFeatures_; }
  int epochsDone() const { return epochs_; }
  QByteArray toJson() const;
  static Mlp fromJson(const QByteArray &json);

private:
  struct Layer {
    int in = 0, out = 0;
    std::vector<double> W, b;           // W: out x in
    std::vector<double> mW, vW, mb, vb; // Adam moments
  };
  MlpOptions opt_;
  int nFeatures_ = 0, nClasses_ = 0;
  std::vector<Layer> layers_;
  int epochs_ = 0, steps_ = 0;
  Rng rng_{0};
  void forward(const double *x, std::vector<std::vector<double>> &acts) const;
  double act(double v) const;
  double actGrad(double a) const;
};

} // namespace skg::sfc
