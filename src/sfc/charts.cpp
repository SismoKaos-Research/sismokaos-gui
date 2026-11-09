#include "sfc/charts.h"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace skg::sfc {

namespace {
constexpr int kDpi = 150;
QSize inches(double w, double h) { return QSize(int(w * kDpi), int(h * kDpi)); }

QImage canvas(const QSize &size) {
  QImage img(size, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  return img;
}

void setup(QPainter &p) {
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);
  QFont f = p.font();
  f.setPointSizeF(9.5);
  p.setFont(f);
}

// Nice tick positions for a range (matplotlib-like).
std::vector<double> niceTicks(double lo, double hi, int n) {
  if (!(hi > lo)) {
    hi = lo + 1;
  }
  const double raw = (hi - lo) / std::max(1, n);
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  double step = 10 * mag;
  for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    if (m * mag >= raw) {
      step = m * mag;
      break;
    }
  std::vector<double> t;
  for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9 * step; v += step)
    t.push_back(std::abs(v) < 1e-12 ? 0.0 : v);
  return t;
}

QRectF panelRect(const QSize &size, int rows, int cols, int r, int c,
                 double top = 0.0) {
  const double w = double(size.width()) / cols,
               h = (size.height() - top) / rows;
  return QRectF(c * w, top + r * h, w, h);
}
} // namespace

QString tickLabel(double v) {
  if (std::abs(v) >= 1e5 || (std::abs(v) < 1e-3 && v != 0))
    return QString::number(v, 'g', 3);
  QString s = QString::number(v, 'f', 4);
  while (s.contains('.') && (s.endsWith('0')))
    s.chop(1);
  if (s.endsWith('.'))
    s.chop(1);
  return s;
}

// --- Axes
// -----------------------------------------------------------------------

Axes::Axes(QPainter &p, const QRectF &rect, double xMin, double xMax,
           double yMin, double yMax)
    : p_(p), outer_(rect), xMin_(xMin), xMax_(xMax), yMin_(yMin), yMax_(yMax) {
  unit_ = std::clamp(std::min(rect.width(), rect.height()) / 450.0, 0.6, 1.6);
  QFont f = p.font();
  f.setPointSizeF(9.5 * std::max(0.8, unit_));
  p.setFont(f);
  const QFontMetricsF fm(f);
  plot_ = rect.adjusted(fm.height() * 4.2, fm.height() * 1.9,
                        -fm.height() * 1.2, -fm.height() * 3.4);
  if (!(xMax_ > xMin_))
    xMax_ = xMin_ + 1;
  if (!(yMax_ > yMin_))
    yMax_ = yMin_ + 1;
}

double Axes::sx(double x) const {
  return plot_.left() + (x - xMin_) / (xMax_ - xMin_) * plot_.width();
}
double Axes::sy(double y) const {
  return plot_.bottom() - (y - yMin_) / (yMax_ - yMin_) * plot_.height();
}
QPointF Axes::px(double x, double y) const { return {sx(x), sy(y)}; }

void Axes::frame() {
  p_.setPen(QPen(Qt::black, 0.9 * unit_));
  p_.setBrush(Qt::NoBrush);
  p_.drawRect(plot_);
}

void Axes::xTicks(const std::vector<double> &at, const QStringList &labels,
                  bool rotate) {
  xTicks_ = at;
  const QFontMetricsF fm(p_.font());
  p_.setPen(QPen(Qt::black, 0.8 * unit_));
  for (size_t i = 0; i < at.size(); ++i) {
    const double x = sx(at[i]);
    if (x < plot_.left() - 1 || x > plot_.right() + 1)
      continue;
    p_.drawLine(QPointF(x, plot_.bottom()),
                QPointF(x, plot_.bottom() + 3 * unit_));
    const QString l = labels.value(int(i));
    if (rotate) {
      p_.save();
      p_.translate(x, plot_.bottom() + 5 * unit_);
      p_.rotate(-30);
      p_.drawText(QRectF(-fm.horizontalAdvance(l), -fm.height() / 2,
                         fm.horizontalAdvance(l), fm.height()),
                  Qt::AlignRight | Qt::AlignVCenter, l);
      p_.restore();
    } else
      p_.drawText(QRectF(x - 60 * unit_, plot_.bottom() + 4 * unit_,
                         120 * unit_, fm.height()),
                  Qt::AlignHCenter, l);
  }
}

void Axes::yTicks(const std::vector<double> &at, const QStringList &labels) {
  yTicks_ = at;
  const QFontMetricsF fm(p_.font());
  p_.setPen(QPen(Qt::black, 0.8 * unit_));
  for (size_t i = 0; i < at.size(); ++i) {
    const double y = sy(at[i]);
    if (y < plot_.top() - 1 || y > plot_.bottom() + 1)
      continue;
    p_.drawLine(QPointF(plot_.left() - 3 * unit_, y), QPointF(plot_.left(), y));
    p_.drawText(QRectF(outer_.left(), y - fm.height() / 2,
                       plot_.left() - outer_.left() - 5 * unit_, fm.height()),
                Qt::AlignRight | Qt::AlignVCenter, labels.value(int(i)));
  }
}

void Axes::autoYTicks(int n) {
  const std::vector<double> t = niceTicks(yMin_, yMax_, n);
  QStringList l;
  for (double v : t)
    l << tickLabel(v);
  yTicks(t, l);
}

