#include "modules/study/comparepage.h"
#include "modules/study/casemap.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QSvgGenerator>
#include <QTableWidget>
#include <QThreadPool>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

#include "core/theme.h"
#include "core/ui.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "modules/study/runspage.h"
#include "study/store.h"

namespace skg {

using study::Recipe;
using study::ScoreRow;
using study::Series;
using study::Study;

namespace {

constexpr double kHead = 40, kRow = 42;

constexpr int kMostSeries = 8; // the eight colours

// The columns the score table can be sorted by.
const int kSortable[] = {0, 2, 3, 5, 6};

QColor caseColor(const QList<QColor> &colors, const QColor &muted,
                 const QStringList &order, const QString &caseId) {
  const qsizetype i = order.indexOf(caseId);
  if (i < 0 || i >= colors.size())
    return muted;
  return colors[i];
}

// A round step for an axis of about `ticks` divisions.
double niceStep(double range, int ticks) {
  if (!(range > 0))
    return 1.0;
  const double raw = range / ticks;
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  for (double m : {1.0, 2.0, 2.5, 5.0, 10.0})
    if (m * mag >= raw)
      return m * mag;
  return 10.0 * mag;
}

QString seriesLabel(const Series &s) {
  return s.station.isEmpty() ? s.caseName
                             : s.caseName + QStringLiteral(" · ") + s.station;
}

} // namespace

// ---------------------------------------------------------------- scores

ScoreTable::ScoreTable(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::StrongFocus);
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ScoreTable::setRows(const QList<ScoreRow> &rows) {
  // The row the keys act on stays on its case and station.
  const QString keep = current_ >= 0 && current_ < rows_.size()
                           ? rows_[current_].caseId + QLatin1Char('|') +
                                 rows_[current_].station
                           : QString();
  rows_ = rows;
  order_.clear();
  for (const ScoreRow &r : rows)
    if (!order_.contains(r.caseId))
      order_ << r.caseId;
  sortRows();
  current_ = -1;
  for (int i = 0; i < int(rows_.size()) && !keep.isEmpty(); ++i)
    if (rows_[i].caseId + QLatin1Char('|') + rows_[i].station == keep)
      current_ = i;
  updateGeometry();
  update();
}

QSize ScoreTable::minimumSizeHint() const {
  return {860, int(kHead + kRow * std::max<qsizetype>(1, rows_.size()) + 10)};
}

QList<double> ScoreTable::columnX() const {
  const double w = width();
  const double bars = std::max(300.0, w - 560.0);
  return {18, 210, 290, 340, 420, 560, 560 + bars / 2.0, 560 + bars};
}

void ScoreTable::sortRows() {
  if (sortColumn_ < 0)
    return;
  const auto value = [this](const ScoreRow &r) -> double {
    switch (sortColumn_) {
    case 2:
      return r.magnitude;
    case 3:
      return r.distanceKm;
    case 5:
      return r.testF1;
    case 6:
      return r.testAuc;
    default:
      return 0;
    }
  };
  std::stable_sort(rows_.begin(), rows_.end(),
                   [&](const ScoreRow &a, const ScoreRow &b) {
                     if (sortColumn_ == 0)
                       return descending_ ? a.caseName > b.caseName
                                          : a.caseName < b.caseName;
                     const double x = value(a), y = value(b);
                     // Missing numbers go last either way.
                     if (std::isnan(x) || std::isnan(y))
                       return !std::isnan(x) && std::isnan(y);
                     return descending_ ? x > y : x < y;
                   });
}

int ScoreTable::rowAt(const QPointF &pos) const {
  if (pos.y() <= kHead)
    return -1;
  const int row = int((pos.y() - kHead) / kRow);
  return row < rows_.size() ? row : -1;
}

void ScoreTable::sortBy(int column) {
  if (std::find(std::begin(kSortable), std::end(kSortable), column) ==
      std::end(kSortable))
    return; // station and model are not sort keys
  const QString keep = current_ >= 0 && current_ < rows_.size()
                           ? rows_[current_].caseId + QLatin1Char('|') +
                                 rows_[current_].station
                           : QString();
  descending_ = column == sortColumn_ ? !descending_ : column != 0;
  sortColumn_ = column;
  sortRows();
  current_ = -1;
  for (int i = 0; i < int(rows_.size()) && !keep.isEmpty(); ++i)
    if (rows_[i].caseId + QLatin1Char('|') + rows_[i].station == keep)
      current_ = i;
  update();
}

// Scores made under a warning carry a ⚠ beside the model; hovering the row
// says what it is.
bool ScoreTable::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    const auto *help = static_cast<QHelpEvent *>(e);
    const int row = help->pos().y() > kHead ? rowAt(help->pos()) : -1;
    if (row >= 0 && row < rows_.size() && !rows_[row].warnings.isEmpty()) {
      QStringList lines{tr("These scores were made with:")};
      for (const QString &w : rows_[row].warnings)
        lines << QStringLiteral("⚠  ") + w;
      QToolTip::showText(help->globalPos(),
                         ui::helpText(lines.join(QLatin1Char('\n'))), this);
      return true;
    }
    // Otherwise the table's own help.
  }
  return QWidget::event(e);
}

void ScoreTable::mousePressEvent(QMouseEvent *e) {
  if (e->position().y() > kHead) {
    current_ = rowAt(e->position());
    update();
    return;
  }
  const QList<double> x = columnX();
  int column = -1;
  for (int i = 0; i < 7; ++i)
    if (e->position().x() >= x[i] && e->position().x() < x[i + 1])
      column = i;
  sortBy(column);
}

