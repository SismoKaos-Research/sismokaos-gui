// A widget that paints one of the analysis figures at the widget's size and
// exports it as PNG (at a chosen dpi) or SVG.
#pragma once

#include <QSize>
#include <QWidget>

#include "analysis/plot.h"

namespace skg {

class FigureView : public QWidget {
  Q_OBJECT
public:
  explicit FigureView(QWidget *parent = nullptr);
  // `natural` is the figure's design size; the widget keeps its aspect.
  void setFigure(const analysis::DrawFn &draw, const QSize &natural);
  void clearFigure();
  bool hasFigure() const { return bool(draw_); }
  QSize naturalSize() const { return natural_; }
  // Export at `scale` times the natural size (PNG) or at the natural size
  // (SVG).
  bool exportPng(const QString &path, double scale) const;
  bool exportSvg(const QString &path) const;
  QImage render(double scale) const;

protected:
  void paintEvent(QPaintEvent *e) override;

private:
  analysis::DrawFn draw_;
  QSize natural_{1240, 465};
};

} // namespace skg
