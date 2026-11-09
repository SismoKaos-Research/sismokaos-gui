// The result of an indicators run: the window table and the figures
// written, shown from the output folder (so an older run can be opened
// too).
#pragma once

#include <QAbstractTableModel>
#include <QWidget>

#include <memory>

#include "cbi/table.h"
#include "modules/indicators/indicatorsmodule.h"

class QTableView;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace skg {

class FigureView;

class IndicatorTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit IndicatorTableModel(QObject *parent = nullptr);
  void setTable(cbi::IndicatorTable table);
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  cbi::IndicatorTable table_;
};

class IndicatorsResultsPane : public QWidget {
  Q_OBJECT
public:
  IndicatorsResultsPane(std::shared_ptr<IndicatorsState> state,
                        QWidget *parent = nullptr);
  // Shows the state's result.
  void refresh();
  // Shows an output folder: its *_pencereler.csv and PNGs.
  void showFolder(const QString &dir);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void showFigure(QListWidgetItem *item);
  void openFolder();
  void chooseFolder();

  std::shared_ptr<IndicatorsState> state_;
  QString dir_;
  IndicatorTableModel *model_;
  QTableView *table_;
  QListWidget *figures_;
  FigureView *view_;
  QLabel *info_;
  QPushButton *open_, *choose_;
};

} // namespace skg