void Axes::autoXTicks(int n) {
  const std::vector<double> t = niceTicks(xMin_, xMax_, n);
  QStringList l;
  for (double v : t)
    l << tickLabel(v);
  xTicks(t, l);
}

void Axes::labels(const QString &x, const QString &y) {
  const QFontMetricsF fm(p_.font());
  p_.setPen(Qt::black);
  p_.drawText(QRectF(plot_.left(), outer_.bottom() - fm.height() * 1.3,
                     plot_.width(), fm.height()),
              Qt::AlignHCenter, x);
  p_.save();
  p_.translate(outer_.left() + fm.height() * 0.9, plot_.center().y());
  p_.rotate(-90);
  p_.drawText(QRectF(-plot_.height() / 2, -fm.height() / 2, plot_.height(),
                     fm.height()),
              Qt::AlignCenter, y);
  p_.restore();
}

void Axes::title(const QString &t) {
  QFont f = p_.font();
  f.setBold(true);
  f.setPointSizeF(f.pointSizeF() * 1.1);
  const QFontMetricsF fm(f);
  p_.save();
  p_.setFont(f);
  p_.setPen(Qt::black);
  p_.drawText(QRectF(plot_.left(), plot_.top() - fm.height() * 1.5,
                     plot_.width(), fm.height() * 1.4),
              Qt::AlignHCenter | Qt::AlignBottom, t);
  p_.restore();
}

void Axes::grid(bool x, bool y) {
  p_.setPen(QPen(QColor(0, 0, 0, 40), 0.7 * unit_, Qt::DotLine));
  if (y)
    for (double v : yTicks_)
      p_.drawLine(QPointF(plot_.left(), sy(v)), QPointF(plot_.right(), sy(v)));
  if (x)
    for (double v : xTicks_)
      p_.drawLine(QPointF(sx(v), plot_.top()), QPointF(sx(v), plot_.bottom()));
}

void Axes::legend(const QList<QPair<QString, QColor>> &items,
                  Qt::Alignment where) {
  if (items.isEmpty())
    return;
  const QFontMetricsF fm(p_.font());
  double w = 0;
  for (const auto &it : items)
    w = std::max(w, fm.horizontalAdvance(it.first));
  const double lh = fm.height() * 1.25, boxW = w + 26 * unit_,
               boxH = lh * items.size() + 6 * unit_;
  double x = where & Qt::AlignLeft ? plot_.left() + 6 * unit_
                                   : plot_.right() - boxW - 6 * unit_;
  double y = where & Qt::AlignBottom ? plot_.bottom() - boxH - 6 * unit_
                                     : plot_.top() + 6 * unit_;
  p_.setPen(QPen(QColor(0xaa, 0xaa, 0xaa), 0.6 * unit_));
  p_.setBrush(QColor(255, 255, 255, 230));
  p_.drawRect(QRectF(x, y, boxW, boxH));
  for (int i = 0; i < items.size(); ++i) {
    p_.setPen(Qt::NoPen);
    p_.setBrush(items[i].second);
    p_.drawRect(QRectF(x + 5 * unit_, y + 3 * unit_ + i * lh + lh * 0.2,
                       12 * unit_, lh * 0.6));
    p_.setPen(Qt::black);
    p_.drawText(
        QRectF(x + 21 * unit_, y + 3 * unit_ + i * lh, w + 4 * unit_, lh),
        Qt::AlignVCenter, items[i].first);
  }
}

// --- colours
// -----------------------------------------------------------------------

QColor classColor(int cls) {
  static const QColor tab10[] = {QColor("#1f77b4"), QColor("#ff7f0e"),
                                 QColor("#2ca02c"), QColor("#d62728"),
                                 QColor("#9467bd"), QColor("#8c564b"),
                                 QColor("#e377c2"), QColor("#7f7f7f"),
                                 QColor("#bcbd22"), QColor("#17becf")};
  return tab10[((cls % 10) + 10) % 10];
}

QColor splitColor(const QString &split) {
  if (split == QLatin1String("train"))
    return QColor("#1f77b4");
  if (split == QLatin1String("val"))
    return QColor("#ff7f0e");
  return QColor("#2ca02c");
}

QColor blues(double t) {
  t = std::clamp(t, 0.0, 1.0);
  const QColor a("#f7fbff"), b("#08306b");
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                          a.greenF() + (b.greenF() - a.greenF()) * t,
                          a.blueF() + (b.blueF() - a.blueF()) * t);
}

QColor coolwarm(double t) {
  t = std::clamp(t, 0.0, 1.0);
  const QColor a("#3b4cc0"), m("#dddddd"), b("#b40426");
  if (t < 0.5) {
    const double u = t * 2;
    return QColor::fromRgbF(a.redF() + (m.redF() - a.redF()) * u,
                            a.greenF() + (m.greenF() - a.greenF()) * u,
                            a.blueF() + (m.blueF() - a.blueF()) * u);
  }
  const double u = (t - 0.5) * 2;
  return QColor::fromRgbF(m.redF() + (b.redF() - m.redF()) * u,
                          m.greenF() + (b.greenF() - m.greenF()) * u,
                          m.blueF() + (b.blueF() - m.blueF()) * u);
}

// --- helpers
// -----------------------------------------------------------------------

