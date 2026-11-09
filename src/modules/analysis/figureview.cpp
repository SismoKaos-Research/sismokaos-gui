#include "modules/analysis/figureview.h"

#include <QPaintEvent>
#include <QPainter>

namespace skg {

FigureView::FigureView(QWidget *parent) : QWidget(parent) {
  setMinimumHeight(160);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::white);
  setPalette(pal);
}

void FigureView::setFigure(const analysis::DrawFn &draw, const QSize &natural) {
  draw_ = draw;
  natural_ = natural;
  update();
}

void FigureView::clearFigure() {
  draw_ = nullptr;
  update();
}

QImage FigureView::render(double scale) const {
  if (!draw_)
    return {};
  return analysis::renderImage(natural_ * scale, draw_);
}

bool FigureView::exportPng(const QString &path, double scale) const {
  const QImage img = render(scale);
  return !img.isNull() && img.save(path, "PNG");
}

bool FigureView::exportSvg(const QString &path) const {
  return draw_ && analysis::renderSvg(path, natural_, draw_);
}

void FigureView::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.fillRect(rect(), Qt::white);
  if (!draw_) {
    p.setPen(Qt::gray);
    p.drawText(rect(), Qt::AlignCenter, tr("No figure yet."));
    return;
  }
  // Fit the natural aspect into the widget.
  const double s = std::min(double(width()) / natural_.width(),
                            double(height()) / natural_.height());
  const QSizeF size(natural_.width() * s, natural_.height() * s);
  const QRectF target(
      QPointF((width() - size.width()) / 2, (height() - size.height()) / 2),
      size);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  draw_(p, target);
}

} // namespace skg
