#include "cbi/plots.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <map>

#include "sfc/charts.h"

namespace skg::cbi {

namespace {
QSize inches(double w, double h, int dpi) {
  return QSize(int(w * dpi), int(h * dpi));
}

QImage canvas(const QSize &size) {
  QImage img(size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  return img;
}

void setup(QPainter &p) {
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
}

bool tr(const PlotContext &c) { return c.lang == QLatin1String("tr"); }

// matplotlib's 5% data margins.
std::pair<double, double>
yRange(const std::vector<const std::vector<double> *> &series) {
  double lo = 1e300, hi = -1e300;
  for (const std::vector<double> *s : series)
    if (s)
      for (double v : *s)
        if (std::isfinite(v)) {
          lo = std::min(lo, v);
          hi = std::max(hi, v);
        }
  if (lo > hi)
    return {0.0, 1.0};
  if (hi == lo) {
    lo -= 0.5;
    hi += 0.5;
  }
  const double m = (hi - lo) * 0.05;
  return {lo - m, hi + m};
}

std::vector<int> tickPositions(int n) {
  std::vector<int> ticks;
  if (n <= 0)
    return ticks;
  const int step = std::max(1, n / std::min(15, n));
  for (int i = 0; i < n; i += step)
    ticks.push_back(i);
  if (ticks.back() != n - 1)
    ticks.push_back(n - 1);
  return ticks;
}

struct Figure {
  QImage img;
  QPainter p;
  sfc::Axes ax;
  // The rotated window labels need more room under the plot than the
  // toolkit leaves, so the axes get a shorter rectangle and the x label is
  // drawn along the image's bottom edge.
  static constexpr double kExtraBottom = 0.09;
  Figure(const IndicatorTable &t,
         const std::vector<const std::vector<double> *> &series,
         const PlotContext &ctx)
      : img(canvas(inches(15, 7, ctx.dpi))), p(&img),
        ax((setup(p), p),
           QRectF(0, 0, img.width(), img.height() * (1 - kExtraBottom)),
           -(std::max(1, t.rows() - 1)) * 0.05,
           (std::max(1, t.rows() - 1)) * 1.05, yRange(series).first,
           yRange(series).second) {}
  void xLabel(const QString &text) {
    const QFontMetricsF fm(p.font());
    p.setPen(Qt::black);
    p.drawText(QRectF(ax.plot().left(), img.height() - fm.height() * 1.3,
                      ax.plot().width(), fm.height()),
               Qt::AlignHCenter | Qt::AlignVCenter, text);
  }
};

void windowLines(QPainter &p, const sfc::Axes &ax, int n) {
  QPen pen(QColor(128, 128, 128, 77), 0.5 * ax.unit(), Qt::DotLine);
  p.setPen(pen);
  for (int i = 0; i < n; ++i)
    p.drawLine(QPointF(ax.sx(i), ax.plot().top()),
               QPointF(ax.sx(i), ax.plot().bottom()));
}

void decorate(Figure &f, const IndicatorTable &t, const QString &ylabel,
              const QString &title, const PlotContext &ctx) {
  std::vector<double> at;
  QStringList labels;
  for (int i : tickPositions(t.rows())) {
    at.push_back(i);
    labels << t.xLabel.value(i);
  }
  f.ax.autoYTicks(6);
  f.ax.grid(true, true);
  f.ax.xTicks(at, labels, true);
  f.ax.frame();
  f.ax.labels(QString(), ylabel);
  f.ax.title(title);
  f.xLabel(tr(ctx) ? QStringLiteral("Zaman Penceresi (Başlangıç - Bitiş)")
                   : QStringLiteral("Time window (start - end)"));
}

// A polyline per run of finite values.
void line(QPainter &p, const sfc::Axes &ax, const std::vector<double> &v,
          const QColor &c, double width, Qt::PenStyle style = Qt::SolidLine,
          double markerPx = 0, int markEvery = 1) {
  QPen pen(c, width * ax.unit(), style);
  pen.setCapStyle(Qt::RoundCap);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);
  QPolygonF poly;
  auto flush = [&] {
    if (poly.size() > 1)
      p.drawPolyline(poly);
    else if (poly.size() == 1 && markerPx <= 0)
      p.drawPoint(poly.first());
    poly.clear();
  };
  for (size_t i = 0; i < v.size(); ++i) {
    if (!std::isfinite(v[i])) {
      flush();
      continue;
    }
    poly << ax.px(double(i), v[i]);
  }
  flush();
  if (markerPx > 0) {
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (size_t i = 0; i < v.size();
         i += static_cast<size_t>(std::max(1, markEvery)))
      if (std::isfinite(v[i]))
        p.drawEllipse(ax.px(double(i), v[i]), markerPx * ax.unit(),
                      markerPx * ax.unit());
  }
}

QPainterPath star(const QPointF &c, double r) {
  QPainterPath path;
  for (int k = 0; k < 10; ++k) {
    const double a = -M_PI / 2 + k * M_PI / 5;
    const double rr = k % 2 == 0 ? r : r * 0.45;
    const QPointF pt(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
    if (k == 0)
      path.moveTo(pt);
    else
      path.lineTo(pt);
  }
  path.closeSubpath();
  return path;
}

// Stars at the windows where an M5-6 / M6+ event first appears.
void markEvents(Figure &f, const IndicatorTable &t, const QString &ycol) {
  const std::vector<double> *y = t.column(ycol);
  if (!y)
    return;
  struct Marker {
    const char *col;
    QColor color;
    double radius;
    double font;
  };
  const Marker markers[2] = {
      {"Mmax_obs_first_5_to_6", QColor(0x80, 0x00, 0x80), 7.0, 8.0},
      {"Mmax_obs_first_6_plus", QColor(0xff, 0xa5, 0x00), 8.5, 9.0}};
  for (const Marker &m : markers) {
    const std::vector<double> *mark = t.column(QLatin1String(m.col));
    if (!mark)
      continue;
    for (size_t i = 0; i < mark->size() && i < y->size(); ++i) {
      if (!((*mark)[i] > 0) || !std::isfinite((*y)[i]))
        continue;
      const QPointF c = f.ax.px(double(i), (*y)[i]);
      f.p.setPen(Qt::NoPen);
      f.p.setBrush(m.color);
      f.p.drawPath(star(c, m.radius * f.ax.unit()));
      QFont font = f.p.font();
      font.setPointSizeF(m.font * std::max(0.8, f.ax.unit()));
      font.setBold(true);
      f.p.setFont(font);
      f.p.setPen(m.color);
      f.p.drawText(
          QPointF(c.x() + 3 * f.ax.unit(), c.y() - 3 * f.ax.unit()),
          QStringLiteral(" M%1").arg(QString::number((*mark)[i], 'f', 1)));
    }
  }
}

QString rawLabel(const PlotContext &c) {
  return tr(c) ? QStringLiteral("Ham Veri") : QStringLiteral("Raw");
}

QString titleSuffix(const PlotContext &c) {
  return tr(c) ? QStringLiteral(" (Pencere: %1, Adım: %2)")
                     .arg(c.windowLabel, c.stepLabel)
               : QStringLiteral(" (window: %1, step: %2)")
                     .arg(c.windowLabel, c.stepLabel);
}
} // namespace

QImage indicatorFigure(const IndicatorTable &t, const IndicatorInfo &ind,
                       const PlotContext &ctx) {
  const std::vector<double> *v = t.column(ind.column);
  Figure f(t, {v}, ctx);
  windowLines(f.p, f.ax, t.rows());
  if (v)
    line(f.p, f.ax, *v, ind.color, 1.5, Qt::SolidLine, 2.0);
  markEvents(f, t, ind.column);
  const QString label = ind.label(ctx.lang);
  decorate(f, t, label, label + titleSuffix(ctx), ctx);
  f.ax.legend({qMakePair(label, ind.color)});
  return f.img;
}

QImage movingAverageFigure(const IndicatorTable &t, const IndicatorInfo &ind,
                           int window, const PlotContext &ctx) {
  const QString maCol = ind.column + QStringLiteral("_MA%1").arg(window);
  const std::vector<double> *v = t.column(ind.column), *ma = t.column(maCol);
  Figure f(t, {v, ma}, ctx);
  windowLines(f.p, f.ax, t.rows());
  QColor faint = ind.color;
  faint.setAlphaF(0.5);
  if (v)
    line(f.p, f.ax, *v, faint, 2.0, Qt::SolidLine, 1.5);
  if (ma)
    line(f.p, f.ax, *ma, ind.color, 2.5);
  markEvents(f, t, maCol);
  const QString label = ind.label(ctx.lang);
  decorate(f, t, label,
           QStringLiteral("%1 + MA-%2").arg(label).arg(window) +
               titleSuffix(ctx),
           ctx);
  f.ax.legend(
      {qMakePair(QStringLiteral("%1 (%2)").arg(label, rawLabel(ctx)), faint),
       qMakePair(QStringLiteral("%1 (MA-%2)").arg(label).arg(window),
                 ind.color)});
  return f.img;
}

QImage combinedMovingAverageFigure(const IndicatorTable &t,
                                   const IndicatorInfo &ind,
                                   const std::vector<int> &windows,
                                   const PlotContext &ctx) {
  std::vector<const std::vector<double> *> series{t.column(ind.column)};
  QStringList maCols;
  for (int w : windows) {
    maCols << ind.column + QStringLiteral("_MA%1").arg(w);
    series.push_back(t.column(maCols.last()));
  }
  Figure f(t, series, ctx);
  windowLines(f.p, f.ax, t.rows());
  const QColor grey(128, 128, 128, 102);
  if (series[0])
    line(f.p, f.ax, *series[0], grey, 2.0, Qt::SolidLine, 1.5);
  const Qt::PenStyle styles[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
  QList<QPair<QString, QColor>> legend{qMakePair(rawLabel(ctx), grey)};
  QStringList names;
  for (size_t i = 0; i < windows.size(); ++i) {
    QColor c = ind.color;
    c.setAlphaF(1.0 - 0.05 * double(i % 3));
    if (series[i + 1])
      line(f.p, f.ax, *series[i + 1], c, 3.0 + 0.1 * double(i % 3),
           styles[i % 3], 2.5, 3);
    names << QStringLiteral("MA-%1").arg(windows[i]);
    legend << qMakePair(names.last(), c);
  }
  markEvents(f, t, ind.column);
  const QString label = ind.label(ctx.lang);
  decorate(
      f, t, label,
      QStringLiteral("%1 + %2").arg(label, names.join(QStringLiteral(", "))) +
          titleSuffix(ctx),
      ctx);
  f.ax.legend(legend);
  return f.img;
}

QImage bollingerFigure(const IndicatorTable &t, const IndicatorInfo &ind,
                       int window, double k, const PlotContext &ctx) {
  const QString c = ind.column;
  const std::vector<double> *v = t.column(c),
                            *ma = t.column(c + QStringLiteral("_BB_MA")),
                            *up = t.column(c + QStringLiteral("_BB_Upper")),
                            *lo = t.column(c + QStringLiteral("_BB_Lower"));
  Figure f(t, {v, ma, up, lo}, ctx);
  windowLines(f.p, f.ax, t.rows());
  if (up && lo) {
    QPainterPath band;
    bool open = false;
    QPolygonF top, bottom;
    auto flush = [&] {
      if (top.size() > 1) {
        QPolygonF poly = top;
        for (int i = bottom.size() - 1; i >= 0; --i)
          poly << bottom[i];
        band.addPolygon(poly);
      }
      top.clear();
      bottom.clear();
      open = false;
    };
    for (size_t i = 0; i < up->size() && i < lo->size(); ++i) {
      if (!std::isfinite((*up)[i]) || !std::isfinite((*lo)[i])) {
        flush();
        continue;
      }
      open = true;
      top << f.ax.px(double(i), (*up)[i]);
      bottom << f.ax.px(double(i), (*lo)[i]);
    }
    if (open)
      flush();
    f.p.setPen(Qt::NoPen);
    f.p.setBrush(QColor(128, 128, 128, 38));
    f.p.drawPath(band);
  }
  QColor faint = ind.color;
  faint.setAlphaF(0.6);
  const QColor red(0xff, 0, 0, 178), green(0, 0x80, 0, 178);
  if (v)
    line(f.p, f.ax, *v, faint, 1.5, Qt::SolidLine, 1.5);
  if (ma)
    line(f.p, f.ax, *ma, ind.color, 2.5);
  if (up)
    line(f.p, f.ax, *up, red, 1.5, Qt::DashLine);
  if (lo)
    line(f.p, f.ax, *lo, green, 1.5, Qt::DashLine);
  markEvents(f, t, c);
  const QString label = ind.label(ctx.lang);
  decorate(f, t, label,
           label + QStringLiteral(" + Bollinger Bands") + titleSuffix(ctx),
           ctx);
  const QString kText = QString::number(k, 'g');
  f.ax.legend(
      {qMakePair(QStringLiteral("%1 (%2)").arg(label, rawLabel(ctx)), faint),
       qMakePair(tr(ctx) ? QStringLiteral("BB Orta (MA-%1)").arg(window)
                         : QStringLiteral("BB middle (MA-%1)").arg(window),
                 ind.color),
       qMakePair(tr(ctx) ? QStringLiteral("Üst Band (+%1σ)").arg(kText)
                         : QStringLiteral("Upper band (+%1σ)").arg(kText),
                 red),
       qMakePair(tr(ctx) ? QStringLiteral("Alt Band (-%1σ)").arg(kText)
                         : QStringLiteral("Lower band (-%1σ)").arg(kText),
                 green)});
  return f.img;
}

QImage annualCountsFigure(const Catalog &catalog, const PlotContext &ctx) {
  std::map<int, int> counts;
  for (const analysis::Event &e : catalog.events)
    counts[toDateTime(e.epoch).date().year()]++;
  QImage img = canvas(inches(12, 6, ctx.dpi));
  QPainter p(&img);
  setup(p);
  int hi = 1;
  for (const auto &[y, c] : counts)
    hi = std::max(hi, c);
  const int n = int(counts.size());
  sfc::Axes ax(p, QRectF(0, 0, img.width(), img.height()), -0.6,
               std::max(0, n - 1) + 0.6, 0, hi * 1.1);
  ax.autoYTicks(6);
  ax.grid(false, true);
  std::vector<double> at;
  QStringList labels;
  int i = 0;
  for (const auto &[year, count] : counts) {
    at.push_back(i);
    labels << QString::number(year);
    const QRectF bar(ax.sx(i - 0.4), ax.sy(count),
                     ax.sx(i + 0.4) - ax.sx(i - 0.4), ax.sy(0) - ax.sy(count));
    p.setPen(QPen(Qt::black, 0.8 * ax.unit()));
    p.setBrush(QColor(0x41, 0x69, 0xe1));
    p.drawRect(bar);
    p.setPen(Qt::black);
    p.drawText(QRectF(bar.left() - 40 * ax.unit(), bar.top() - 18 * ax.unit(),
                      bar.width() + 80 * ax.unit(), 16 * ax.unit()),
               Qt::AlignHCenter | Qt::AlignBottom, QString::number(count));
    ++i;
  }
  ax.xTicks(at, labels, n > 12);
  ax.frame();
  ax.labels(tr(ctx) ? QStringLiteral("Yıl") : QStringLiteral("Year"),
            tr(ctx) ? QStringLiteral("Toplam Olay Sayısı")
                    : QStringLiteral("Events"));
  ax.title(tr(ctx) ? QStringLiteral("Yıllara Göre Toplam Olay Sayısı")
                   : QStringLiteral("Events per year"));
  return img;
}

} // namespace skg::cbi
