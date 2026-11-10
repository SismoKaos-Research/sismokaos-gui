// "What do my stations cover?": the stations the study has waveforms of (or,
// before any are imported, the stations its cases use) on a map, the area
// within a radius of them, and the earthquakes of the catalog inside and
// outside it.
#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <vector>

#include "analysis/events.h"
#include "core/module.h"
#include "study/study.h"
#include "studyui/coveragemap.h"

class QComboBox;
class QLabel;

namespace skg {

// The stations the waveform folders hold, with the coordinates the station
// list or the study gives them; codes with no coordinates go to `without`.
QList<CoverageStation> stationsWithWaveforms(const study::Study &s,
                                             QStringList *without = nullptr);
// The stations ticked in any case.
QList<CoverageStation> stationsOfCases(const study::Study &s);
// The study's catalog, or the built-in list of larger earthquakes when the
// study names none or it cannot be read. `source` says which, for the user.
// Reads the file: call it off the GUI thread for a large catalog.
std::vector<analysis::Event> coverageEvents(const study::Study &s,
                                            QString *source);

class CoverageDialog : public QDialog {
  Q_OBJECT
public:
  // `chosen`: a set of stations to start on (the rows selected in the
  // Waveforms dialog); it is offered as a third choice.
  explicit CoverageDialog(AppContext ctx, QWidget *parent = nullptr,
                          const QStringList &chosen = {});
  CoverageMap *coverage() const { return coverage_; }
  // The catalog is read on a worker thread; true until it is in.
  bool loading() const { return loading_; }

private:
  void fill();

  AppContext ctx_;
  QLabel *whichLabel_, *note_;
  QComboBox *which_;
  CoverageMap *coverage_;
  std::vector<analysis::Event> events_;
  QString source_;
  QStringList chosen_;
  bool loading_ = false;
};

} // namespace skg
