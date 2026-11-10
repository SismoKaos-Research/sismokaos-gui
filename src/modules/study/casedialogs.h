// The two ways to add a case: pick an earthquake from the study's catalog
// (with the stations nearest to it), or name a reference date at a place.
#pragma once

#include <QDialog>

#include <functional>
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
class QPushButton;

namespace skg {

// Fills a table with stations (Use, Station, Distance, Azimuth, and
// Waveforms when `waveforms` is given: what the study's folders hold of the
// station); `ticked` codes start checked. Shared with the Cases page.
using StationNote = std::function<QString(const study::StationRef &)>;
void fillStationTable(QTableWidget *table,
                      const QList<study::StationRef> &stations,
                      const QStringList &ticked,
                      const StationNote &waveforms = {});
// "found: 340 of 365 days in span", "found: at least 12 of 365 days in span
// (coverage not read yet)" or "not found", from the study's waveform index.
QString waveformNote(const study::Study &study, const study::StationRef &s,
                     const QDateTime &from, const QDateTime &to);
// Whether the study's waveform folders hold anything of the station.
bool hasWaveformsOf(const study::Study &study, const study::StationRef &s);
// The codes of the stations the study's waveform folders hold, sorted.
QStringList stationsWithWaveforms(const study::Study &study);
QList<study::StationRef> tickedStations(const QTableWidget *table,
                                        const QList<study::StationRef> &all);

class AddFromCatalogDialog : public QDialog {
  Q_OBJECT
public:
  // Throws std::runtime_error when the catalog cannot be read.
  // Without a catalog of its own the study offers the built-in list of
  // large earthquakes. `nearStations` starts the "near stations" filter with
  // those codes.
  AddFromCatalogDialog(const study::Study &study, QWidget *parent = nullptr,
                       const QStringList &nearStations = {});
  std::optional<study::Case> result() const { return result_; }
  void accept() override;

private:
  void refill();
  void eventChosen();
  void updateNotes(); // warnings under the tables, and the Add button

  const study::Study &study_;
  bool builtIn_; // the events are the built-in list, not the study's catalog
  QStringList haveData_; // stations found in the study's waveform folders
  QPushButton *mine_;
  QPushButton *ok_;
  QLabel *range_, *stationNote_, *warning_;
  std::vector<analysis::Event> events_;
  std::vector<analysis::Station> stations_;
  std::vector<int> shown_; // row -> index into events_
  // The "near stations" filter: the codes typed, resolved against the list.
  std::vector<analysis::Station> filterStations_;
  QLineEdit *nearCodes_;
  QDoubleSpinBox *nearKm_;
  QLabel *nearNote_;
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
