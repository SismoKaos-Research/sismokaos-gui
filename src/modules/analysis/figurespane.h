// The metric figures: one day, all days, or a window around the main event,
// for any plottable column; export of the shown figure, or of every daily
// figure into a folder as a job.
#pragma once

#include <QWidget>

#include <memory>

#include "core/module.h"
#include "modules/analysis/analysismodule.h"

class QComboBox;
class QDateEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;

namespace skg {

class FigureView;

class FiguresPane : public QWidget {
  Q_OBJECT
public:
  FiguresPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
              QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  enum Mode { Daily, Combined, Centred };
  void retranslate();
  void redraw();
  void exportShown(bool svg);
  void exportAllDaily();
  void updateControls();

  AppContext ctx_;
  std::shared_ptr<AnalysisState> state_;
  QComboBox *column_, *mode_, *channel_;
  QDateEdit *day_;
  QSpinBox *windowDays_, *dpi_;
  QCheckBox *autoRange_;
  QDoubleSpinBox *yMin_, *yMax_;
  QPushButton *exportPng_, *exportSvg_, *exportAll_;
  QLabel *note_;
  FigureView *view_;
};

} // namespace skg
