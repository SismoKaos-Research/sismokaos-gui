#include "modules/study/comparepage.h"

#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QSvgGenerator>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/theme.h"
#include "core/ui.h"
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

QColor caseColor(const Theme *theme, const QStringList &order,
                 const QString &caseId) {
  const QList<QColor> colors = theme->seriesColors();
  const qsizetype i = order.indexOf(caseId);
  if (i < 0 || i >= colors.size())
    return theme->color(QStringLiteral("muted"));
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
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void ScoreTable::setRows(const QList<ScoreRow> &rows) {
  rows_ = rows;
  order_.clear();
  for (const ScoreRow &r : rows)
    if (!order_.contains(r.caseId))
      order_ << r.caseId;
  sortRows();
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

void ScoreTable::mousePressEvent(QMouseEvent *e) {
  if (e->position().y() > kHead)
    return;
  const QList<double> x = columnX();
  int column = -1;
  for (int i = 0; i < 7; ++i)
    if (e->position().x() >= x[i] && e->position().x() < x[i + 1])
      column = i;
  if (column == 1 || column == 4 || column < 0)
    return; // station and model are not sort keys
  descending_ = column == sortColumn_ ? !descending_ : column != 0;
  sortColumn_ = column;
  sortRows();
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
    p.setPen(col("border"));
    p.drawLine(QPointF(12, y), QPointF(width() - 12, y));
    p.setPen(QPen(col("surface"), 2));
    p.setBrush(caseColor(theme_, order_, row.caseId));
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
        done ? (row.bestModel.isEmpty() ? dash : row.bestModel)
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
                           double spanDays, const QStringList &order) {
  // Eight colours, in a fixed order; further series are not drawn.
  series_ = series.mid(0, 8);
  hidden_ = int(series.size() - series_.size());
  column_ = column;
  span_ = std::max(1.0, spanDays);
  order_ = order;
  update();
}

void SeriesPlot::paintEvent(QPaintEvent *) {
  QPainter p(this);
  paintPlot(p, QRectF(rect()));
}

void SeriesPlot::paintPlot(QPainter &p, const QRectF &rect) const {
  p.setRenderHint(QPainter::Antialiasing);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
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
  const QList<QColor> palette = theme_->seriesColors();
  bool sameCaseTwice = false;
  for (int i = 0; i < int(series_.size()); ++i)
    for (int j = 0; j < i; ++j)
      sameCaseTwice = sameCaseTwice || series_[i].caseId == series_[j].caseId;
  for (int i = 0; i < int(series_.size()); ++i)
    colors << (sameCaseTwice ? palette[i % palette.size()]
                             : caseColor(theme_, order_, series_[i].caseId));

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
    p.drawText(QPointF(lx, ly + 4), tr("+%1 more not drawn").arg(hidden_));
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
  scoresHint_->setObjectName(QStringLiteral("fieldLabel"));
  scoresHint_->setWordWrap(true);

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
  aligned_ = new QLabel(main);
  aligned_->setObjectName(QStringLiteral("fieldLabel"));
  exportFigure_ = new QPushButton(main);
  auto *mid = new QHBoxLayout;
  mid->setSpacing(8);
  mid->addWidget(seriesLabel_);
  mid->addWidget(seriesRecipe_);
  mid->addWidget(columnLabel_);
  mid->addWidget(column_);
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
  auto *bottom = new QHBoxLayout;
  bottom->setSpacing(14);
  bottom->addWidget(plot_, 7);
  bottom->addWidget(card, 4);

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(10);
  mainLayout->addLayout(top);
  mainLayout->addWidget(scroll);
  mainLayout->addWidget(scoresHint_);
  mainLayout->addLayout(mid);
  mainLayout->addLayout(bottom, 1);
  stack_->addWidget(main);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  connect(recipe_, &QComboBox::activated, this, &ComparePage::refreshScores);
  connect(seriesRecipe_, &QComboBox::activated, this, [this] {
    column_->clear();
    refreshSeries();
  });
  connect(column_, &QComboBox::activated, this, &ComparePage::refreshSeries);
  connect(exportTable_, &QPushButton::clicked, this, &ComparePage::exportTable);
  connect(exportFigure_, &QPushButton::clicked, this,
          &ComparePage::exportFigure);
  connect(ctx_.study, &study::StudyStore::changed, this, &ComparePage::refresh);
  connect(ctx_.jobs, &JobModel::jobFinished, this, [this] {
    if (isVisible())
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
  noStudy_->setText(tr("No study is open.\nOpen or start one on the Cases "
                       "page, or from the study selector above."));
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
  fittedNote_->setText(tr("What each case fitted for itself in this recipe; "
                          "everything else in the recipe is identical."));
}

void ComparePage::refresh() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  if (!open)
    return;
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
  refreshScores();
  refreshSeries();
}

void ComparePage::refreshScores() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const Recipe *r = s.findRecipe(recipe_->currentData().toString());
  QList<ScoreRow> rows;
  if (r)
    rows = study::scoreRows(s, *r);
  scores_->setRows(rows);
  // As tall as its rows, up to about six of them; then it scrolls.
  if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("scoreScroll")))
    scroll->setFixedHeight(std::min(scores_->minimumSizeHint().height(), 300) +
                           2);
  exportTable_->setEnabled(!rows.isEmpty());
  int done = 0;
  for (const ScoreRow &row : rows)
    done += row.state == study::CellState::Done ? 1 : 0;
  scoresHint_->setText(
      !r ? tr("No classification recipe yet. Add one on the Runs page; it "
              "needs a features recipe to take its table from.")
      : rows.isEmpty() ? tr("The study has no case with a station yet.")
                       : tr("%1 of %2 rows have a finished run. Click a column "
                            "heading to sort.")
                             .arg(done)
                             .arg(rows.size()));
}