void ScoreTable::mouseDoubleClickEvent(QMouseEvent *e) {
  const int row = rowAt(e->position());
  if (row >= 0) {
    current_ = row;
    update();
    emit rowActivated(rows_[row]);
  }
}

void ScoreTable::keyPressEvent(QKeyEvent *e) {
  const int n = int(std::size(kSortable));
  int at = -1;
  for (int i = 0; i < n; ++i)
    if (kSortable[i] == sortColumn_)
      at = i;
  switch (e->key()) {
  case Qt::Key_Left:
    if (at > 0)
      sortBy(kSortable[at - 1]);
    return;
  case Qt::Key_Right:
    if (at < n - 1)
      sortBy(kSortable[at + 1]);
    return;
  case Qt::Key_Space:
    if (sortColumn_ >= 0)
      sortBy(sortColumn_); // the other way round
    return;
  case Qt::Key_Up:
    current_ = std::max(rows_.isEmpty() ? -1 : 0, current_ - 1);
    update();
    return;
  case Qt::Key_Down:
    current_ = std::min(int(rows_.size()) - 1, current_ + 1);
    update();
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
    if (current_ >= 0 && current_ < rows_.size())
      emit rowActivated(rows_[current_]);
    return;
  default:
    QWidget::keyPressEvent(e);
  }
}

void ScoreTable::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  update();
}

void ScoreTable::focusOutEvent(QFocusEvent *e) {
  QWidget::focusOutEvent(e);
  update();
}

void ScoreTable::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  QPainterPath card;
  card.addRoundedRect(r, 8, 8);
  p.fillPath(card, col("surface"));
  p.setPen(col("border"));
  p.drawPath(card);

  const QList<double> x = columnX();
  const double barW = (x[6] - x[5]) - 150;
  QFont head = font();
  head.setBold(true);
  head.setPointSizeF(head.pointSizeF() * 0.9);
  QFont bold = font();
  bold.setWeight(QFont::DemiBold);
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.9);

  const QStringList heads = {tr("Case"),
                             tr("Station"),
                             tr("M"),
                             tr("Dist. (km)"),
                             tr("Best model"),
                             tr("Test F1  (model over baseline)"),
                             tr("Test AUC  (model over baseline)")};
  p.setFont(head);
  p.setPen(col("muted"));
  for (int i = 0; i < int(heads.size()); ++i) {
    QString text = heads[i];
    if (i == sortColumn_)
      text += descending_ ? QStringLiteral("  ▾") : QStringLiteral("  ▴");
    p.drawText(QRectF(x[i], 4, x[i + 1] - x[i] - 6, kHead - 6),
               Qt::AlignVCenter, text);
  }
  const QColor baseBar =
      theme_->dark() ? QColor(0x5B, 0x66, 0x76) : QColor(0xB4, 0xBC, 0xC8);
  for (int i = 0; i < int(rows_.size()); ++i) {
    const ScoreRow &row = rows_[i];
    const double y = kHead + i * kRow;
    if (i == current_) {
      // The row the keys act on; dotted while the table has the focus.
      p.setPen(hasFocus() ? QPen(col("text"), 1, Qt::DotLine) : QPen(Qt::NoPen));
      p.setBrush(col("surface2"));
      p.drawRoundedRect(QRectF(8, y + 2, width() - 16, kRow - 4), 6, 6);
    }
    p.setPen(col("border"));
    p.drawLine(QPointF(12, y), QPointF(width() - 12, y));
    p.setPen(QPen(col("surface"), 2));
    p.setBrush(caseColor(theme_->seriesColors(), col("muted"), order_,
                         row.caseId));
    if (row.reference) {
      // A reference date: a ring, so it is not told by colour alone.
      p.drawEllipse(QPointF(x[0] + 6, y + kRow / 2), 5.5, 5.5);
      p.setBrush(col("surface"));
      p.setPen(Qt::NoPen);
      p.drawEllipse(QPointF(x[0] + 6, y + kRow / 2), 2.5, 2.5);
    } else {
      p.drawEllipse(QPointF(x[0] + 6, y + kRow / 2), 5.5, 5.5);
    }
    p.setPen(col("text"));
    p.setFont(bold);
    p.drawText(QRectF(x[0] + 20, y, x[1] - x[0] - 26, kRow), Qt::AlignVCenter,
               p.fontMetrics().elidedText(row.caseName, Qt::ElideRight,
                                          int(x[1] - x[0] - 26)));
    p.setFont(font());
    const QString dash = QStringLiteral("–");
    const bool done = row.state == study::CellState::Done;
    MatrixCell state;
    state.state = row.state;
    const QStringList cells = {
        row.station.isEmpty() ? dash : row.station,
        std::isnan(row.magnitude) ? dash
                                  : QString::number(row.magnitude, 'f', 1),
        std::isnan(row.distanceKm) ? dash
                                   : QString::number(row.distanceKm, 'f', 0),
        done ? (row.bestModel.isEmpty() ? dash : row.bestModel) +
                   (row.warnings.isEmpty() ? QString()
                                           : QStringLiteral("  ⚠"))
             : RunMatrix::stateText(state)};
    for (int k = 0; k < 4; ++k) {
      p.setPen(k == 3 && !done ? col("muted") : col("text"));
      p.drawText(QRectF(x[k + 1], y, x[k + 2] - x[k + 1] - 6, kRow),
                 Qt::AlignVCenter, cells[k]);
    }
    const auto pair = [&](double xx, double model, double base) {
      if (std::isnan(model)) {
        p.setPen(col("muted"));
        p.setFont(font());
        p.drawText(QRectF(xx, y, 200, kRow), Qt::AlignVCenter, tr("missing"));
        return;
      }
      const double by = y + 11;
      p.setPen(Qt::NoPen);
      p.setBrush(col("accent"));
      p.drawRoundedRect(QRectF(xx, by, barW * std::clamp(model, 0.0, 1.0), 8),
                        2, 2);
      if (!std::isnan(base)) {
        p.setBrush(baseBar);
        p.drawRoundedRect(
            QRectF(xx, by + 10, barW * std::clamp(base, 0.0, 1.0), 8), 2, 2);
      }
      p.setPen(col("text"));
      p.setFont(bold);
      p.drawText(QRectF(xx + barW + 10, y, 44, kRow), Qt::AlignVCenter,
                 QString::number(model, 'f', 2));
      p.setPen(col("muted"));
      p.setFont(small);
      const double d = model - base;
      p.drawText(QRectF(xx + barW + 52, y, 110, kRow), Qt::AlignVCenter,
                 std::isnan(base) ? tr("no baseline")
                                  : QStringLiteral("%1  (%2%3)")
                                        .arg(base, 0, 'f', 2)
                                        .arg(d >= 0 ? QStringLiteral("+")
                                                    : QStringLiteral("−"))
                                        .arg(std::abs(d), 0, 'f', 2));
    };
    pair(x[5], row.testF1, row.baseF1);
    pair(x[6], row.testAuc, row.baseAuc);
  }
}

