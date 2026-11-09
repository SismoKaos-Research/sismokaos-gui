#include "analysis/plot.h"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSvgGenerator>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace skg::analysis {

namespace {

struct Mark {
  double x; // plot units (minutes)
  double mag, depthKm, km;
  int band;
};

QString tr(const QString &lang, const char *trText, const char *enText) {
  return lang == QLatin1String("tr") ? QString::fromUtf8(trText)
                                     : QString::fromUtf8(enText);
}

double dayStart(const QDate &d) {
  return double(
             QDateTime(d, QTime(0, 0), QTimeZone::utc()).toMSecsSinceEpoch()) /
         1000.0;
}

QDate dayOf(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(epoch * 1000.0)),
                                        QTimeZone::utc())
      .date();
}

QString dateLabel(const QDate &d, const QString &lang) {
  return QStringLiteral("%1 %2 %3")
      .arg(d.day(), 2, 10, QLatin1Char('0'))
      .arg(monthName(d.month(), lang))
      .arg(d.year());
}

void drawMarker(QPainter &p, const QPointF &c, double r, int band) {
  QPainterPath path;
  switch (band) {
  case 1: // triangle
    path.moveTo(c.x(), c.y() - r * 1.15);
    path.lineTo(c.x() + r * 1.1, c.y() + r * 0.8);
    path.lineTo(c.x() - r * 1.1, c.y() + r * 0.8);
    path.closeSubpath();
    break;
  case 2: // square
    path.addRect(QRectF(c.x() - r * 0.9, c.y() - r * 0.9, r * 1.8, r * 1.8));
    break;
  case 3: { // star
    for (int i = 0; i < 10; ++i) {
      const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
      const double rr = (i % 2 == 0) ? r * 1.4 : r * 0.6;
      const QPointF q(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
      if (i == 0)
        path.moveTo(q);
      else
        path.lineTo(q);
    }
    path.closeSubpath();
    break;
  }
  default:
    path.addEllipse(c, r, r);
  }
  p.drawPath(path);
}

// The frame: axes, grid, trace, events, legends. x in [0, xMax] minutes.
struct Frame {
  const PlotStyle &style;
  QRectF outer;
  QRectF plot; // the axes area
  double xMax;
  YRange y;
  QFont font;
  double px; // pixels per "point" of the style, for scaling fonts and lines

  double sx(double x) const { return plot.left() + x / xMax * plot.width(); }
  double sy(double v) const {
    return plot.bottom() - (v - y.min) / (y.max - y.min) * plot.height();
  }
  // The right axis: 0..8 magnitudes with its zero on the left axis' zero.
  double rMin() const { return y.max != 0 ? y.min * 8.0 / y.max : -8.0; }
  double syMag(double m) const {
    return plot.bottom() - (m - rMin()) / (8.0 - rMin()) * plot.height();
  }
};

Frame makeFrame(QPainter &p, const QRectF &rect, const PlotStyle &style,
                double xMax, const YRange &y) {
  Frame f{style, rect, {}, xMax, y, p.font(), rect.height() / 310.0};
  f.font.setPointSizeF(std::max(5.0, 7.0 * f.px * 1.35));
  const double left = rect.width() * 0.07,
               right = rect.width() * (style.showDepthBar ? 0.19 : 0.09);
  const double top = rect.height() * 0.12, bottom = rect.height() * 0.17;
  f.plot = QRectF(rect.left() + left, rect.top() + top,
                  rect.width() - left - right, rect.height() - top - bottom);
  return f;
}

void drawTrace(QPainter &p, const Frame &f, const std::vector<double> &x,
               const std::vector<double> &v) {
  if (x.size() < 2)
    return;
  const double zero = f.sy(std::clamp(0.0, f.y.min, f.y.max));
  QPolygonF pos, neg;
  pos << QPointF(f.sx(x.front()), zero);
  neg << QPointF(f.sx(x.front()), zero);
  for (size_t i = 0; i < x.size(); ++i) {
    const double vv = std::clamp(v[i], f.y.min, f.y.max);
    pos << QPointF(f.sx(x[i]), f.sy(std::max(0.0, vv)));
    neg << QPointF(f.sx(x[i]), f.sy(std::min(0.0, vv)));
  }
  pos << QPointF(f.sx(x.back()), zero);
  neg << QPointF(f.sx(x.back()), zero);
  QColor pc = f.style.positive, nc = f.style.negative;
  pc.setAlphaF(0.65);
  nc.setAlphaF(0.65);
  p.setPen(QPen(f.style.positive.darker(130), 1.0 * f.px));
  p.setBrush(pc);
  p.drawPolygon(pos);
  p.setPen(QPen(f.style.negative.darker(130), 1.0 * f.px));
  p.setBrush(nc);
  p.drawPolygon(neg);
}

void drawAxes(QPainter &p, const Frame &f,
              const std::vector<std::pair<double, QString>> &xTicks,
              const QString &xLabel, const QString &yLabel) {
  p.setFont(f.font);
  const QFontMetricsF fm(f.font);
  p.setPen(QPen(QColor(0x26, 0x26, 0x26, 64), 0.8 * f.px, Qt::DotLine));
  // y ticks: integers when the range allows at least 4, else 7 even steps
  std::vector<double> yt;
  if (int(f.y.max) - int(f.y.min) + 1 >= 4)
    for (int v = int(std::ceil(f.y.min)); v <= int(std::floor(f.y.max)); ++v)
      yt.push_back(v);
  else
    for (int i = 0; i < 7; ++i)
      yt.push_back(f.y.min + (f.y.max - f.y.min) * i / 6.0);
  for (double v : yt)
    p.drawLine(QPointF(f.plot.left(), f.sy(v)),
               QPointF(f.plot.right(), f.sy(v)));
  for (const auto &[x, label] : xTicks)
    p.drawLine(QPointF(f.sx(x), f.plot.top()),
               QPointF(f.sx(x), f.plot.bottom()));
  p.setPen(QPen(Qt::black, 0.8 * f.px));
  p.setBrush(Qt::NoBrush);
  p.drawRect(f.plot);
  const int decimals = (f.y.max - f.y.min) <= 1.0
                           ? 2
                           : ((int(f.y.max) - int(f.y.min) + 1 >= 4) ? 0 : 1);
  for (double v : yt) {
    const QString t = QString::number(v, 'f', decimals);
    p.drawText(QRectF(f.outer.left(), f.sy(v) - fm.height() / 2,
                      f.plot.left() - f.outer.left() - 3 * f.px, fm.height()),
               Qt::AlignRight | Qt::AlignVCenter, t);
  }
  for (const auto &[x, label] : xTicks)
    p.drawText(QRectF(f.sx(x) - 40 * f.px, f.plot.bottom() + 2 * f.px,
                      80 * f.px, fm.height()),
               Qt::AlignHCenter, label);
  // Zero line.
  if (f.y.min < 0 && f.y.max > 0) {
    p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.8 * f.px));
    p.drawLine(QPointF(f.plot.left(), f.sy(0)),
               QPointF(f.plot.right(), f.sy(0)));
  }
  // Axis titles.
  QFont small = f.font;
  small.setPointSizeF(f.font.pointSizeF() * 0.9);
  p.setFont(small);
  p.setPen(Qt::black);
  p.drawText(QRectF(f.plot.left(), f.plot.bottom() + fm.height() + 3 * f.px,
                    f.plot.width(), fm.height()),
             Qt::AlignHCenter, xLabel);
  p.save();
  p.translate(f.outer.left() + fm.height() * 0.9, f.plot.center().y());
  p.rotate(-90);
  p.drawText(QRectF(-f.plot.height() / 2, -fm.height() / 2, f.plot.height(),
                    fm.height()),
             Qt::AlignCenter, yLabel);
  p.restore();
  // Right axis: magnitudes 0..8.
  p.setPen(QPen(Qt::black, 0.8 * f.px));
  for (int m = 0; m <= 8; ++m) {
    const double yy = f.syMag(m);
    if (yy < f.plot.top() - 1 || yy > f.plot.bottom() + 1)
      continue;
    p.drawLine(QPointF(f.plot.right(), yy),
               QPointF(f.plot.right() + 3 * f.px, yy));
    p.drawText(QRectF(f.plot.right() + 4 * f.px, yy - fm.height() / 2,
                      20 * f.px, fm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, QString::number(m));
  }
  p.save();
  p.translate(f.plot.right() + 24 * f.px, f.plot.center().y());
  p.rotate(90);
  p.drawText(
      QRectF(-f.plot.height() / 2, -fm.height() / 2, f.plot.height(),
             fm.height()),
      Qt::AlignCenter,
      tr(f.style.lang, "Deprem Büyüklüğü (M)", "Earthquake Magnitude (M)"));
  p.restore();
  p.setFont(f.font);
}

