// The two ways to add a case: pick an earthquake from the study's catalog
// (with the stations nearest to it), or name a reference date at a place.
#pragma once

#include <QDialog>

#include <optional>
#include <vector>

#include "analysis/events.h"
#include "study/study.h"

class QTableWidget;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QComboBox;
class QDateTimeEdit;
class QLabel;

namespace skg {

// Fills a table with stations (Use, Station, Distance, Azimuth); `ticked`
// codes start checked. Shared with the Cases page.
void fillStationTable(QTableWidget *table,
                      const QList<study::StationRef> &stations,
                      const QStringList &ticked);
QList<study::StationRef> tickedStations(const QTableWidget *table,
                                        const QList<study::StationRef> &all);

class AddFromCatalogDialog : public QDialog {
  Q_OBJECT
public:
  // Throws std::runtime_error when the catalog cannot be read.
  AddFromCatalogDialog(const study::Study &study, QWidget *parent = nullptr);
  std::optional<study::Case> result() const { return result_; }
  void accept() override;

private:
  void refill();
  void eventChosen();

  std::vector<analysis::Event> events_;
  std::vector<analysis::Station> stations_;
  std::vector<int> shown_; // row -> index into events_
  QList<study::StationRef> near_;
  std::optional<study::Case> result_;
  QDoubleSpinBox *minMag_;
  QLineEdit *place_;
  QTableWidget *table_;
  QLabel *count_;
  QLineEdit *name_;
  QSpinBox *span_;
  QDoubleSpinBox *maxKm_;
  QTableWidget *stationTable_;
};

class ReferenceDateDialog : public QDialog {
  Q_OBJECT
public:
  ReferenceDateDialog(const study::Study &study, QWidget *parent = nullptr);
  std::optional<study::Case> result() const { return result_; }
  void accept() override;

private:
  const study::Study &study_;
  std::optional<study::Case> result_;
  QLineEdit *name_;
  QDateTimeEdit *time_;
  QComboBox *place_;
  QDoubleSpinBox *lat_, *lon_;
  QSpinBox *span_;
};

} // namespace skg