double percentileOf(std::vector<double> v, double q) {
  v.erase(std::remove_if(v.begin(), v.end(),
                         [](double x) { return std::isnan(x); }),
          v.end());
  if (v.empty())
    return std::nan("");
  std::sort(v.begin(), v.end());
  const double pos = q * double(v.size() - 1);
  const size_t lo = static_cast<size_t>(std::floor(pos)),
               hi = static_cast<size_t>(std::ceil(pos));
  return v[lo] + (v[hi] - v[lo]) * (pos - double(lo));
}

Pca pcaFit(const std::vector<double> &X, int rows, int cols, int k) {
  Pca pca;
  pca.mean.assign(static_cast<size_t>(cols), 0.0);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c)
      pca.mean[static_cast<size_t>(c)] +=
          X[static_cast<size_t>(r) * static_cast<size_t>(cols) +
            static_cast<size_t>(c)];
  for (double &m : pca.mean)
    m /= std::max(1, rows);
  // covariance
  std::vector<std::vector<double>> cov(
      static_cast<size_t>(cols),
      std::vector<double>(static_cast<size_t>(cols), 0.0));
  for (int r = 0; r < rows; ++r)
    for (int i = 0; i < cols; ++i) {
      const double xi = X[static_cast<size_t>(r) * static_cast<size_t>(cols) +
                          static_cast<size_t>(i)] -
                        pca.mean[static_cast<size_t>(i)];
      for (int j = i; j < cols; ++j)
        cov[static_cast<size_t>(i)][static_cast<size_t>(j)] +=
            xi * (X[static_cast<size_t>(r) * static_cast<size_t>(cols) +
                    static_cast<size_t>(j)] -
                  pca.mean[static_cast<size_t>(j)]);
    }
  for (int i = 0; i < cols; ++i)
    for (int j = i; j < cols; ++j) {
      cov[static_cast<size_t>(i)][static_cast<size_t>(j)] /=
          std::max(1, rows - 1);
      cov[static_cast<size_t>(j)][static_cast<size_t>(i)] =
          cov[static_cast<size_t>(i)][static_cast<size_t>(j)];
    }
  // Jacobi eigen decomposition.
  std::vector<std::vector<double>> V(
      static_cast<size_t>(cols),
      std::vector<double>(static_cast<size_t>(cols), 0.0));
  for (int i = 0; i < cols; ++i)
    V[static_cast<size_t>(i)][static_cast<size_t>(i)] = 1;
  for (int sweep = 0; sweep < 100; ++sweep) {
    double off = 0;
    for (int i = 0; i < cols; ++i)
      for (int j = i + 1; j < cols; ++j)
        off += cov[static_cast<size_t>(i)][static_cast<size_t>(j)] *
               cov[static_cast<size_t>(i)][static_cast<size_t>(j)];
    if (off < 1e-18)
      break;
    for (int p = 0; p < cols; ++p)
      for (int q = p + 1; q < cols; ++q) {
        const double apq = cov[static_cast<size_t>(p)][static_cast<size_t>(q)];
        if (std::abs(apq) < 1e-300)
          continue;
        const double theta =
            (cov[static_cast<size_t>(q)][static_cast<size_t>(q)] -
             cov[static_cast<size_t>(p)][static_cast<size_t>(p)]) /
            (2 * apq);
        const double t = (theta >= 0 ? 1.0 : -1.0) /
                         (std::abs(theta) + std::sqrt(theta * theta + 1));
        const double c = 1 / std::sqrt(t * t + 1), s = t * c;
        for (int k2 = 0; k2 < cols; ++k2) {
          const double akp =
                           cov[static_cast<size_t>(k2)][static_cast<size_t>(p)],
                       akq =
                           cov[static_cast<size_t>(k2)][static_cast<size_t>(q)];
          cov[static_cast<size_t>(k2)][static_cast<size_t>(p)] =
              c * akp - s * akq;
          cov[static_cast<size_t>(k2)][static_cast<size_t>(q)] =
              s * akp + c * akq;
        }
        for (int k2 = 0; k2 < cols; ++k2) {
          const double apk =
                           cov[static_cast<size_t>(p)][static_cast<size_t>(k2)],
                       aqk =
                           cov[static_cast<size_t>(q)][static_cast<size_t>(k2)];
          cov[static_cast<size_t>(p)][static_cast<size_t>(k2)] =
              c * apk - s * aqk;
          cov[static_cast<size_t>(q)][static_cast<size_t>(k2)] =
              s * apk + c * aqk;
        }
        for (int k2 = 0; k2 < cols; ++k2) {
          const double vkp = V[static_cast<size_t>(k2)][static_cast<size_t>(p)],
                       vkq = V[static_cast<size_t>(k2)][static_cast<size_t>(q)];
          V[static_cast<size_t>(k2)][static_cast<size_t>(p)] =
              c * vkp - s * vkq;
          V[static_cast<size_t>(k2)][static_cast<size_t>(q)] =
              s * vkp + c * vkq;
        }
      }
  }
  std::vector<int> order(static_cast<size_t>(cols));
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return cov[static_cast<size_t>(a)][static_cast<size_t>(a)] >
           cov[static_cast<size_t>(b)][static_cast<size_t>(b)];
  });
  double total = 0;
  for (int i = 0; i < cols; ++i)
    total += std::max(0.0, cov[static_cast<size_t>(i)][static_cast<size_t>(i)]);
  for (int i = 0; i < std::min(k, cols); ++i) {
    std::vector<double> comp(static_cast<size_t>(cols));
    for (int r = 0; r < cols; ++r)
      comp[static_cast<size_t>(r)] =
          V[static_cast<size_t>(r)][static_cast<size_t>(order[size_t(i)])];
    pca.components.push_back(comp);
    pca.explained.push_back(
        total > 0 ? std::max(0.0, cov[static_cast<size_t>(order[size_t(i)])]
                                     [static_cast<size_t>(order[size_t(i)])]) /
                        total
                  : 0);
  }
  return pca;
}