// ------------------------------------------------------------------ plot

SeriesPlot::SeriesPlot(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void SeriesPlot::setSeries(const QList<Series> &series, const QString &column,
                           double spanDays, const QStringList &order,
                           int notDrawn) {
  // Eight colours, in a fixed order; further series are not drawn.
  series_ = series.mid(0, kMostSeries);
  hidden_ = notDrawn + int(series.size() - series_.size());
  column_ = column;
  span_ = std::max(1.0, spanDays);
  order_ = order;
  update();
}

void SeriesPlot::paintEvent(QPaintEvent *) {
  QPainter p(this);
  paintPlot(p, QRectF(rect()));
}

void SeriesPlot::paintPlot(QPainter &p, const QRectF &rect,
                           bool forFile) const {
  p.setRenderHint(QPainter::Antialiasing);
  // A figure for a file is light whatever the theme.
  const auto col = [this, forFile](const char *t) {
    return forFile ? Theme::lightColor(QLatin1String(t))
                   : theme_->color(QLatin1String(t));
  };
  if (forFile)
    p.fillRect(rect, Qt::white);
  const QRectF r = rect.adjusted(1, 1, -1, -1);
  QPainterPath card;
  card.addRoundedRect(r, 8, 8);
  p.fillPath(card, col("surface"));
  p.setPen(col("border"));
  p.drawPath(card);
  QFont base = p.font();
  QFont small = base;
  small.setPointSizeF(base.pointSizeF() * 0.88);
  QFont bold = base;
  bold.setWeight(QFont::DemiBold);
  if (series_.isEmpty()) {
    p.setPen(col("muted"));
    p.setFont(base);
    p.drawText(r.adjusted(24, 0, -24, 0), Qt::AlignCenter | Qt::TextWordWrap,
               tr("No series to draw yet.\nRun an indicators or a features "
                  "recipe on the Runs page, then choose it above."));
    return;
  }

  // Each series keeps its colour: by case, and by position when a case has
  // several stations.
  QList<QColor> colors;
  const QList<QColor> palette =
      forFile ? Theme::lightSeriesColors() : theme_->seriesColors();
  bool sameCaseTwice = false;
  for (int i = 0; i < int(series_.size()); ++i)
    for (int j = 0; j < i; ++j)
      sameCaseTwice = sameCaseTwice || series_[i].caseId == series_[j].caseId;
  for (int i = 0; i < int(series_.size()); ++i)
    colors << (sameCaseTwice ? palette[i % palette.size()]
                             : caseColor(palette, col("muted"), order_,
                                         series_[i].caseId));

  // Legend, wrapping to further lines when it is long.
  p.setFont(small);
  double lx = r.left() + 18, ly = r.top() + 20;
  for (int i = 0; i < int(series_.size()); ++i) {
    const QString text = seriesLabel(series_[i]);
    const double w = 26 + p.fontMetrics().horizontalAdvance(text) + 22;
    if (lx + w > r.right() - 12 && lx > r.left() + 18) {
      lx = r.left() + 18;
      ly += 18;
    }
    p.setPen(QPen(colors[i], 2,
                  series_[i].reference ? Qt::DashLine : Qt::SolidLine));
    p.drawLine(QPointF(lx, ly), QPointF(lx + 20, ly));
    p.setPen(col("text"));
    p.drawText(QPointF(lx + 26, ly + 4), text);
    lx += w;
  }
  if (hidden_ > 0) {
    p.setPen(col("muted"));
    p.drawText(QPointF(lx, ly + 4),
               tr("+%1 more not drawn (choose with Series)").arg(hidden_));
  }

  const bool direct = series_.size() <= 4;
  const QRectF plot(r.left() + 62, ly + 26,
                    r.width() - 62 - (direct ? 156 : 26),
                    r.bottom() - 46 - (ly + 26));
  if (plot.width() < 40 || plot.height() < 40)
    return;

  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (const Series &s : series_)
    for (double v : s.values)
      if (!std::isnan(v)) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
  if (!(hi >= lo)) {
    lo = 0;
    hi = 1;
  }
  if (hi == lo) {
    lo -= 0.5;
    hi += 0.5;
  }
  const double ystep = niceStep(hi - lo, 5);
  lo = std::floor(lo / ystep) * ystep;
  hi = std::ceil(hi / ystep) * ystep;
  const auto X = [&](double d) {
    return plot.left() + (d + span_) / span_ * plot.width();
  };
  const auto Y = [&](double v) {
    return plot.bottom() - (v - lo) / (hi - lo) * plot.height();
  };

  // One y axis, recessive grid.
  p.setFont(small);
  const int decimals =
      ystep >= 1 ? 0 : std::min(6, int(std::ceil(-std::log10(ystep))) + 0);
  for (double v = lo; v <= hi + ystep * 0.01; v += ystep) {
    p.setPen(QPen(col("grid"), 1));
    p.drawLine(QPointF(plot.left(), Y(v)), QPointF(plot.right(), Y(v)));
    p.setPen(col("muted"));
    p.drawText(QRectF(r.left(), Y(v) - 9, 54, 18),
               Qt::AlignRight | Qt::AlignVCenter,
               QString::number(v, 'f', decimals));
  }
  const double xstep = niceStep(span_, 6);
  for (double d = 0; d >= -span_ - 1e-9; d -= xstep) {
    p.setPen(col("muted"));
    p.drawText(QRectF(X(d) - 30, plot.bottom() + 6, 60, 16), Qt::AlignHCenter,
               QString::number(d, 'f', 0));
  }
  p.drawText(QRectF(plot.left(), plot.bottom() + 24, plot.width(), 16),
             Qt::AlignHCenter, tr("Days before the event"));
  p.save();
  p.translate(r.left() + 14, plot.center().y());
  p.rotate(-90);
  p.drawText(QRectF(-120, -8, 240, 16), Qt::AlignHCenter, column_);
  p.restore();

  // The event, at zero.
  p.setPen(QPen(col("text"), 1.4));
  p.drawLine(QPointF(X(0), plot.top() - 4), QPointF(X(0), plot.bottom()));
  p.setFont(bold);
  p.drawText(QRectF(X(0) - 60, plot.top() - 22, 120, 16), Qt::AlignHCenter,
             tr("event"));

  QList<QPair<double, int>> ends;
  p.save();
  p.setClipRect(plot.adjusted(-2, -2, 2, 2));
  for (int i = 0; i < int(series_.size()); ++i) {
    const Series &s = series_[i];
    QPainterPath path;
    bool pen = false;
    double lastY = std::numeric_limits<double>::quiet_NaN();
    for (size_t k = 0; k < s.values.size(); ++k) {
      if (std::isnan(s.values[k])) {
        pen = false; // a gap, not a line through it
        continue;
      }
      const QPointF q(X(s.daysBefore[k]), Y(s.values[k]));
      if (pen)
        path.lineTo(q);
      else
        path.moveTo(q);
      pen = true;
      lastY = q.y();
    }
    p.setPen(QPen(colors[i], 2, s.reference ? Qt::DashLine : Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    if (!std::isnan(lastY))
      ends << qMakePair(lastY, i);
  }
  p.restore();
  if (direct) {
    // Names at the line ends, in ink, nudged apart.
    std::sort(ends.begin(), ends.end());
    double prev = -1e9;
    p.setFont(small);
    for (const auto &e : ends) {
      const double y = std::max(e.first, prev + 14);
      prev = y;
      p.setPen(Qt::NoPen);
      p.setBrush(colors[e.second]);
      p.drawEllipse(QPointF(X(0) + 10, y), 4, 4);
      p.setPen(col("text"));
      p.drawText(QPointF(X(0) + 19, y + 4),
                 p.fontMetrics().elidedText(seriesLabel(series_[e.second]),
                                            Qt::ElideRight, 132));
    }
  }
}

// ------------------------------------------------------------------ page

ComparePage::ComparePage(AppContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  stack_ = new QStackedWidget(this);

  // No study open: the window's own bar above offers New and Open.
  noStudy_ = new QLabel(this);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  stack_->addWidget(noStudy_);

  auto *main = new QWidget(this);
  recipeLabel_ = new QLabel(main);
  recipeLabel_->setObjectName(QStringLiteral("fieldLabel"));
  recipe_ = new QComboBox(main);
  recipe_->setObjectName(QStringLiteral("scoreRecipe"));
  recipe_->setMinimumWidth(240);
  headline_ = new QLabel(main);
  headline_->setObjectName(QStringLiteral("fieldLabel"));
  headline_->setWordWrap(true);
  exportTable_ = new QPushButton(main);
  auto *top = new QHBoxLayout;
  top->setSpacing(8);
  top->addWidget(recipeLabel_);
  top->addWidget(recipe_);
  top->addSpacing(10);
  top->addWidget(headline_, 1);
  top->addWidget(exportTable_);

  scores_ = new ScoreTable(ctx_.theme, main);
  auto *scroll = new QScrollArea(main);
  scroll->setWidget(scores_);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setObjectName(QStringLiteral("scoreScroll"));
  scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  scoresHint_ = new QLabel(main);
  scoresHint_->setObjectName(QStringLiteral("scoresHint"));
  scoresHint_->setWordWrap(true);
  scoresGo_ = new QPushButton(main);
  scoresGo_->setObjectName(QStringLiteral("scoresGo"));
  auto *hintRow = new QHBoxLayout;
  hintRow->addWidget(scoresHint_, 1);
  hintRow->addWidget(scoresGo_);

  seriesLabel_ = new QLabel(main);
  seriesLabel_->setObjectName(QStringLiteral("fieldLabel"));
  seriesRecipe_ = new QComboBox(main);
  seriesRecipe_->setObjectName(QStringLiteral("seriesRecipe"));
  seriesRecipe_->setMinimumWidth(240);
  columnLabel_ = new QLabel(main);
  columnLabel_->setObjectName(QStringLiteral("fieldLabel"));
  column_ = new QComboBox(main);
  column_->setObjectName(QStringLiteral("seriesColumn"));
  column_->setMinimumWidth(200);
  pick_ = new QToolButton(main);
  pick_->setObjectName(QStringLiteral("seriesPick"));
  pick_->setPopupMode(QToolButton::InstantPopup);
  auto *pickMenu = new QMenu(pick_);
  pick_->setMenu(pickMenu);
  aligned_ = new QLabel(main);
  aligned_->setObjectName(QStringLiteral("fieldLabel"));
  exportFigure_ = new QPushButton(main);
  auto *mid = new QHBoxLayout;
  mid->setSpacing(8);
  mid->addWidget(seriesLabel_);
  mid->addWidget(seriesRecipe_);
  mid->addWidget(columnLabel_);
  mid->addWidget(column_);
  mid->addWidget(pick_);
  mid->addStretch(1);
  mid->addWidget(aligned_);
  mid->addWidget(exportFigure_);

  plot_ = new SeriesPlot(ctx_.theme, main);
  auto *card = new QFrame(main);
  card->setObjectName(QStringLiteral("card"));
  fittedTitle_ = new QLabel(card);
  fittedTitle_->setObjectName(QStringLiteral("cardTitle"));
  fitted_ = new QTableWidget(card);
  fitted_->setObjectName(QStringLiteral("fittedTable"));
  fitted_->verticalHeader()->hide();
  fitted_->setShowGrid(false);
  fitted_->setAlternatingRowColors(true);
  fitted_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fitted_->setSelectionMode(QAbstractItemView::NoSelection);
  fitted_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                   Qt::AlignVCenter);
  fittedNote_ = new QLabel(card);
  fittedNote_->setObjectName(QStringLiteral("fieldLabel"));
  fittedNote_->setWordWrap(true);
  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(16, 12, 16, 12);
  cardLayout->addWidget(fittedTitle_);
  cardLayout->addWidget(fitted_, 1);
  cardLayout->addWidget(fittedNote_);
  // "Where": the same cases on a map, above the fitted values. The scores
  // stay the first thing on the page; the panel folds away.
  auto *whereCard = new QFrame(main);
  whereCard->setObjectName(QStringLiteral("card"));
  whereToggle_ = new QToolButton(whereCard);
  whereToggle_->setObjectName(QStringLiteral("whereToggle"));
  whereToggle_->setAutoRaise(true);
  whereToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  whereHint_ = new QLabel(whereCard);
  whereHint_->setObjectName(QStringLiteral("fieldLabel"));
  whereHint_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  whereMap_ = new GeoMap(ctx_.theme, whereCard);
  whereMap_->setObjectName(QStringLiteral("whereMap"));
  whereMap_->setMinimumHeight(190);
  auto *whereHead = new QHBoxLayout;
  whereHead->addWidget(whereToggle_);
  whereHead->addWidget(whereHint_, 1);
  auto *whereLayout = new QVBoxLayout(whereCard);
  whereLayout->setContentsMargins(12, 8, 12, 10);
  whereLayout->setSpacing(6);
  whereLayout->addLayout(whereHead);
  whereLayout->addWidget(whereMap_, 1);
  const bool whereOpen =
      ctx_.settings
          ? ctx_.settings->value(QStringLiteral("compare/whereOpen"), true)
                .toBool()
          : true;
  whereToggle_->setProperty("open", whereOpen);
  whereToggle_->setArrowType(whereOpen ? Qt::DownArrow : Qt::RightArrow);
  whereMap_->setVisible(whereOpen);
  whereHint_->setVisible(whereOpen);
  auto *side = new QVBoxLayout;
  side->setSpacing(12);
  side->addWidget(whereCard, whereOpen ? 5 : 0);
  side->addWidget(card, 4);
  connect(whereToggle_, &QToolButton::clicked, this, [this, side, whereCard] {
    const bool open = !whereToggle_->property("open").toBool();
    whereToggle_->setProperty("open", open);
    whereToggle_->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    whereMap_->setVisible(open);
    whereHint_->setVisible(open);
    side->setStretchFactor(whereCard, open ? 5 : 0);
    if (ctx_.settings)
      ctx_.settings->setValue(QStringLiteral("compare/whereOpen"), open);
  });
  connect(ctx_.theme, &Theme::changed, this, &ComparePage::updateWhere);
  auto *bottom = new QHBoxLayout;
  bottom->setSpacing(14);
  bottom->addWidget(plot_, 7);
  bottom->addLayout(side, 4);
  seriesNote_ = new QLabel(main);
  seriesNote_->setObjectName(QStringLiteral("seriesNote"));
  seriesNote_->setWordWrap(true);
  seriesNote_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  seriesNote_->hide();

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(10);
  mainLayout->addLayout(top);
  mainLayout->addWidget(scroll);
  mainLayout->addLayout(hintRow);
  mainLayout->addLayout(mid);
  mainLayout->addLayout(bottom, 1);
  mainLayout->addWidget(seriesNote_);
  stack_->addWidget(main);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  connect(recipe_, &QComboBox::activated, this, &ComparePage::load);
  connect(seriesRecipe_, &QComboBox::activated, this, [this] {
    column_->clear(); // the other recipe has other columns
    load();
  });
  connect(column_, &QComboBox::activated, this, &ComparePage::load);
  connect(exportTable_, &QPushButton::clicked, this, &ComparePage::exportTable);
  connect(exportFigure_, &QPushButton::clicked, this,
          &ComparePage::exportFigure);
  connect(scoresGo_, &QPushButton::clicked, this, [this] {
    const QString page = scoresGo_->property("page").toString();
    if (ctx_.nav && !page.isEmpty())
      ctx_.nav->show(page);
  });
  // The series to draw, when there are more than the eight colours.
  connect(pickMenu, &QMenu::aboutToShow, this, [this, pickMenu] {
    pickMenu->clear();
    for (const Series &series : data_.series) {
      const QString k = seriesKey(series);
      QAction *a = pickMenu->addAction(seriesLabel(series));
      a->setCheckable(true);
      a->setChecked(chosen_.contains(k));
      a->setEnabled(a->isChecked() || chosen_.size() < kMostSeries);
      connect(a, &QAction::toggled, this, [this, k](bool on) {
        if (on)
          chosen_.insert(k);
        else
          chosen_.remove(k);
        showSeries();
      });
    }
  });
  // A row's run folder, by double click or Enter.
  connect(scores_, &ScoreTable::rowActivated, this, [this](const ScoreRow &row) {
    if (!ctx_.study->isOpen())
      return;
    const Study &s = ctx_.study->study();
    const Recipe *r = s.findRecipe(recipe_->currentData().toString());
    const study::Case *c = s.findCase(row.caseId);
    if (!r || !c)
      return;
    const QString dir = study::runDir(s, *c, row.station, *r);
    if (QDir(dir).exists())
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  connect(ctx_.study, &study::StudyStore::changed, this, &ComparePage::refresh);
  // Only a run of the study changes what is shown here.
  connect(ctx_.jobs, &JobModel::jobFinished, this, [this](Job *job) {
    if (isVisible() && job && study::recipeTools().contains(job->tool()))
      refresh();
  });
  retranslate();
  refresh();
}

void ComparePage::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refresh();
  }
  QWidget::changeEvent(e);
}

void ComparePage::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  refresh(); // runs may have finished since the page was last seen
}

void ComparePage::retranslate() {
  noStudy_->setText(tr("No study is open.\nA study is a folder that holds "
                       "the cases, the recipes and every result. Start or "
                       "open one with the buttons above."));
  recipeLabel_->setText(tr("Scores of"));
  headline_->setText(
      tr("Test days. The model is the one that did best on the validation "
         "days; the baseline is the best of logistic regression and the "
         "SVMs, chosen the same way."));
  exportTable_->setText(tr("Export table…"));
  seriesLabel_->setText(tr("Series of"));
  columnLabel_->setText(tr("column"));
  aligned_->setText(tr("Aligned by days before each case's event"));
  exportFigure_->setText(tr("Export figure…"));
  fittedTitle_->setText(tr("FITTED PER CASE"));
  whereToggle_->setText(tr("WHERE"));
  whereToggle_->setToolTip(ui::helpText(
      tr("Shows or hides the map of the cases. Each case has the colour of "
         "its curve and its row; a reference date is a hollow diamond.")));
  whereHint_->setText(tr("marker size: magnitude · ◇ reference date"));
  whereMap_->setPlaceholder(tr("The cases of the study appear here."));
  fittedNote_->setText(tr("What each case fitted for itself in this recipe; "
                          "everything else in the recipe is identical."));
  ui::setHelp({recipeLabel_, recipe_},
              tr("The classification recipe whose scores are listed: one "
                 "row per case and station, with the baseline beside the "
                 "best model."));
  exportTable_->setToolTip(ui::helpText(
      tr("Writes the score rows as shown to a CSV file, by default in the "
         "study's exports folder.")));
  scores_->setToolTip(ui::helpText(
      tr("Click a heading to sort; double-click a row to open its run "
         "folder. With the keyboard: Left and Right choose the sort column, "
         "Space turns the order, Up and Down choose a row, Enter opens it.")));
  scoresGo_->setToolTip(
      ui::helpText(tr("Opens the page where what is missing is done.")));
  ui::setHelp({seriesLabel_, seriesRecipe_},
              tr("The indicators or features recipe whose values are drawn "
                 "below, one line per case."));
  ui::setHelp({columnLabel_, column_},
              tr("The column that is drawn: an indicator such as the "
                 "b-value, or a feature (the median of each day). Every "
                 "case is placed on days before its own event."));
  pick_->setToolTip(ui::helpText(
      tr("Which series are drawn when there are more than eight (the "
         "figure has eight colours). Untick one to tick another.")));
  exportFigure_->setToolTip(ui::helpText(
      tr("Writes the series figure to a PNG or an SVG file, by default in "
         "the study's exports folder. The file is always on a white "
         "background, whatever the theme.")));
}

QString ComparePage::seriesKey(const Series &s) {
  return s.caseId + QLatin1Char('|') + s.station;
}

void ComparePage::refresh() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  if (!open) {
    ++generation_; // a read under way is for a study that is closed now
    loading_ = false;
    data_ = {};
    return;
  }
  const Study &s = ctx_.study->study();
  const auto fill = [&s](QComboBox *box, const QStringList &tools) {
    const QString keep = box->currentData().toString();
    const QSignalBlocker block(box);
    box->clear();
    for (const Recipe &r : s.recipes)
      if (tools.contains(r.tool))
        box->addItem(r.name, r.id);
    const int i = box->findData(keep);
    if (i >= 0)
      box->setCurrentIndex(i);
  };
  fill(recipe_, {study::kToolClassification});
  fill(seriesRecipe_, {study::kToolIndicators, study::kToolFeatures});
  load();
}

