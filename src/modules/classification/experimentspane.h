// The experiments of a classification project: every run with its best
// model and the settings it changed, the trials of the chosen run, its
// confusion matrices and figures, the HTML report.
#pragma once

#include <QAbstractTableModel>
#include <QWidget>

#include <memory>
#include <vector>

#include "modules/classification/classificationmodule.h"
#include "sfc/experiments.h"

class QTableView;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QStackedWidget;
class QComboBox;

namespace skg {

class FigureView;

class TrialsModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit TrialsModel(QObject *parent = nullptr);
  void setTrials(std::vector<sfc::Trial> trials);
  const sfc::Trial *trialAt(int row) const;
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  std::vector<sfc::Trial> trials_;
};

class ExperimentsPane : public QWidget {
  Q_OBJECT
public:
  ExperimentsPane(std::shared_ptr<ClassificationState> state,
                  QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void selectRun(int row);
  void showFile(QListWidgetItem *item);
  void removeRun();
  void writeReport();

  std::shared_ptr<ClassificationState> state_;
  QString modelsDir_;
  std::vector<sfc::RunSummary> summaries_;
  QTableView *runs_;
  class RunsModel *runsModel_;
  TrialsModel *trialsModel_;
  QTableView *trials_;
  QLabel *runInfo_;
  QListWidget *files_;
  QStackedWidget *stack_;
  FigureView *figure_;
  QPlainTextEdit *text_;
  QPushButton *refresh_, *report_, *remove_, *openFolder_;
};

} // namespace skg