std::vector<double> pcaTransform(const Pca &pca, const std::vector<double> &X,
                                 int rows, int cols) {
  const int k = int(pca.components.size());
  std::vector<double> out(static_cast<size_t>(rows) * static_cast<size_t>(k));
  for (int r = 0; r < rows; ++r)
    for (int i = 0; i < k; ++i) {
      double s = 0;
      for (int c = 0; c < cols; ++c)
        s += (X[static_cast<size_t>(r) * static_cast<size_t>(cols) +
                static_cast<size_t>(c)] -
              pca.mean[static_cast<size_t>(c)]) *
             pca.components[static_cast<size_t>(i)][static_cast<size_t>(c)];
      out[static_cast<size_t>(r) * static_cast<size_t>(k) +
          static_cast<size_t>(i)] = s;
    }
  return out;
}

// --- figures
// -----------------------------------------------------------------------

namespace {
void bars(QPainter &p, Axes &ax, const std::vector<double> &values,
          const QColor &color, double width = 0.8) {
  for (size_t i = 0; i < values.size(); ++i) {
    const QPointF a = ax.px(double(i) - width / 2, values[i]),
                  b = ax.px(double(i) + width / 2, 0);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(QRectF(a, b).normalized());
  }
}
double maxOf(const std::vector<double> &v) {
  double m = 0;
  for (double x : v)
    if (std::isfinite(x))
      m = std::max(m, x);
  return m;
}
std::vector<double> indexTicks(int n) {
  std::vector<double> t(static_cast<size_t>(n));
  std::iota(t.begin(), t.end(), 0.0);
  return t;
}
} // namespace

QImage classDistributionFigure(const QStringList &classes,
                               const std::vector<double> &days,
                               const std::vector<double> &windows) {
  QImage img = canvas(inches(10, 4));
  QPainter p(&img);
  setup(p);
  const std::vector<double> *cols[2] = {&days, &windows};
  const char *names[2] = {"days", "windows"};
  for (int i = 0; i < 2; ++i) {
    Axes ax(p, panelRect(img.size(), 1, 2, 0, i), -0.6, classes.size() - 0.4, 0,
            maxOf(*cols[i]) * 1.08);
    ax.autoYTicks();
    ax.grid(false, true);
    bars(p, ax, *cols[i], QColor("#4682b4"));
    ax.frame();
    ax.xTicks(indexTicks(classes.size()), classes);
    ax.labels(QStringLiteral("Class"),
              QStringLiteral("Number of %1").arg(names[i]));
    ax.title(QStringLiteral("Number of %1 per class").arg(names[i]));
  }
  return img;
}

QImage
boxplotsFigure(const QStringList &features, const QStringList &classes,
               const std::vector<std::vector<std::vector<double>>> &values,
               const QString &title) {
  const int cols = 4, rows = std::max(1, int(std::ceil(features.size() / 4.0)));
  QImage img = canvas(inches(4 * cols, 3 * rows + 0.4));
  QPainter p(&img);
  setup(p);
  const double top = 0.4 * kDpi;
  for (int f = 0; f < features.size(); ++f) {
    const auto &perClass = values[static_cast<size_t>(f)];
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    struct Box {
      double q1, med, q3, wlo, whi;
    };
    std::vector<Box> boxes;
    for (const auto &v : perClass) {
      Box b{percentileOf(v, 0.25), percentileOf(v, 0.5), percentileOf(v, 0.75),
            0, 0};
      const double iqr = b.q3 - b.q1;
      double wlo = b.q1, whi = b.q3;
      for (double x : v) {
        if (x >= b.q1 - 1.5 * iqr && x < wlo)
          wlo = x;
        if (x <= b.q3 + 1.5 * iqr && x > whi)
          whi = x;
      }
      b.wlo = wlo;
      b.whi = whi;
      boxes.push_back(b);
      if (std::isfinite(wlo)) {
        lo = std::min(lo, wlo);
        hi = std::max(hi, whi);
      }
    }
    if (!std::isfinite(lo)) {
      lo = 0;
      hi = 1;
    }
    const double pad = (hi - lo) * 0.08 + 1e-12;
    Axes ax(p, panelRect(img.size(), rows, cols, f / cols, f % cols, top), -0.6,
            classes.size() - 0.4, lo - pad, hi + pad);
    ax.autoYTicks(5);
    ax.grid(false, true);
    for (size_t c = 0; c < boxes.size(); ++c) {
      const Box &b = boxes[c];
      if (!std::isfinite(b.med))
        continue;
      p.setPen(QPen(Qt::black, 0.9 * ax.unit()));
      p.setBrush(Qt::NoBrush);
      p.drawRect(
          QRectF(ax.px(double(c) - 0.3, b.q3), ax.px(double(c) + 0.3, b.q1))
              .normalized());
      p.setPen(QPen(QColor("#ff7f0e"), 1.4 * ax.unit()));
      p.drawLine(ax.px(double(c) - 0.3, b.med), ax.px(double(c) + 0.3, b.med));
      p.setPen(QPen(Qt::black, 0.9 * ax.unit()));
      p.drawLine(ax.px(double(c), b.q3), ax.px(double(c), b.whi));
      p.drawLine(ax.px(double(c), b.q1), ax.px(double(c), b.wlo));
      p.drawLine(ax.px(double(c) - 0.15, b.whi),
                 ax.px(double(c) + 0.15, b.whi));
      p.drawLine(ax.px(double(c) - 0.15, b.wlo),
                 ax.px(double(c) + 0.15, b.wlo));
    }
    ax.frame();
    ax.xTicks(indexTicks(classes.size()), classes);
    ax.labels(QStringLiteral("Class"), QStringLiteral("Feature value"));
    ax.title(features[f]);
  }
  QFont bf = p.font();
  bf.setBold(true);
  p.setFont(bf);
  p.setPen(Qt::black);
  p.drawText(QRectF(0, 0, img.width(), top), Qt::AlignCenter, title);
  return img;
}

