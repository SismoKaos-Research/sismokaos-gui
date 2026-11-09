// The experiment settings of a classification project: the ones changed
// between experiments as a form, the whole file as TOML text beside it.
#pragma once

#include <QWidget>

#include <memory>

#include "modules/classification/classificationmodule.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QDateTimeEdit;
class QTableWidget;

namespace skg {

class SettingsPane : public QWidget {
  Q_OBJECT
public:
  SettingsPane(std::shared_ptr<ClassificationState> state,
               QWidget *parent = nullptr);
  // Fill the widgets from the state's settings (and feature choices from the
  // source).
  void refresh();
  // Read the form back into the state's settings; false (with a message) when
  // invalid.
  bool apply(QString *error = nullptr);

signals:
  void edited();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void showToml();
  bool applyToml(QString *error);

  std::shared_ptr<ClassificationState> state_;
  bool filling_ = false;

  QLineEdit *runName_, *labelMerge_, *classMonths_, *station_, *eventName_;
  QDateTimeEdit *eqTime_;
  QTableWidget *classTable_;
  QPushButton *addRow_, *removeRow_;
  QListWidget *features_;
  QCheckBox *logistic_, *svmLinear_, *svmPoly_, *svmRbf_, *randomForest_,
      *gradientBoosting_, *xgboost_;
  QCheckBox *balanced_, *cv_, *clip_, *saveModels_, *annBalance_;
  QSpinBox *cvFolds_, *seed_, *annEpochs_, *annPatience_;
  QDoubleSpinBox *clipValue_, *trainSize_, *valSize_;
  QComboBox *scaler_, *selectBy_;
  QPlainTextEdit *toml_;
  QPushButton *applyToml_;
  QLabel *tomlNote_;
  QList<QPair<QLabel *, const char *>> rows_;
};

} // namespace skg
