// What a set of stations covers: the area within a radius of any of them,
// and which earthquakes of a catalog fall inside it. The function is the
// rule; the widget draws it and says the count.
#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include <vector>

#include "analysis/events.h"

class QDoubleSpinBox;
class QLabel;

namespace skg {

class GeoLegend;
class GeoMap;
class Theme;

struct CoverageStation {
  QString code;
  double lat = 0, lon = 0;
};

struct CoverageResult {
  // Indices into the events given, of those with magnitude >= minMag.
  std::vector<int> covered, missed;
  int considered() const { return int(covered.size() + missed.size()); }
};

// An earthquake is covered when its epicentre lies within radiusKm of at
// least one station (great-circle distance).
CoverageResult stationCoverage(const std::vector<analysis::Event> &events,
                               const QList<CoverageStation> &stations,
                               double radiusKm, double minMag);

class CoverageMap : public QWidget {
  Q_OBJECT
public:
  explicit CoverageMap(Theme *theme, QWidget *parent = nullptr);

  void setStations(const QList<CoverageStation> &stations);
  // `source` says where the earthquakes come from ("the study's catalog").
  void setEvents(const std::vector<analysis::Event> &events,
                 const QString &source);
  void setRadiusKm(double km);
  double radiusKm() const;
  void setMinMagnitude(double mag);
  double minMagnitude() const;
  // "12 of 40 earthquakes of M ≥ 5.0 lie within 150 km of a selected
  // station."
  QString summary() const;
  CoverageResult result() const { return result_; }
  GeoMap *map() const { return map_; }

signals:
  void radiusChanged(double km);
  void stationClicked(const QString &code);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void rebuild(bool refit);

  Theme *theme_;
  QList<CoverageStation> stations_;
  std::vector<analysis::Event> events_;
  QString source_;
  CoverageResult result_;
  GeoMap *map_;
  GeoLegend *legend_;
  QLabel *radiusLabel_, *magLabel_, *summary_;
  QDoubleSpinBox *radius_, *minMag_;
};

} // namespace skg