void drawEvents(QPainter &p, const Frame &f, const std::vector<Mark> &marks) {
  if (marks.empty())
    return;
  double mMin = marks[0].mag, mMax = marks[0].mag;
  for (const Mark &m : marks) {
    mMin = std::min(mMin, m.mag);
    mMax = std::max(mMax, m.mag);
  }
  QFont lf = f.font;
  lf.setPointSizeF(f.font.pointSizeF() * 0.75);
  lf.setBold(true);
  for (const Mark &m : marks) {
    const double size = marks.size() > 1 ? 20 + 32 * (m.mag - mMin) /
                                                    std::max(mMax - mMin, 0.001)
                                         : 32;
    const double r = std::sqrt(size) * 0.55 * f.px;
    const QPointF c(f.sx(m.x), f.syMag(m.mag));
    QColor col = depthColor(m.depthKm);
    col.setAlphaF(0.8);
    p.setPen(QPen(Qt::black, 0.5 * f.px));
    p.setBrush(col);
    drawMarker(p, c, r, m.band);
    if (m.mag >= f.style.labelMag) {
      const QString label = m.km > 0
                                ? QStringLiteral("M%1 (%2km)")
                                      .arg(m.mag, 0, 'f', 1)
                                      .arg(m.km, 0, 'f', 0)
                                : QStringLiteral("M%1").arg(m.mag, 0, 'f', 1);
      p.setFont(lf);
      const QFontMetricsF fm(lf);
      const QRectF box(c.x() - fm.horizontalAdvance(label) / 2 - 2 * f.px,
                       c.y() - r - fm.height() - 3 * f.px,
                       fm.horizontalAdvance(label) + 4 * f.px,
                       fm.height() + 2 * f.px);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 255, 255, 220));
      p.drawRoundedRect(box, 2 * f.px, 2 * f.px);
      p.setPen(QColor(0xB2, 0, 0));
      p.drawText(box, Qt::AlignCenter, label);
      p.setFont(f.font);
    }
  }
}

