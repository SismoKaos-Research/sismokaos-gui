// The map of the Data page: every station the portal lists as a faint dot,
// the stations chosen for download as labelled triangles and, when the page
// was opened for a case, its epicentre and catalog zone. A click on a
// station adds it to the selection or takes it out.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "tdvms/catalog.h"

class QLabel;

namespace skg {

class GeoLegend;
class GeoMap;
class Theme;

class StationPickMap : public QWidget {
  Q_OBJECT
public:
  explicit StationPickMap(Theme *theme, QWidget *parent = nullptr);

  // The stations offered (the portal's list).
  void setStations(const QList<tdvms::CatalogStation> &stations);
  // The stations chosen; `current` is the one the forms are on.
  void setSelected(const QStringList &codes, const QString &current);
  QStringList selected() const { return selected_; }
  // The case the downloads are for; a radius of 0 draws no zone.
  void setCase(const QString &name, double lat, double lon, double zoneKm);
  void clearCase();
  GeoMap *map() const { return map_; }
  // The codes chosen that the portal's list does not hold.
  QStringList unknown() const;

signals:
  // A station was clicked: it should join the selection, or leave it.
  void toggled(const QString &code);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void rebuild(bool refit);

  Theme *theme_;
  QList<tdvms::CatalogStation> stations_;
  QStringList selected_;
  QString current_;
  bool hasCase_ = false;
  QString caseName_;
  double caseLat_ = 0, caseLon_ = 0, caseZoneKm_ = 0;
  GeoMap *map_;
  GeoLegend *legend_;
  QLabel *summary_;
};

} // namespace skg
