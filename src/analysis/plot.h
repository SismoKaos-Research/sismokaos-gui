// SCAP's metric figures drawn with QPainter: a day, all days end to end,
// and a window of days centred on the main event. The metric is a filled
// trace (red above zero, blue below) on the left axis; earthquakes sit on a
// right magnitude axis, one marker shape per distance band, coloured by
// depth, labelled from M5. Renders to a QImage or an SVG file.
#pragma once

#include <QColor>
#include <QDate>
#include <QImage>
#include <QRectF>
#include <QSize>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

#include "analysis/events.h"
#include "analysis/series.h"

class QPainter;

namespace skg::analysis {

struct PlotStyle {
  QString lang = QStringLiteral("tr");
  QString station, channel;     // for the title
  QString metricName;           // defaults to metricTitle(column)
  std::optional<YRange> yRange; // defaults to scapRange or autoRange
  double minPlotMag = 0.0;      // events below are not drawn
  double labelMag = 5.0;        // events from here get a text label
  bool showDepthBar = true;
  bool showDistanceLegend = true;
  bool showAreaLegend = true;
  QColor positive{0xD9, 0x55, 0x55}, negative{0x40, 0x73, 0xD9};
};

// Depth colour map, 0-80 km (light yellow -> orange -> red -> indigo).
QColor depthColor(double depthKm);

// Draws into `rect` of a painter already set up for the device.
void drawDaily(QPainter &p, const QRectF &rect, const MetricSeries &s,
               const QDate &day, const std::vector<EventAtStation> &events,
               const PlotStyle &style);
void drawCombined(QPainter &p, const QRectF &rect, const MetricSeries &s,
                  const std::vector<QDate> &days,
                  const std::vector<EventAtStation> &events,
                  const std::optional<EventAtStation> &mainEvent,
                  const PlotStyle &style);
// `days` from centredDays(); the main event is marked with a vertical line.
void drawCentred(QPainter &p, const QRectF &rect, const MetricSeries &s,
                 const std::vector<QDate> &days,
                 const std::vector<EventAtStation> &events,
                 const EventAtStation &mainEvent, const PlotStyle &style);

using DrawFn = std::function<void(QPainter &, const QRectF &)>;
// A white image of `size` pixels with `draw` applied.
QImage renderImage(const QSize &size, const DrawFn &draw);
// An SVG file of `size` points. Returns false when the file cannot be written.
bool renderSvg(const QString &path, const QSize &size, const DrawFn &draw);

// SCAP's figure sizes (inches * 150 dpi): daily 8.27x3.1, combined 11x3.1,
// centred 9.5x3.1.
QSize dailySize(int dpi = 150);
QSize combinedSize(int dpi = 150);
QSize centredSize(int dpi = 150);

} // namespace skg::analysis
