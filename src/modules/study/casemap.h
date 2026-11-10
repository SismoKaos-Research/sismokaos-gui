// The maps of the study pages, all on the shared GeoMap:
//  - CaseMap, the overview on the Cases page: every case of the study, the
//    selected one in its colour with its zone and stations;
//  - CasePreview, in the add-case dialog: the earthquakes listed, then the
//    chosen one with its zone and candidate stations;
//  - caseMapContent(), which the Compare page's "Where" panel also uses.
#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

#include "analysis/events.h"
#include "study/study.h"
#include "studyui/coveragemap.h"
#include "studyui/geomap.h"

class QLabel;
class QPushButton;

namespace skg {

class Theme;

struct CaseMapSpec {
  // The case drawn in colour with its zone and stations; empty: every case
  // in its own colour (the comparison).
  QString selectedId;
  // Stations offered for the selected case that it does not use.
  QList<study::StationRef> candidates;
  // Whether the study has waveforms of a station; unset: not shown.
  std::function<bool(const study::StationRef &)> hasData;
  bool stations = true;
  // Colour and label per case id; a case without one takes the series
  // colour of its position and its name.
  QHash<QString, QColor> colors;
  QHash<QString, QString> labels;
};

// Marker ids are "case:<id>" and "station:<code>". Fault files that could
// not be read are named in `problems`.
GeoContent caseMapContent(const study::Study &s, const Theme &theme,
                          const CaseMapSpec &spec,
                          QStringList *problems = nullptr);

class CaseMap : public QWidget {
  Q_OBJECT
public:
  explicit CaseMap(Theme *theme, QWidget *parent = nullptr);

  void setStudy(const study::Study &s, const CaseMapSpec &spec);
  void clear();
  GeoMap *map() const { return map_; }

signals:
  void caseClicked(const QString &caseId);
  void stationClicked(const QString &code);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void updateLegend();

  Theme *theme_;
  GeoMap *map_;
  GeoLegend *legend_;
  QLabel *title_, *hint_, *note_;
  QPushButton *fitAll_, *fitCase_;
  bool hasSelection_ = false, hasReference_ = false, hasData_ = false,
       hasCandidates_ = false, hasOutside_ = false;
  QString lastSelected_;
};

// One earthquake of the add-case dialog's list.
struct PreviewEvent {
  int row = 0; // reported on a click
  double lat = 0, lon = 0, magnitude = 0;
  QString tip;
  QString label; // shown beside it when it is selected
};

class CasePreview : public QWidget {
  Q_OBJECT
public:
  // Uses the application's theme; without one the widget stays empty.
  explicit CasePreview(QWidget *parent = nullptr);

  // Nothing chosen yet: the earthquakes of the list, and, when the list is
  // limited to those near some stations, what those stations cover.
  // `selected` rows are drawn strong, in colour and with their name: the
  // earthquakes about to be added when several are chosen at once.
  void setList(const QList<PreviewEvent> &events,
               const QList<CoverageStation> &filterStations, double filterKm,
               const QSet<int> &selected = {});
  // The chosen earthquake: its zone, the candidate stations and the ticked
  // ones. A ticked station beyond the zone is flagged.
  void setCase(const study::Case &c, const QList<study::StationRef> &candidates,
               const QStringList &ticked,
               const std::function<bool(const study::StationRef &)> &hasData,
               const QList<PreviewEvent> &others = {});
  // The warning shown under the map, or empty.
  QString warning() const;
  GeoMap *map() const { return map_; }

signals:
  void eventClicked(int row);
  void stationClicked(const QString &code);

private:
  Theme *theme_;
  GeoMap *map_ = nullptr;
  GeoLegend *legend_ = nullptr;
  QLabel *warning_ = nullptr, *hint_ = nullptr;
  bool caseMode_ = false;
};

} // namespace skg
