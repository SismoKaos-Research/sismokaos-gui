#include "analysis/grid.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace skg::analysis {

QImage composeGrid(const GridSpec &spec,
                   const std::function<QImage(int, int)> &cell) {
  const int rows = spec.rowLabels.size(), cols = spec.colLabels.size();
  if (rows == 0 || cols == 0)
    return {};
  // Cell size: given, or SCAP's scale of the first non-empty figure by the
  // cell count, shrunk until the whole image fits in maxPixels.
  int cw = spec.cellWidth, ch = spec.cellHeight;
  std::vector<QImage> cells(size_t(rows * cols));
  QSize sample;
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      cells[size_t(r * cols + c)] = cell(r, c);
      if (sample.isEmpty() && !cells[size_t(r * cols + c)].isNull())
        sample = cells[size_t(r * cols + c)].size();
    }
  if (sample.isEmpty())
    sample = QSize(1240, 465);
  const int total = rows * cols;
  double scale = total > 200   ? 0.20
                 : total > 100 ? 0.27
                 : total > 50  ? 0.38
                               : 0.50;
  const int leftMargin = 150, topMargin = 80, spacing = 3;
  if (cw <= 0 || ch <= 0) {
    cw = int(sample.width() * scale);
    ch = int(sample.height() * scale);
  }
  auto totalPixels = [&] {
    return qint64(leftMargin + cols * (cw + spacing)) *
           qint64(topMargin + rows * (ch + spacing));
  };
  while (totalPixels() > spec.maxPixels && cw > 40) {
    cw = int(cw * 0.8);
    ch = int(ch * 0.8);
  }
  QImage out(leftMargin + cols * (cw + spacing),
             topMargin + rows * (ch + spacing),
             QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::white);
  QPainter p(&out);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.setRenderHint(QPainter::TextAntialiasing);
  QFont f = p.font();
  f.setPointSize(std::clamp(ch / 8, 7, 14));
  p.setFont(f);
  QFont bold = f;
  bold.setBold(true);
  bold.setPointSize(f.pointSize() + 3);
  p.setPen(Qt::black);
  p.setFont(bold);
  p.drawText(QRect(leftMargin, 0, out.width() - leftMargin, topMargin / 2),
             Qt::AlignCenter, spec.title);
  p.setFont(f);
  for (int c = 0; c < cols; ++c)
    p.drawText(QRect(leftMargin + c * (cw + spacing), topMargin / 2, cw,
                     topMargin / 2),
               Qt::AlignCenter, spec.colLabels[c]);
  for (int r = 0; r < rows; ++r) {
    const int y = topMargin + r * (ch + spacing);
    p.setFont(bold);
    p.drawText(QRect(0, y, leftMargin - 6, ch),
               Qt::AlignRight | Qt::AlignVCenter, spec.rowLabels[r]);
    p.setFont(f);
    for (int c = 0; c < cols; ++c) {
      const QRect target(leftMargin + c * (cw + spacing), y, cw, ch);
      const QImage &img = cells[size_t(r * cols + c)];
      if (img.isNull()) {
        p.fillRect(target, QColor(0xf2, 0xf2, 0xf2));
        p.setPen(QColor(0x90, 0x90, 0x90));
        p.drawText(target, Qt::AlignCenter, QStringLiteral("—"));
        p.setPen(Qt::black);
      } else
        p.drawImage(target, img);
    }
  }
  return out;
}

} // namespace skg::analysis
