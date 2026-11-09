// Station comparison: several feature tables, one metric, the common days;
// a grid of daily figures (stations down, days across) rendered as a job.
#pragma once

#include <QWidget>

#include <memory>

#include "core/module.h"
#include "modules/analysis/analysismodule.h"

class QListWidget;
class QComboBox;
class QPushButton;
class QLabel;
class QSpinBox;

namespace skg {

class FigureView;

class GridPane : public QWidget {
  Q_OBJECT
public:
  GridPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
           QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void addTables();
  void build();

  AppContext ctx_;
  std::shared_ptr<AnalysisState> state_;
  QListWidget *tables_;
  QPushButton *add_, *remove_, *build_, *save_;
  QComboBox *column_;
  QSpinBox *maxDays_;
  QLabel *note_;
  FigureView *view_;
  QImage grid_;
};

} // namespace skg