void ComparePage::load() {
  if (!ctx_.study->isOpen())
    return;
  // Read on a worker thread; only the answer to the latest question is
  // shown. Until it is back the page keeps what it showed.
  const int generation = ++generation_;
  loading_ = true;
  study::CompareRequest q;
  q.scoreRecipe = recipe_->currentData().toString();
  q.seriesRecipe = seriesRecipe_->currentData().toString();
  q.column = column_->currentText();
  const Study snapshot = ctx_.study->study();
  const QPointer<ComparePage> self(this);
  QThreadPool::globalInstance()->start([self, generation, snapshot, q] {
    study::CompareData d;
    try {
      d = study::loadCompare(snapshot, q);
    } catch (const std::exception &e) {
      d.skipped << QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(
        qApp,
        [self, generation, d] {
          if (self && self->generation_ == generation)
            self->apply(d);
        },
        Qt::QueuedConnection);
  });
}

void ComparePage::apply(const study::CompareData &d) {
  loading_ = false;
  data_ = d;
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();

  scores_->setRows(d.scores);
  updateWhere();
  // As tall as its rows, up to about six of them; then it scrolls.
  if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("scoreScroll")))
    scroll->setFixedHeight(std::min(scores_->minimumSizeHint().height(), 300) +
                           2);
  exportTable_->setEnabled(!d.scores.isEmpty());
  int done = 0;
  for (const ScoreRow &row : d.scores)
    done += row.state == study::CellState::Done ? 1 : 0;
  // What the table shows, or what is missing and the page that is done on.
  QString hint, page, go;
  if (!d.hasScoreRecipe) {
    hint = tr("No classification recipe yet. Add one on the Runs page; it "
              "needs a features recipe to take its table from.");
    page = QStringLiteral("runs");
    go = tr("Go to Runs");
  } else if (d.scores.isEmpty()) {
    hint = tr("The study has no case with a station yet. Stations are "
              "ticked on the Cases page.");
    page = QStringLiteral("cases");
    go = tr("Go to Cases");
  } else if (done == 0) {
    hint = tr("None of the %1 rows has a finished run yet. Run the recipe "
              "on the Runs page.")
               .arg(d.scores.size());
    page = QStringLiteral("runs");
    go = tr("Go to Runs");
  } else {
    hint = tr("%1 of %2 rows have a finished run. Click a column heading to "
              "sort; double-click a row to open its folder.")
               .arg(done)
               .arg(d.scores.size());
    int warned = 0;
    for (const study::ScoreRow &r : d.scores)
      warned += r.warnings.isEmpty() ? 0 : 1;
    if (warned > 0)
      hint += QLatin1Char(' ') +
              tr("⚠ marks %n row(s) whose scores were made under a warning "
                 "(a sparse catalog, little data): point at the row to read "
                 "it.",
                 nullptr, warned);
    if (done < d.scores.size()) {
      page = QStringLiteral("runs");
      go = tr("Go to Runs");
    }
  }
  scoresHint_->setText(hint);
  scoresGo_->setText(go);
  scoresGo_->setProperty("page", page);
  scoresGo_->setVisible(!go.isEmpty() && ctx_.nav != nullptr);

  {
    const QSignalBlocker block(column_);
    column_->clear();
    column_->addItems(d.columns);
    column_->setCurrentIndex(std::max(0, int(d.columns.indexOf(d.column))));
  }
  const QString seriesId = seriesRecipe_->currentData().toString();
  if (chosenFor_ != seriesId) {
    chosenFor_ = seriesId;
    chosen_.clear();
  }
  showSeries();

  // The fitted values: one column per name any case recorded.
  const Recipe *r = s.findRecipe(seriesId);
  QStringList keys;
  for (const study::FittedRow &row : d.fitted)
    for (auto it = row.fitted.begin(); it != row.fitted.end(); ++it)
      if (!keys.contains(it.key()))
        keys << it.key();
  const bool stations = r && study::perStation(r->tool);
  QStringList heads;
  if (r) {
    heads << tr("Case");
    if (stations)
      heads << tr("Station");
    heads << keys;
  }
  fitted_->setColumnCount(int(heads.size()));
  fitted_->setHorizontalHeaderLabels(heads);
  fitted_->setRowCount(r ? int(d.fitted.size()) : 0);
  for (int i = 0; r && i < int(d.fitted.size()); ++i) {
    int c = 0;
    fitted_->setItem(i, c++, new QTableWidgetItem(d.fitted[i].caseName));
    if (stations)
      fitted_->setItem(i, c++, new QTableWidgetItem(d.fitted[i].station));
    for (const QString &k : keys) {
      const QVariant v = d.fitted[i].fitted.value(k);
      QString text = QStringLiteral("–");
      if (v.isValid())
        text = v.metaType().id() == QMetaType::Double
                   ? QString::number(v.toDouble(), 'g', 4)
                   : v.toString();
      fitted_->setItem(i, c++, new QTableWidgetItem(text));
    }
  }
  fitted_->resizeColumnsToContents();
  fitted_->horizontalHeader()->setStretchLastSection(true);

  // Runs that are finished but could not be drawn are named, with why.
  seriesNote_->setText(
      d.skipped.isEmpty()
          ? QString()
          : tr("⚠  Not drawn (%n run(s)): %1", nullptr, int(d.skipped.size()))
                .arg(d.skipped.join(QStringLiteral("; "))));
  seriesNote_->setVisible(!d.skipped.isEmpty());
}

