// Small multiples: a grid of figures with row and column labels, as SCAP's
// station comparison grids (stations down, days across).
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

namespace skg::analysis {

struct GridSpec {
  QStringList rowLabels, colLabels;
  QString title;
  int cellWidth = 0,
      cellHeight = 0; // pixels; 0 = scaled by SCAP's rule from the cell count
  int maxPixels = 100'000'000;
};

// `cell(row, col)` returns the figure of one cell, or a null image for an
// empty cell. The result is white with labels in the margins.
QImage composeGrid(const GridSpec &spec,
                   const std::function<QImage(int row, int col)> &cell);

} // namespace skg::analysis
