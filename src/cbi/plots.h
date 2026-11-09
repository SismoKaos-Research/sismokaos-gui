// The indicator figures of the originals, drawn with the sfc chart toolkit:
// one indicator over the windows with stars where M5-6 / M6+ events first
// appear, the same with a trailing moving average, every moving average on
// one figure, Bollinger bands, and the annual event counts. Port of
// indicators-forecast's plotting.py (15 x 7 in; the dpi is a parameter,
// the originals used 300).
#pragma once

#include <QImage>
#include <QString>

#include <vector>

#include "cbi/catalog.h"
#include "cbi/indicators.h"
#include "cbi/table.h"

namespace skg::cbi {

struct PlotContext {
  QString lang = QStringLiteral("tr");
  QString windowLabel, stepLabel; // "12 Ay", "1 Ay": for the titles
  int dpi = 100;
};

QImage indicatorFigure(const IndicatorTable &table, const IndicatorInfo &ind,
                       const PlotContext &ctx);
QImage movingAverageFigure(const IndicatorTable &table,
                           const IndicatorInfo &ind, int window,
                           const PlotContext &ctx);
QImage combinedMovingAverageFigure(const IndicatorTable &table,
                                   const IndicatorInfo &ind,
                                   const std::vector<int> &windows,
                                   const PlotContext &ctx);
QImage bollingerFigure(const IndicatorTable &table, const IndicatorInfo &ind,
                       int window, double k, const PlotContext &ctx);
QImage annualCountsFigure(const Catalog &catalog, const PlotContext &ctx);

} // namespace skg::cbi