void drawLegends(QPainter &p, const Frame &f) {
  QFont lf = f.font;
  lf.setPointSizeF(f.font.pointSizeF() * 0.8);
  p.setFont(lf);
  const QFontMetricsF fm(lf);
  const double lh = fm.height() * 1.15;
  if (f.style.showAreaLegend) {
    const QStringList items{tr(f.style.lang, "Pozitif Alan", "Positive Area"),
                            tr(f.style.lang, "Negatif Alan", "Negative Area")};
    double w = 0;
    for (const QString &s : items)
      w = std::max(w, fm.horizontalAdvance(s));
    const QRectF box(f.plot.right() - w - 30 * f.px,
                     f.plot.bottom() - 2 * lh - 8 * f.px, w + 26 * f.px,
                     2 * lh + 6 * f.px);
    p.setPen(QPen(QColor(0xaa, 0xaa, 0xaa), 0.6 * f.px));
    p.setBrush(QColor(0xFE, 0xFE, 0xFE, 230));
    p.drawRect(box);
    for (int i = 0; i < 2; ++i) {
      QColor c = i == 0 ? f.style.positive : f.style.negative;
      c.setAlphaF(0.65);
      p.setPen(QPen(c.darker(130), 0.6 * f.px));
      p.setBrush(c);
      const QRectF sw(box.left() + 4 * f.px,
                      box.top() + 3 * f.px + i * lh + lh * 0.15, 12 * f.px,
                      lh * 0.7);
      p.drawRect(sw);
      p.setPen(Qt::black);
      p.drawText(QRectF(sw.right() + 4 * f.px, box.top() + 3 * f.px + i * lh,
                        w + 4 * f.px, lh),
                 Qt::AlignVCenter, items[i]);
    }
  }
  if (f.style.showDepthBar) {
    const double x = f.plot.right() + 44 * f.px, top = f.plot.top(),
                 h = f.plot.height() * 0.74, w = 7 * f.px;
    QLinearGradient g(x, top + h, x, top);
    for (double d : {0.0, 5.0, 20.0, 35.0, 80.0})
      g.setColorAt(d / 80.0, depthColor(d));
    p.setPen(QPen(Qt::black, 0.5 * f.px));
    p.setBrush(g);
    p.drawRect(QRectF(x, top, w, h));
    QFont tf = lf;
    tf.setPointSizeF(lf.pointSizeF() * 0.85);
    p.setFont(tf);
    for (int d : {0, 5, 10, 15, 20, 35, 50, 80}) {
      const double yy = top + h - d / 80.0 * h;
      p.drawLine(QPointF(x + w, yy), QPointF(x + w + 2 * f.px, yy));
      p.drawText(QRectF(x + w + 3 * f.px, yy - lh / 2, 20 * f.px, lh),
                 Qt::AlignVCenter, QString::number(d));
    }
    p.setFont(lf);
    p.save();
    p.translate(x + w + 22 * f.px, top + h / 2);
    p.rotate(90);
    p.drawText(QRectF(-h / 2, -lh / 2, h, lh), Qt::AlignCenter,
               tr(f.style.lang, "Derinlik (km)", "Depth (km)"));
    p.restore();
    if (f.style.showDistanceLegend) {
      QFont bf = lf;
      bf.setBold(true);
      p.setFont(bf);
      double yy = top + h + lh * 1.2;
      p.drawText(QRectF(x - 6 * f.px, yy, 90 * f.px, lh), Qt::AlignVCenter,
                 tr(f.style.lang, "İst.-Dep. Mesafesi:", "Sta.-Eq. Distance:"));
      p.setFont(lf);
      const QStringList rows{"0-100 km", "100-300 km", "300-500 km",
                             "500-1000 km"};
      for (int i = 0; i < 4; ++i) {
        yy += lh;
        p.setPen(QPen(Qt::black, 0.5 * f.px));
        p.setBrush(Qt::white);
        drawMarker(p, QPointF(x, yy + lh / 2), 2.6 * f.px, i);
        p.drawText(QRectF(x + 8 * f.px, yy, 80 * f.px, lh), Qt::AlignVCenter,
                   rows[i]);
      }
    }
  }
  p.setFont(f.font);
}

