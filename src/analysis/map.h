// SCAP's publication map, drawn with QPainter: the station, the events of
// the period coloured by distance band and sized by magnitude, the main
// event, distance rings, the active fault lines and province boundaries
// when available, graticule, scale bar and north arrow.
#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

#include "analysis/events.h"
#include "analysis/faults.h"
#include "analysis/shapefile.h"

class QPainter;

namespace skg::analysis {

struct MapSpec {
  QString lang = QStringLiteral("tr");
  Station station;
  std::vector<EventAtStation> events;
  std::optional<EventAtStation> mainEvent;
  std::vector<double> ringsKm{100, 300,
                              500}; // distance rings around the station
  double extentKm =
      0; // half-width of the view; 0 = fit rings and events, plus padding
  double paddingKm = 50;
  const FaultIndex *faults = nullptr;
  const ShapeLayer *provinces = nullptr; // polygon shapes
  bool showFaults = true, showProvinces = true, showGraticule = true;
  QString title;
};

// The view rectangle in (lon, lat) that the map will show.
QRectF mapExtent(const MapSpec &spec);
void drawMap(QPainter &p, const QRectF &rect, const MapSpec &spec);
// SCAP's band colours: red, orange, yellow, green.
QColor bandColor(int band);

} // namespace skg::analysis
