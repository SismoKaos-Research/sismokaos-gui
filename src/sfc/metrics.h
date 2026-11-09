// The metrics sfc reports: accuracy, macro precision, recall and F1 (a
// class without predictions counts as 0), one-vs-rest ROC AUC averaged over
// the classes present, the confusion matrix and ROC curve points.
#pragma once

#include <QMap>
#include <QString>

#include <vector>

namespace skg::sfc {

struct Metrics {
  double accuracy = 0, precisionMacro = 0, recallMacro = 0, f1Macro = 0;
  double rocAucMacro = std::nan("");
  double value(const QString &name) const; // "f1_macro", "accuracy", ...
};

// `scores`: rows x nClasses class scores (probabilities), may be empty.
Metrics computeMetrics(const std::vector<int> &yTrue,
                       const std::vector<int> &yPred,
                       const std::vector<double> &scores, int nClasses);
// rows: true class, columns: predicted, over the classes 0..nClasses-1.
std::vector<std::vector<int>> confusionMatrix(const std::vector<int> &yTrue,
                                              const std::vector<int> &yPred,
                                              int nClasses);
// Area under the ROC of a binary problem from scores (ties count half).
double rocAuc(const std::vector<char> &positive,
              const std::vector<double> &score);
struct RocCurve {
  std::vector<double> fpr, tpr;
  double auc = 0;
};
RocCurve rocCurve(const std::vector<char> &positive,
                  const std::vector<double> &score);
// Cross-entropy of probabilities against the true classes (clipped at 1e-15).
double logLoss(const std::vector<int> &yTrue, const std::vector<double> &proba,
               int nClasses);

} // namespace skg::sfc