void drawTitle(QPainter &p, const Frame &f, const QString &title) {
  QFont tf = f.font;
  tf.setBold(true);
  tf.setPointSizeF(f.font.pointSizeF() * 1.15);
  p.setFont(tf);
  p.setPen(Qt::black);
  p.drawText(QRectF(f.outer.left(), f.outer.top() + 2 * f.px, f.outer.width(),
                    f.plot.top() - f.outer.top() - 4 * f.px),
             Qt::AlignHCenter | Qt::AlignBottom, title);
  p.setFont(f.font);
}

YRange rangeFor(const MetricSeries &s, const PlotStyle &style) {
  if (style.yRange)
    return *style.yRange;
  if (const auto r = scapRange(s.column))
    return *r;
  return autoRange(s);
}

QString metricName(const MetricSeries &s, const PlotStyle &style) {
  return style.metricName.isEmpty() ? metricTitle(s.column, style.lang)
                                    : style.metricName;
}

QString titlePrefix(const PlotStyle &style, const MetricSeries &s) {
  QString t = style.station;
  if (!style.channel.isEmpty())
    t += (t.isEmpty() ? QString() : QStringLiteral(" ")) + style.channel;
  if (!t.isEmpty())
    t += QStringLiteral(" - ");
  return t + metricName(s, style);
}

