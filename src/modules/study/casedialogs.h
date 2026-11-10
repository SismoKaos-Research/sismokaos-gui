// The two ways to add a case: pick an earthquake from the study's catalog
// (with the stations nearest to it), or name a reference date at a place.
#pragma once

#include <QDate>
#include <QDialog>
#include <QPair>
#include <QHash>

#include <functional>
#include <optional>
#include <vector>

#include "analysis/events.h"
#include "study/study.h"

class QTableWidget;
class QCheckBox;
class QDateEdit;
class QSettings;
class QStackedWidget;
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
// What the study's waveform index holds of the station in the period:
// "found: 340 of 365 days in span" once the exact scan has read its files;
// "about 340 of 365 days in span — exact scan running" while some of the
// days are estimated from the periods in the file names; "found: at least
// 12 of 365 days in span (coverage not read yet)" when some file's name
// carries no period; or "not found".
QString waveformNote(const study::Study &study, const study::StationRef &s,
                     const QDateTime &from, const QDateTime &to);
// The same as the tables show it: with "⚠ little of the span" added when
// the station has data on less than a tenth of the span's days (it is then
// used with a warning, runs.h).
QString waveformCell(const study::Study &study, const study::StationRef &s,
                     const QDateTime &from, const QDateTime &to);
// Whether the study's waveform folders hold anything of the station.
bool hasWaveformsOf(const study::Study &study, const study::StationRef &s);
// Whether they hold anything of it in [from, to): what a Features run of a
// case with that span would read. False for a station whose data lies
// entirely outside the span (it started recording later, say).
bool hasWaveformsIn(const study::Study &study, const study::StationRef &s,
                    const QDateTime &from, const QDateTime &to);
// The codes of the stations the study's waveform folders hold, sorted.
QStringList stationsWithWaveforms(const study::Study &study);
QList<study::StationRef> tickedStations(const QTableWidget *table,
                                        const QList<study::StationRef> &all);

class CasePreview;

// The stations a case starts with: of `near` (nearest first), the first `n`
// that have waveforms in [from, to); when none has, the first `n` nothing
// is known of (their data is still to be got). Never a station whose data
// lies outside the span, since a run on it could only fail.
QStringList stationsToStartWith(const study::Study &study,
                                const QList<study::StationRef> &near,
                                const QDateTime &from, const QDateTime &to,
                                int n);

// What to do with a chosen earthquake that is already a case of the study.
enum class Duplicates { Skip, Replace, AddAgain };

class AddFromCatalogDialog : public QDialog {
  Q_OBJECT
public:
  // Throws std::runtime_error when the catalog cannot be read.
  // Without a catalog of its own the study offers the built-in list of
  // large earthquakes. `nearStations` starts the "near stations" filter with
  // those codes. With `settings`, the filters, the span, the station rule
  // and the dialog's size are remembered per study.
  AddFromCatalogDialog(const study::Study &study, QWidget *parent = nullptr,
                       const QStringList &nearStations = {},
                       QSettings *settings = nullptr);
  // The cases to add, in the order of the list. One whose `id` is set
  // replaces the case of that id (the earthquake was already in the study
  // and "Replace" was chosen); the others are new.
  QList<study::Case> results() const { return results_; }
  // The first of them, for a single choice.
  std::optional<study::Case> result() const {
    return results_.isEmpty() ? std::nullopt
                              : std::optional<study::Case>(results_.first());
  }
  // "Add and keep open" hands the chosen cases to this instead of closing;
  // it returns whether they were added. Without it the button is hidden.
  void setAdder(std::function<bool(const QList<study::Case> &)> adder);
  void accept() override;
  void done(int r) override;

private:
  struct Choice {
    int event = -1;     // index into events_
    study::Case c;      // name, span and stations as they would be added
    QString existingId; // the case this earthquake already is, or empty
    QStringList unknown; // station codes typed that the list does not have
  };
  void refill();
  void eventChosen();
  void updateNotes(); // warnings under the tables, and the Add button
  void updatePreview(); // the map beside the tables
  void fillPlan();      // several chosen: one row each
  void planEdited(int row, int column);
  void addAndStay();
  void remember() const;
  QList<int> chosenRows() const; // rows of the event table, top to bottom
  QList<Choice> choices() const; // what would be added, before `dup_`
  QList<study::Case> collect() const; // the same after it
  QString existingCase(const analysis::Event &e) const;
  QList<study::StationRef> stationsNear(const analysis::Event &e,
                                        double *radius = nullptr) const;
  bool coveredByMine(const analysis::Event &e) const;

  const study::Study &study_;
  QSettings *settings_;
  QString key_; // settings prefix of this study
  bool builtIn_; // the events are the built-in list, not the study's catalog
  QStringList haveData_; // stations found in the study's waveform folders
  std::vector<analysis::Station> haveStations_; // the same, with places
  QHash<QString, QPair<QDate, QDate>> haveRange_; // their data, first to last
  QPushButton *mine_;
  QPushButton *ok_, *keep_ = nullptr, *more_;
  QLabel *range_, *stationNote_, *warning_, *added_;
  std::vector<analysis::Event> events_;
  std::vector<analysis::Station> stations_;
  std::vector<int> shown_; // row -> index into events_
  int passing_ = 0;        // how many pass the filters (shown_ may be cut)
  int limit_ = 500;        // how many rows are shown at most
  int sortColumn_ = 1;     // magnitude
  bool sortDescending_ = true;
  bool keepChosenOnce_ = false; // the next refill keeps the selection
  // The "near stations" filter: the codes typed, resolved against the list.
  std::vector<analysis::Station> filterStations_;
  QLineEdit *nearCodes_;
  QDoubleSpinBox *nearKm_;
  QLabel *nearNote_;
  QList<study::StationRef> near_;
  QList<study::Case> results_;
  std::function<bool(const QList<study::Case> &)> adder_;
  QDoubleSpinBox *minMag_, *maxDepth_;
  QDateEdit *from_, *to_;
  QCheckBox *covered_;
  QLineEdit *place_;
  QTableWidget *table_;
  QLabel *count_;
  QLineEdit *name_;
  QSpinBox *span_, *nearestN_;
  QDoubleSpinBox *maxKm_;
  QStackedWidget *lower_;
  QTableWidget *stationTable_, *planTable_;
  QComboBox *dup_;
  QLabel *dupLabel_;
  // Several chosen: what the user changed in the plan, by event.
  QHash<int, QString> planNames_;
  QHash<int, QStringList> planStations_;
  bool fillingPlan_ = false;
  CasePreview *preview_ = nullptr;
};

class ReferenceDateDialog : public QDialog {
  Q_OBJECT
public:
  ReferenceDateDialog(const study::Study &study, QWidget *parent = nullptr);
  std::optional<study::Case> result() const { return result_; }
  void accept() override;

private:
  void placeChanged(); // the candidates and what is ticked to start with
  void updateNotes();
  study::Case draft() const; // the case as the fields have it, no stations

  const study::Study &study_;
  std::optional<study::Case> result_;
  std::vector<analysis::Station> stations_;
  QList<study::StationRef> near_;
  QLineEdit *name_;
  QDateTimeEdit *time_;
  QComboBox *place_;
  QDoubleSpinBox *lat_, *lon_;
  QSpinBox *span_;
  QTableWidget *stationTable_;
  QLabel *warning_;
  CasePreview *preview_ = nullptr;
};

} // namespace skg
