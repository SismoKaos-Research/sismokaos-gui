// The events around the station in the table's period, the main event, the
// station's nearest fault, and the map; nearby stations of the main event.
#pragma once

#include <QAbstractTableModel>
#include <QWidget>

#include <memory>

#include "modules/analysis/analysismodule.h"

class QTableView;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QCheckBox;

namespace skg {

class FigureView;

class EventsModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Time,
    Mag,
    Depth,
    Place,
    Distance,
    Direction,
    Band,
    FaultName,
    FaultKm,
    FaultDir,
    ColumnCount
  };
  explicit EventsModel(QObject *parent = nullptr);
  void setEvents(const std::vector<analysis::EventAtStation> &events,
                 const analysis::FaultIndex *faults, const QString &lang);
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  struct Row {
    analysis::EventAtStation e;
    QString faultName, faultDir;
    double faultKm = -1;
  };
  std::vector<Row> rows_;
  QString lang_;
};

class CatalogPane : public QWidget {
  Q_OBJECT
public:
  CatalogPane(std::shared_ptr<AnalysisState> state, QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void exportMap(bool svg);
  void exportTable();

  std::shared_ptr<AnalysisState> state_;
  EventsModel *model_;
  QTableView *table_;
  QLabel *summary_, *faultInfo_;
  FigureView *map_;
  QDoubleSpinBox *extent_;
  QCheckBox *showFaults_;
  QPushButton *exportPng_, *exportSvg_, *exportCsv_;
};

} // namespace skg