// Days end to end: x = dayIndex * 1440 + minutes.
void drawMultiDay(QPainter &p, const QRectF &rect, const MetricSeries &s,
                  const std::vector<QDate> &days,
                  const std::vector<EventAtStation> &events,
                  const std::optional<EventAtStation> &mainEvent,
                  const PlotStyle &style, const QString &title,
                  bool dayMonthLabels) {
  if (days.empty())
    return;
  const Frame f = makeFrame(p, rect, style, double(days.size()) * 1440.0,
                            rangeFor(s, style));
  p.fillRect(rect, Qt::white);
  std::vector<double> x, v;
  for (size_t i = 0; i < days.size(); ++i) {
    const DaySlice d = daySlice(s, days[i]);
    for (size_t k = 0; k < d.minutes.size(); ++k) {
      x.push_back(double(i) * 1440.0 + d.minutes[k]);
      v.push_back(d.value[k]);
    }
  }
  std::vector<std::pair<double, QString>> ticks;
  for (size_t i = 0; i <= days.size(); ++i) {
    const QDate d = i < days.size() ? days[i] : days.back().addDays(1);
    ticks.push_back({double(i) * 1440.0,
                     dayMonthLabels ? d.toString(QStringLiteral("dd/MM"))
                                    : d.toString(QStringLiteral("MM/dd"))});
  }
  drawAxes(p, f, ticks,
           dayMonthLabels
               ? tr(style.lang, "Tarih (Gün/Ay)", "Date (Day/Month)")
               : tr(style.lang, "Tarih (Ay/Gün)", "Date (Month/Day)"),
           metricName(s, style));
  p.setPen(QPen(QColor(0x66, 0x66, 0x66, 128), 1.2 * f.px, Qt::DotLine));
  for (size_t i = 1; i < days.size(); ++i)
    p.drawLine(QPointF(f.sx(double(i) * 1440.0), f.plot.top()),
               QPointF(f.sx(double(i) * 1440.0), f.plot.bottom()));
  drawTrace(p, f, x, v);
  std::vector<Mark> marks;
  for (const EventAtStation &e : events) {
    if (e.event.mag < style.minPlotMag)
      continue;
    const QDate d = dayOf(e.event.epoch);
    const auto it = std::find(days.begin(), days.end(), d);
    if (it == days.end())
      continue;
    const double minutes = (e.event.epoch - dayStart(d)) / 60.0;
    marks.push_back({double(it - days.begin()) * 1440.0 + minutes, e.event.mag,
                     e.event.depthKm, e.km, e.band});
  }
  drawEvents(p, f, marks);
  if (mainEvent && mainEvent->event.mag >= 5.0) {
    const QDate d = dayOf(mainEvent->event.epoch);
    const auto it = std::find(days.begin(), days.end(), d);
    if (it != days.end()) {
      const double xx = f.sx(double(it - days.begin()) * 1440.0 +
                             (mainEvent->event.epoch - dayStart(d)) / 60.0);
      p.setPen(QPen(QColor(0xD9, 0, 0), 1.8 * f.px));
      p.drawLine(QPointF(xx, f.plot.top()), QPointF(xx, f.plot.bottom()));
    }
  }
  drawLegends(p, f);
  drawTitle(p, f, title);
}

} // namespace

QColor depthColor(double d) {
  struct Stop {
    double at;
    QColor c;
  };
  static const Stop stops[] = {{0.0, QColor("#FFFF99")},
                               {5.0, QColor("#FFD700")},
                               {20.0, QColor("#FF8C00")},
                               {35.0, QColor("#CC2200")},
                               {80.0, QColor("#4B0082")}};
  d = std::clamp(d, 0.0, 80.0);
  for (size_t i = 1; i < std::size(stops); ++i)
    if (d <= stops[i].at) {
      const double t = (d - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
      const QColor &a = stops[i - 1].c, &b = stops[i].c;
      return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                              a.greenF() + (b.greenF() - a.greenF()) * t,
                              a.blueF() + (b.blueF() - a.blueF()) * t);
    }
  return stops[std::size(stops) - 1].c;
}