QImage histogramsFigure(const QStringList &features,
                        const std::vector<std::vector<double>> &values,
                        const QString &title) {
  const int cols = 4, rows = std::max(1, int(std::ceil(features.size() / 4.0)));
  QImage img = canvas(inches(4 * cols, 3 * rows + 0.4));
  QPainter p(&img);
  setup(p);
  const double top = 0.4 * kDpi;
  for (int f = 0; f < features.size(); ++f) {
    const auto &v = values[static_cast<size_t>(f)];
    double lo = percentileOf(v, 0.005), hi = percentileOf(v, 0.995);
    if (!std::isfinite(lo) || !(hi > lo)) {
      lo = 0;
      hi = 1;
    }
    const int bins = 50;
    std::vector<double> count(static_cast<size_t>(bins), 0.0);
    for (double x : v)
      if (std::isfinite(x) && x >= lo && x <= hi)
        ++count[size_t(std::min(bins - 1, int((x - lo) / (hi - lo) * bins)))];
    Axes ax(p, panelRect(img.size(), rows, cols, f / cols, f % cols, top), lo,
            hi, 0, maxOf(count) * 1.08);
    ax.autoYTicks(5);
    ax.autoXTicks(5);
    ax.grid(false, true);
    for (int b = 0; b < bins; ++b) {
      const double x0 = lo + (hi - lo) * b / bins,
                   x1 = lo + (hi - lo) * (b + 1) / bins;
      p.setPen(Qt::NoPen);
      p.setBrush(QColor("#4682b4"));
      p.drawRect(QRectF(ax.px(x0, count[static_cast<size_t>(b)]), ax.px(x1, 0))
                     .normalized());
    }
    ax.frame();
    ax.labels(QStringLiteral("Feature value"),
              QStringLiteral("Number of windows"));
    ax.title(features[f]);
  }
  QFont bf = p.font();
  bf.setBold(true);
  p.setFont(bf);
  p.setPen(Qt::black);
  p.drawText(QRectF(0, 0, img.width(), top), Qt::AlignCenter, title);
  return img;
}

QImage correlationFigure(const QStringList &features,
                         const std::vector<std::vector<double>> &corr) {
  QImage img = canvas(inches(8, 7));
  QPainter p(&img);
  setup(p);
  const int n = features.size();
  const QFontMetricsF fm(p.font());
  const double left = fm.height() * 7, top = fm.height() * 2.5,
               right = img.width() - fm.height() * 5;
  const double bottom = img.height() - fm.height() * 7;
  const double cw = (right - left) / n, ch = (bottom - top) / n;
  QFont small = p.font();
  small.setPointSizeF(7);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      const double v = corr[static_cast<size_t>(i)][static_cast<size_t>(j)];
      const QRectF cell(left + j * cw, top + i * ch, cw, ch);
      p.fillRect(cell, coolwarm((v + 1) / 2));
      p.setFont(small);
      p.setPen(std::abs(v) > 0.6 ? Qt::white : Qt::black);
      p.drawText(cell, Qt::AlignCenter, QString::number(v, 'f', 2));
    }
  p.setFont(p.font());
  QFont f = p.font();
  f.setPointSizeF(8.5);
  p.setFont(f);
  p.setPen(Qt::black);
  for (int i = 0; i < n; ++i) {
    p.drawText(QRectF(0, top + i * ch, left - 4, ch),
               Qt::AlignRight | Qt::AlignVCenter, features[i]);
    p.save();
    p.translate(left + i * cw + cw / 2, bottom + 4);
    p.rotate(90);
    p.drawText(QRectF(0, -fm.height() / 2, fm.height() * 6, fm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, features[i]);
    p.restore();
  }
  // colour bar
  const double bx = right + fm.height() * 1.2, bw = fm.height() * 0.9;
  for (int k = 0; k < 100; ++k) {
    const double y0 = bottom - (bottom - top) * (k + 1) / 100.0;
    p.fillRect(QRectF(bx, y0, bw, (bottom - top) / 100.0 + 1),
               coolwarm(k / 99.0));
  }
  p.setPen(QPen(Qt::black, 0.8));
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(bx, top, bw, bottom - top));
  for (double v : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
    const double y = bottom - (bottom - top) * (v + 1) / 2;
    p.drawText(
        QRectF(bx + bw + 3, y - fm.height() / 2, fm.height() * 3, fm.height()),
        Qt::AlignVCenter, tickLabel(v));
  }
  QFont bf = p.font();
  bf.setBold(true);
  bf.setPointSizeF(11);
  p.setFont(bf);
  p.drawText(QRectF(left, 0, right - left, top), Qt::AlignCenter,
             QStringLiteral("Feature correlation"));
  return img;
}

