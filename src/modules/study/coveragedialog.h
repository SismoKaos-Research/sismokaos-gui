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
// The study's catalog, or the bundled list of larger earthquakes when the
// study names none or it cannot be read. `source` says which, for the user.
std::vector<analysis::Event> coverageEvents(const study::Study &s,
                                            QString *source);

class CoverageDialog : public QDialog {
  Q_OBJECT
public:
  explicit CoverageDialog(AppContext ctx, QWidget *parent = nullptr);
  CoverageMap *coverage() const { return coverage_; }

private:
  void fill();

  AppContext ctx_;
  QLabel *whichLabel_, *note_;
  QComboBox *which_;
  CoverageMap *coverage_;
  std::vector<analysis::Event> events_;
  QString source_;
};

} // namespace skg