void ComparePage::updateWhere() {
  if (!ctx_.study || !ctx_.study->isOpen()) {
    whereMap_->setContent({}, true);
    return;
  }
  // Every case in its series colour (the order of the study's cases, as in
  // the plot), named beside its marker; no stations, the panel is small.
  CaseMapSpec spec;
  spec.stations = false;
  whereMap_->setContent(
      caseMapContent(ctx_.study->study(), *ctx_.theme, spec), false);
}

void ComparePage::showSeries() {
  QStringList order;
  double span = 1;
  if (ctx_.study->isOpen())
    for (const study::Case &c : ctx_.study->study().cases) {
      order << c.id;
      span = std::max(span, double(c.spanDays));
    }
  const QList<Series> &all = data_.series;
  QList<Series> drawn = all;
  if (all.size() > kMostSeries) {
    // More than the eight colours: the first eight, until the user chooses.
    QSet<QString> present;
    for (const Series &series : all)
      present.insert(seriesKey(series));
    chosen_.intersect(present);
    if (chosen_.isEmpty())
      for (int i = 0; i < kMostSeries; ++i)
        chosen_.insert(seriesKey(all[i]));
    drawn.clear();
    for (const Series &series : all)
      if (chosen_.contains(seriesKey(series)))
        drawn << series;
  }
  pick_->setVisible(all.size() > kMostSeries);
  pick_->setText(
      tr("Series: %1 of %2").arg(drawn.size()).arg(all.size()));
  plot_->setSeries(drawn, data_.column, span, order,
                   int(all.size() - drawn.size()));
  exportFigure_->setEnabled(!drawn.isEmpty());
}