QImage splitCountsFigure(const QStringList &classes, const QStringList &splits,
                         const std::vector<std::vector<double>> &windows,
                         const std::vector<std::vector<double>> &days) {
  QImage img = canvas(inches(11, 4.5));
  QPainter p(&img);
  setup(p);
  const std::vector<std::vector<double>> *tables[2] = {&windows, &days};
  const char *names[2] = {"windows", "days"};
  for (int i = 0; i < 2; ++i) {
    const auto &t = *tables[i];
    std::vector<double> totals(static_cast<size_t>(classes.size()), 0.0);
    for (size_t c = 0; c < totals.size(); ++c)
      for (size_t s = 0; s < t.size(); ++s)
        totals[c] += t[s][c];
    Axes ax(p, panelRect(img.size(), 1, 2, 0, i), -0.6, classes.size() - 0.4, 0,
            maxOf(totals) * 1.08);
    ax.autoYTicks();
    ax.grid(false, true);
    std::vector<double> bottom(totals.size(), 0.0);
    QList<QPair<QString, QColor>> legend;
    for (size_t s = 0; s < t.size(); ++s) {
      for (size_t c = 0; c < totals.size(); ++c) {
        p.setPen(Qt::NoPen);
        p.setBrush(splitColor(splits[int(s)]));
        p.drawRect(QRectF(ax.px(double(c) - 0.4, bottom[c] + t[s][c]),
                          ax.px(double(c) + 0.4, bottom[c]))
                       .normalized());
        bottom[c] += t[s][c];
      }
      legend << qMakePair(splits[int(s)], splitColor(splits[int(s)]));
    }
    ax.frame();
    ax.xTicks(indexTicks(classes.size()), classes);
    ax.labels(QStringLiteral("Class"),
              QStringLiteral("Number of %1").arg(names[i]));
    ax.title(
        QStringLiteral("%1%2 per class and split")
            .arg(QString(names[i][0]).toUpper(), QString(names[i]).mid(1)));
    ax.legend(legend);
  }
  return img;
}

QImage featureDistributionsFigure(
    const QStringList &features, const QStringList &splits,
    const std::vector<std::vector<std::vector<double>>> &values,
    const QString &title) {
  const int cols = 4, rows = std::max(1, int(std::ceil(features.size() / 4.0)));
  QImage img = canvas(inches(4 * cols, 3 * rows + 0.4));
  QPainter p(&img);
  setup(p);
  const double top = 0.4 * kDpi;
  for (int f = 0; f < features.size(); ++f) {
    std::vector<double> all;
    for (const auto &v : values[static_cast<size_t>(f)])
      all.insert(all.end(), v.begin(), v.end());
    double lo = percentileOf(all, 0.005), hi = percentileOf(all, 0.995);
    if (!std::isfinite(lo) || !(hi > lo)) {
      lo = 0;
      hi = 1;
    }
    const int bins = 40;
    std::vector<std::vector<double>> dens(
        values[static_cast<size_t>(f)].size(),
        std::vector<double>(static_cast<size_t>(bins), 0.0));
    double maxD = 0;
    for (size_t s = 0; s < dens.size(); ++s) {
      int n = 0;
      for (double x : values[static_cast<size_t>(f)][s])
        if (std::isfinite(x) && x >= lo && x <= hi) {
          ++dens[s]
                [size_t(std::min(bins - 1, int((x - lo) / (hi - lo) * bins)))];
          ++n;
        }
      const double width = (hi - lo) / bins;
      for (double &d : dens[s]) {
        d = n ? d / (n * width) : 0;
        maxD = std::max(maxD, d);
      }
    }
    Axes ax(p, panelRect(img.size(), rows, cols, f / cols, f % cols, top), lo,
            hi, 0, maxD * 1.08 + 1e-12);
    ax.autoYTicks(5);
    ax.autoXTicks(5);
    ax.grid(false, true);
    QList<QPair<QString, QColor>> legend;
    for (size_t s = 0; s < dens.size(); ++s) {
      QPolygonF poly;
      poly << ax.px(lo, 0);
      for (int b = 0; b < bins; ++b) {
        poly << ax.px(lo + (hi - lo) * b / bins,
                      dens[s][static_cast<size_t>(b)]);
        poly << ax.px(lo + (hi - lo) * (b + 1) / bins,
                      dens[s][static_cast<size_t>(b)]);
      }
      poly << ax.px(hi, 0);
      p.setPen(QPen(splitColor(splits[int(s)]), 1.5 * ax.unit()));
      p.setBrush(Qt::NoBrush);
      p.drawPolyline(poly);
      legend << qMakePair(splits[int(s)], splitColor(splits[int(s)]));
    }
    ax.frame();
    ax.labels(QStringLiteral("Feature value"), QStringLiteral("Density"));
    ax.title(features[f]);
    if (f == 0)
      ax.legend(legend);
  }
  QFont bf = p.font();
  bf.setBold(true);
  p.setFont(bf);
  p.setPen(Qt::black);
  p.drawText(QRectF(0, 0, img.width(), top), Qt::AlignCenter, title);
  return img;
}

