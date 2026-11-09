// Earthquake labels for the loaded table: writes <stem>_etiketli.csv and
// .npy beside it and previews the result.
#pragma once

#include <QWidget>

#include <memory>

#include "core/module.h"
#include "modules/analysis/analysismodule.h"

class QLabel;
class QPushButton;
class QTableView;
class QDoubleSpinBox;

namespace skg {

class NpyModel;

class LabelsPane : public QWidget {
  Q_OBJECT
public:
  LabelsPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
             QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void run();

  AppContext ctx_;
  std::shared_ptr<AnalysisState> state_;
  QLabel *info_, *result_;
  QDoubleSpinBox *winSec_;
  QPushButton *run_;
  NpyModel *model_;
  QTableView *preview_;
};

} // namespace skg