void drawDaily(QPainter &p, const QRectF &rect, const MetricSeries &s,
               const QDate &day, const std::vector<EventAtStation> &events,
               const PlotStyle &style) {
  const Frame f = makeFrame(p, rect, style, 1440.0, rangeFor(s, style));
  p.fillRect(rect, Qt::white);
  const DaySlice d = daySlice(s, day);
  std::vector<std::pair<double, QString>> ticks;
  for (int h = 0; h <= 24; h += 2)
    ticks.push_back({h * 60.0, QString::number(h)});
  drawAxes(p, f, ticks, tr(style.lang, "Zaman (Saat)", "Time (Hours)"),
           metricName(s, style));
  drawTrace(p, f, d.minutes, d.value);
  std::vector<Mark> marks;
  const double t0 = dayStart(day);
  for (const EventAtStation &e : events)
    if (e.event.mag >= style.minPlotMag && e.event.epoch >= t0 &&
        e.event.epoch < t0 + 86400)
      marks.push_back({(e.event.epoch - t0) / 60.0, e.event.mag,
                       e.event.depthKm, e.km, e.band});
  drawEvents(p, f, marks);
  drawLegends(p, f);
  drawTitle(p, f,
            titlePrefix(style, s) + QStringLiteral(" - ") +
                dateLabel(day, style.lang));
}

void drawCombined(QPainter &p, const QRectF &rect, const MetricSeries &s,
                  const std::vector<QDate> &days,
                  const std::vector<EventAtStation> &events,
                  const std::optional<EventAtStation> &mainEvent,
                  const PlotStyle &style) {
  if (days.empty())
    return;
  const QString title =
      QStringLiteral("%1 - %2 - %3 (%4 %5)")
          .arg(titlePrefix(style, s), dateLabel(days.front(), style.lang),
               dateLabel(days.back(), style.lang))
          .arg(days.size())
          .arg(tr(style.lang, "Gün", "Days"));
  drawMultiDay(p, rect, s, days, events, mainEvent, style, title, false);
}

void drawCentred(QPainter &p, const QRectF &rect, const MetricSeries &s,
                 const std::vector<QDate> &days,
                 const std::vector<EventAtStation> &events,
                 const EventAtStation &mainEvent, const PlotStyle &style) {
  if (days.empty())
    return;
  const QString title =
      QStringLiteral("%1 - %2 M%3 (%4 %5)")
          .arg(titlePrefix(style, s),
               tr(style.lang, "Ana Deprem", "Main Earthquake"))
          .arg(mainEvent.event.mag, 0, 'f', 1)
          .arg(days.size())
          .arg(tr(style.lang, "Gün", "Days"));
  drawMultiDay(p, rect, s, days, events, mainEvent, style, title, true);
}

QImage renderImage(const QSize &size, const DrawFn &draw) {
  QImage img(size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  draw(p, QRectF(QPointF(0, 0), QSizeF(size)));
  return img;
}

bool renderSvg(const QString &path, const QSize &size, const DrawFn &draw) {
  QSvgGenerator gen;
  gen.setFileName(path);
  gen.setSize(size);
  gen.setViewBox(QRect(QPoint(0, 0), size));
  gen.setTitle(QStringLiteral("sismokaos-gui"));
  QPainter p;
  if (!p.begin(&gen))
    return false;
  p.setRenderHint(QPainter::Antialiasing);
  draw(p, QRectF(QPointF(0, 0), QSizeF(size)));
  p.end();
  return true;
}

QSize dailySize(int dpi) { return QSize(int(8.27 * dpi), int(3.1 * dpi)); }
QSize combinedSize(int dpi) { return QSize(int(11.0 * dpi), int(3.1 * dpi)); }
QSize centredSize(int dpi) { return QSize(int(9.5 * dpi), int(3.1 * dpi)); }

} // namespace skg::analysis