QImage
scatterPanelsFigure(const ScatterData &d, const QStringList &axisNames,
                    const QStringList &classNames, const QString &title,
                    const std::vector<std::pair<double, double>> *limits) {
  QImage img = canvas(inches(13, 5.8));
  QPainter p(&img);
  setup(p);
  double xlo, xhi, ylo, yhi;
  if (limits && limits->size() >= 2) {
    xlo = (*limits)[0].first;
    xhi = (*limits)[0].second;
    ylo = (*limits)[1].first;
    yhi = (*limits)[1].second;
  } else {
    xlo = percentileOf(d.x, 0.0);
    xhi = percentileOf(d.x, 1.0);
    ylo = percentileOf(d.y, 0.0);
    yhi = percentileOf(d.y, 1.0);
  }
  const double top = 0.45 * kDpi;
  for (int panel = 0; panel < 2; ++panel) {
    Axes ax(p, panelRect(img.size(), 1, 2, 0, panel, top), xlo, xhi, ylo, yhi);
    ax.autoXTicks(6);
    ax.autoYTicks(6);
    ax.grid(true, true);
    p.setClipRect(ax.plot());
    for (size_t i = 0; i < d.x.size(); ++i) {
      QColor c = panel == 0 ? classColor(d.cls[i]) : splitColor(d.split[i]);
      c.setAlphaF(0.5);
      p.setPen(Qt::NoPen);
      p.setBrush(c);
      p.drawEllipse(ax.px(d.x[i], d.y[i]), 1.6 * ax.unit(), 1.6 * ax.unit());
    }
    p.setClipping(false);
    ax.frame();
    ax.labels(axisNames.value(0), axisNames.value(1));
    ax.title(panel == 0 ? QStringLiteral("Colored by class")
                        : QStringLiteral("Colored by split"));
    QList<QPair<QString, QColor>> legend;
    if (panel == 0)
      for (int c = 0; c < classNames.size(); ++c)
        legend << qMakePair(
            QStringLiteral("Class %1 (%2)").arg(c).arg(classNames[c]),
            classColor(c));
    else
      for (const char *s : {"train", "val", "test"})
        legend << qMakePair(QString(s), splitColor(s));
    ax.legend(legend);
  }
  QFont bf = p.font();
  bf.setBold(true);
  p.setFont(bf);
  p.setPen(Qt::black);
  p.drawText(QRectF(0, 0, img.width(), top), Qt::AlignCenter, title);
  return img;
}

QImage confusionFigure(const std::vector<std::vector<int>> &cm,
                       const QStringList &labels, const QString &title) {
  QImage img = canvas(inches(5, 4.5));
  QPainter p(&img);
  setup(p);
  const int n = labels.size();
  int mx = 1;
  for (const auto &row : cm)
    for (int v : row)
      mx = std::max(mx, v);
  // Row 0 at the top: data y = n - 1 - row.
  Axes ax(p, QRectF(0, 0, img.width(), img.height()), -0.5, n - 0.5, -0.5,
          n - 0.5);
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      const int v = cm[static_cast<size_t>(i)][static_cast<size_t>(j)];
      const double y = n - 1 - i;
      const QRectF cell =
          QRectF(ax.px(j - 0.5, y + 0.5), ax.px(j + 0.5, y - 0.5)).normalized();
      p.fillRect(cell, blues(double(v) / mx));
      p.setPen(v > mx / 2 ? Qt::white : Qt::black);
      p.drawText(cell, Qt::AlignCenter, QString::number(v));
    }
  ax.frame();
  ax.xTicks(indexTicks(n), labels);
  QStringList reversed = labels;
  std::reverse(reversed.begin(), reversed.end());
  ax.yTicks(indexTicks(n), reversed);
  ax.labels(QStringLiteral("Predicted label"), QStringLiteral("True label"));
  ax.title(title);
  return img;
}

QImage rocFigure(const std::vector<RocSeries> &curves, const QString &title) {
  QImage img = canvas(inches(5.5, 5));
  QPainter p(&img);
  setup(p);
  Axes ax(p, QRectF(0, 0, img.width(), img.height()), 0, 1, 0, 1);
  ax.autoXTicks(5);
  ax.autoYTicks(5);
  ax.grid(true, true);
  QList<QPair<QString, QColor>> legend;
  for (size_t i = 0; i < curves.size(); ++i) {
    QPolygonF poly;
    for (size_t k = 0; k < curves[i].fpr.size(); ++k)
      poly << ax.px(curves[i].fpr[k], curves[i].tpr[k]);
    p.setPen(QPen(classColor(int(i)), 1.6 * ax.unit()));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);
    legend << qMakePair(curves[i].label, classColor(int(i)));
  }
  p.setPen(QPen(Qt::black, 1.0 * ax.unit(), Qt::DashLine));
  p.drawLine(ax.px(0, 0), ax.px(1, 1));
  legend << qMakePair(QStringLiteral("Chance"), QColor(Qt::black));
  ax.frame();
  ax.labels(QStringLiteral("False positive rate"),
            QStringLiteral("True positive rate"));
  ax.title(title);
  ax.legend(legend, Qt::AlignRight | Qt::AlignBottom);
  return img;
}