QString ComparePage::exportDir() const {
  if (!ctx_.study->isOpen())
    return {};
  const QString dir =
      QDir(ctx_.study->study().dir).filePath(QStringLiteral("exports"));
  QDir().mkpath(dir);
  return dir;
}

bool ComparePage::writeTable(const QString &path, QString *error) const {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) ||
      f.write(study::scoresCsv(scores_->rows()).toUtf8()) < 0) {
    if (error)
      *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path),
                                            f.errorString());
    return false;
  }
  return true;
}

bool ComparePage::writeFigure(const QString &path, QString *error) const {
  const auto fail = [&](const QString &why) {
    if (error)
      *error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), why);
    return false;
  };
  if (plot_->isEmpty())
    return fail(tr("there is no series to draw"));
  const QSizeF size(960, 540);
  if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
    // QSvgGenerator does not say when it cannot write: ask the file first,
    // and look at what is there afterwards.
    QFile probe(path);
    if (!probe.open(QIODevice::WriteOnly))
      return fail(probe.errorString());
    probe.close();
    QSvgGenerator svg;
    svg.setFileName(path);
    svg.setSize(size.toSize());
    svg.setViewBox(QRectF(QPointF(0, 0), size));
    QPainter p;
    if (!p.begin(&svg))
      return fail(tr("the SVG could not be started"));
    p.setFont(font());
    plot_->paintPlot(p, QRectF(QPointF(0, 0), size), true);
    p.end();
    if (QFileInfo(path).size() <= 0)
      return fail(tr("nothing was written"));
    return true;
  }
  QImage img((size * 2).toSize(), QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  QPainter p(&img);
  p.scale(2, 2);
  p.setFont(font());
  plot_->paintPlot(p, QRectF(QPointF(0, 0), size), true);
  p.end();
  if (!img.save(path))
    return fail(tr("the image could not be written"));
  return true;
}

void ComparePage::exportTable() {
  const QString name =
      QStringLiteral("scores_%1.csv").arg(recipe_->currentData().toString());
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export the score table"), QDir(exportDir()).filePath(name),
      tr("CSV (*.csv)"));
  if (path.isEmpty())
    return;
  QString error;
  if (!writeTable(path, &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  seriesNote_->setText(tr("Written: %1").arg(QDir::toNativeSeparators(path)));
  seriesNote_->show();
}

void ComparePage::exportFigure() {
  const QString name = QStringLiteral("%1_%2.png")
                           .arg(seriesRecipe_->currentData().toString(),
                                column_->currentText());
  QString path = QFileDialog::getSaveFileName(
      this, tr("Export the figure"), QDir(exportDir()).filePath(name),
      tr("PNG image (*.png);;SVG drawing (*.svg)"));
  if (path.isEmpty())
    return;
  if (QFileInfo(path).suffix().isEmpty())
    path += QStringLiteral(".png");
  QString error;
  if (!writeFigure(path, &error)) {
    QMessageBox::warning(this, tr("Export"), error);
    return;
  }
  seriesNote_->setText(tr("Written: %1").arg(QDir::toNativeSeparators(path)));
  seriesNote_->show();
}

} // namespace skg