void ComparePage::refreshSeries() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const Recipe *r = s.findRecipe(seriesRecipe_->currentData().toString());
  QStringList order;
  double span = 1;
  for (const study::Case &c : s.cases) {
    order << c.id;
    span = std::max(span, double(c.spanDays));
  }
  if (!r) {
    plot_->setSeries({}, QString(), span, order);
    fitted_->setRowCount(0);
    fitted_->setColumnCount(0);
    exportFigure_->setEnabled(false);
    return;
  }
  const QStringList columns = study::seriesColumns(s, *r);
  {
    const QString keep = column_->currentText();
    const QSignalBlocker block(column_);
    column_->clear();
    column_->addItems(columns);
    int i = column_->findText(keep);
    if (i < 0)
      i = column_->findText(QStringLiteral("b_value"));
    column_->setCurrentIndex(std::max(0, i));
  }
  const QString column = column_->currentText();
  const QList<Series> series =
      column.isEmpty() ? QList<Series>() : study::seriesOf(s, *r, column);
  plot_->setSeries(series, column, span, order);
  exportFigure_->setEnabled(!series.isEmpty());

  // The fitted values: one column per name any case recorded.
  const QList<study::FittedRow> rows = study::fittedRows(s, *r);
  QStringList keys;
  for (const study::FittedRow &row : rows)
    for (auto it = row.fitted.begin(); it != row.fitted.end(); ++it)
      if (!keys.contains(it.key()))
        keys << it.key();
  const bool stations = study::perStation(r->tool);
  QStringList heads{tr("Case")};
  if (stations)
    heads << tr("Station");
  heads << keys;
  fitted_->setColumnCount(int(heads.size()));
  fitted_->setHorizontalHeaderLabels(heads);
  fitted_->setRowCount(int(rows.size()));
  for (int i = 0; i < int(rows.size()); ++i) {
    int c = 0;
    fitted_->setItem(i, c++, new QTableWidgetItem(rows[i].caseName));
    if (stations)
      fitted_->setItem(i, c++, new QTableWidgetItem(rows[i].station));
    for (const QString &k : keys) {
      const QVariant v = rows[i].fitted.value(k);
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
}

void ComparePage::exportTable() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export the score table"), QStringLiteral("scores.csv"),
      tr("CSV (*.csv)"));
  if (path.isEmpty())
    return;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) ||
      f.write(study::scoresCsv(scores_->rows()).toUtf8()) < 0)
    QMessageBox::warning(this, tr("Export"), f.errorString());
}

void ComparePage::exportFigure() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export the figure"), QStringLiteral("series.png"),
      tr("PNG image (*.png);;SVG drawing (*.svg)"));
  if (path.isEmpty())
    return;
  const QSizeF size(960, 540);
  if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
    QSvgGenerator svg;
    svg.setFileName(path);
    svg.setSize(size.toSize());
    svg.setViewBox(QRectF(QPointF(0, 0), size));
    QPainter p(&svg);
    p.setFont(font());
    plot_->paintPlot(p, QRectF(QPointF(0, 0), size));
    return;
  }
  QImage img((size * 2).toSize(), QImage::Format_ARGB32_Premultiplied);
  img.fill(ctx_.theme->color(QStringLiteral("surface")));
  QPainter p(&img);
  p.scale(2, 2);
  p.setFont(font());
  plot_->paintPlot(p, QRectF(QPointF(0, 0), size));
  p.end();
  if (!img.save(path))
    QMessageBox::warning(this, tr("Export"), tr("Cannot write %1.").arg(path));
}

} // namespace skg