QImage lossFigure(const std::vector<double> &train,
                  const std::vector<double> &val, const QString &title) {
  QImage img = canvas(inches(6, 4));
  QPainter p(&img);
  setup(p);
  double hi = std::max(maxOf(train), maxOf(val));
  Axes ax(p, QRectF(0, 0, img.width(), img.height()), 0,
          std::max<double>(1, train.size() - 1), 0, hi * 1.08 + 1e-12);
  ax.autoXTicks(6);
  ax.autoYTicks(5);
  ax.grid(true, true);
  auto line = [&](const std::vector<double> &v, const QColor &c) {
    QPolygonF poly;
    for (size_t i = 0; i < v.size(); ++i)
      poly << ax.px(double(i), v[i]);
    p.setPen(QPen(c, 1.6 * ax.unit()));
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(poly);
  };
  line(train, classColor(0));
  line(val, classColor(1));
  ax.frame();
  ax.labels(QStringLiteral("Epoch"), QStringLiteral("Cross-entropy loss"));
  ax.title(title);
  ax.legend({qMakePair(QStringLiteral("Train loss"), classColor(0)),
             qMakePair(QStringLiteral("Validation loss"), classColor(1))});
  return img;
}

QImage comparisonFigure(const QStringList &models,
                        const std::vector<double> &trainF1,
                        const std::vector<double> &valF1,
                        const std::vector<double> &testF1) {
  QImage img = canvas(inches(std::max(6.0, 1.3 * models.size()), 4.5));
  QPainter p(&img);
  setup(p);
  Axes ax(p, QRectF(0, 0, img.width(), img.height()), -0.6, models.size() - 0.4,
          0, 1);
  ax.autoYTicks(5);
  ax.grid(false, true);
  const std::vector<double> *series[3] = {&trainF1, &valF1, &testF1};
  const char *names[3] = {"train F1", "val F1", "test F1"};
  const double w = 0.26;
  QList<QPair<QString, QColor>> legend;
  for (int s = 0; s < 3; ++s) {
    for (size_t i = 0; i < series[s]->size(); ++i) {
      p.setPen(Qt::NoPen);
      p.setBrush(classColor(s));
      p.drawRect(QRectF(ax.px(double(i) + (s - 1) * w - w / 2, (*series[s])[i]),
                        ax.px(double(i) + (s - 1) * w + w / 2, 0))
                     .normalized());
    }
    legend << qMakePair(QString(names[s]), classColor(s));
  }
  ax.frame();
  ax.xTicks(indexTicks(models.size()), models, true);
  ax.labels(QString(), QStringLiteral("Macro F1"));
  ax.title(QStringLiteral("Best trial of each model"));
  ax.legend(legend);
  return img;
}

QImage errorTimelineFigure(const std::vector<TimelinePoint> &points,
                           double eventDay, bool showEvent,
                           const QString &title) {
  QImage img = canvas(inches(11, 4));
  QPainter p(&img);
  setup(p);
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (const TimelinePoint &pt : points) {
    lo = std::min(lo, pt.epochDay);
    hi = std::max(hi, pt.epochDay);
  }
  if (showEvent) {
    lo = std::min(lo, eventDay);
    hi = std::max(hi, eventDay);
  }
  if (!std::isfinite(lo)) {
    lo = 0;
    hi = 1;
  }
  const double pad = std::max(1.0, (hi - lo) * 0.03);
  Axes ax(p, QRectF(0, 0, img.width(), img.height()), lo - pad, hi + pad, -0.03,
          1.03);
  ax.autoYTicks(5);
  // date ticks
  std::vector<double> at;
  QStringList labels;
  const int n = 8;
  for (int i = 0; i <= n; ++i) {
    const double day = lo + (hi - lo) * i / n;
    at.push_back(day);
    labels << QDateTime::fromSecsSinceEpoch(qint64(day * 86400),
                                            QTimeZone::utc())
                  .date()
                  .toString(QStringLiteral("yyyy-MM-dd"));
  }
  ax.xTicks(at, labels, true);
  ax.grid(true, true);
  for (const TimelinePoint &pt : points) {
    QColor c = splitColor(pt.split);
    c.setAlphaF(0.85);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(ax.px(pt.epochDay, pt.rate), 3.2 * ax.unit(),
                  3.2 * ax.unit());
  }
  QList<QPair<QString, QColor>> legend;
  for (const char *s : {"train", "val", "test"})
    legend << qMakePair(QString(s), splitColor(s));
  if (showEvent) {
    p.setPen(QPen(Qt::black, 1.0 * ax.unit(), Qt::DashLine));
    p.drawLine(ax.px(eventDay, -0.03), ax.px(eventDay, 1.03));
    legend << qMakePair(QStringLiteral("event day"), QColor(Qt::black));
  }
  ax.frame();
  ax.labels(QStringLiteral("Date (UTC)"),
            QStringLiteral("Share of wrong predictions per day"));
  ax.title(title);
  ax.legend(legend);
  return img;
}

} // namespace skg::sfc
