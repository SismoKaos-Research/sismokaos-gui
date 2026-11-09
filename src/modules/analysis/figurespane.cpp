#include "modules/analysis/figurespane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "analysis/plot.h"
#include "analysis/series.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/analysis/figureview.h"

namespace skg {

using namespace analysis;

FiguresPane::FiguresPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
                         QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::move(state)) {
  column_ = new QComboBox(this);
  column_->setMinimumWidth(160);
  mode_ = new QComboBox(this);
  mode_->addItem(QString(), Daily);
  mode_->addItem(QString(), Combined);
  mode_->addItem(QString(), Centred);
  channel_ = new QComboBox(this);
  channel_->addItems({"", "E", "N", "Z"});
  channel_->setEditable(true);
  channel_->setMaximumWidth(70);
  day_ = new QDateEdit(this);
  day_->setCalendarPopup(true);
  day_->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  windowDays_ = new QSpinBox(this);
  windowDays_->setRange(1, 365);
  windowDays_->setValue(7);
  autoRange_ = new QCheckBox(this);
  autoRange_->setChecked(true);
  yMin_ = new QDoubleSpinBox(this);
  yMin_->setRange(-1e9, 1e9);
  yMin_->setDecimals(3);
  yMax_ = new QDoubleSpinBox(this);
  yMax_->setRange(-1e9, 1e9);
  yMax_->setDecimals(3);
  yMax_->setValue(1);
  dpi_ = new QSpinBox(this);
  dpi_->setRange(72, 600);
  dpi_->setValue(300);
  exportPng_ = new QPushButton(this);
  exportSvg_ = new QPushButton(this);
  exportAll_ = new QPushButton(this);
  note_ = new QLabel(this);
  note_->setWordWrap(true);
  view_ = new FigureView(this);

  auto *bar = new QHBoxLayout;
  bar->addWidget(column_);
  bar->addWidget(channel_);
  bar->addWidget(mode_);
  bar->addWidget(day_);
  bar->addWidget(windowDays_);
  bar->addSpacing(12);
  bar->addWidget(autoRange_);
  bar->addWidget(yMin_);
  bar->addWidget(yMax_);
  bar->addStretch(1);
  auto *exportBar = new QHBoxLayout;
  exportBar->addWidget(new QLabel(tr("PNG dpi"), this));
  exportBar->addWidget(dpi_);
  exportBar->addWidget(exportPng_);
  exportBar->addWidget(exportSvg_);
  exportBar->addWidget(exportAll_);
  exportBar->addStretch(1);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(bar);
  layout->addWidget(note_);
  layout->addWidget(view_, 1);
  layout->addLayout(exportBar);

  connect(column_, &QComboBox::currentIndexChanged, this, &FiguresPane::redraw);
  connect(channel_, &QComboBox::currentTextChanged, this, &FiguresPane::redraw);
  connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
    updateControls();
    redraw();
  });
  connect(day_, &QDateEdit::dateChanged, this, &FiguresPane::redraw);
  connect(windowDays_, &QSpinBox::valueChanged, this, &FiguresPane::redraw);
  connect(autoRange_, &QCheckBox::toggled, this, [this] {
    updateControls();
    redraw();
  });
  connect(yMin_, &QDoubleSpinBox::editingFinished, this, &FiguresPane::redraw);
  connect(yMax_, &QDoubleSpinBox::editingFinished, this, &FiguresPane::redraw);
  connect(exportPng_, &QPushButton::clicked, this,
          [this] { exportShown(false); });
  connect(exportSvg_, &QPushButton::clicked, this,
          [this] { exportShown(true); });
  connect(exportAll_, &QPushButton::clicked, this,
          &FiguresPane::exportAllDaily);
  retranslate();
  updateControls();
}

void FiguresPane::refresh() {
  const AnalysisState &s = *state_;
  const QString keep = column_->currentText();
  const QSignalBlocker b(column_);
  column_->clear();
  if (s.hasTable()) {
    column_->addItems(plottableColumns(s.table));
    const int i = column_->findText(keep);
    column_->setCurrentIndex(i >= 0 ? i : 0);
    if (s.spanEnd > s.spanStart) {
      const QDate first =
          QDateTime::fromSecsSinceEpoch(qint64(s.spanStart), QTimeZone::utc())
              .date();
      const QDate last =
          QDateTime::fromSecsSinceEpoch(qint64(s.spanEnd - 1), QTimeZone::utc())
              .date();
      day_->setDateRange(first, last);
      if (day_->date() < first || day_->date() > last)
        day_->setDate(s.mainEvent ? QDateTime::fromSecsSinceEpoch(
                                        qint64(s.mainEvent->event.epoch),
                                        QTimeZone::utc())
                                        .date()
                                  : first);
    }
  }
  updateControls();
  redraw();
}

void FiguresPane::updateControls() {
  const int mode = mode_->currentData().toInt();
  day_->setVisible(mode == Daily);
  windowDays_->setVisible(mode == Centred);
  yMin_->setEnabled(!autoRange_->isChecked());
  yMax_->setEnabled(!autoRange_->isChecked());
  const bool have = state_->hasTable() && column_->count() > 0;
  exportPng_->setEnabled(have);
  exportSvg_->setEnabled(have);
  exportAll_->setEnabled(have);
}

void FiguresPane::redraw() {
  const AnalysisState &s = *state_;
  if (!s.hasTable() || column_->currentText().isEmpty()) {
    view_->clearFigure();
    note_->setText(tr("Load a feature table to draw its columns."));
    return;
  }
  MetricSeries series;
  try {
    series = metricSeries(s.table, column_->currentText(), s.winSec);
  } catch (const std::exception &e) {
    note_->setText(QString::fromUtf8(e.what()));
    view_->clearFigure();
    return;
  }
  PlotStyle style;
  style.lang = s.lang;
  style.station = s.station;
  style.channel = channel_->currentText().trimmed();
  if (!autoRange_->isChecked())
    style.yRange = YRange{yMin_->value(), yMax_->value()};
  const auto events = s.eventsAtStation;
  const auto main = s.mainEvent;
  const int mode = mode_->currentData().toInt();
  const auto days = daysOf(series);
  QString note;
  if (mode == Daily) {
    const QDate day = day_->date();
    view_->setFigure(
        [series, day, events, style](QPainter &p, const QRectF &r) {
          drawDaily(p, r, series, day, events, style);
        },
        dailySize(150));
    const Areas a = areasOf(daySlice(series, day));
    note = tr("%1: positive area %2, negative area %3 (value·minutes)")
               .arg(day.toString(Qt::ISODate))
               .arg(a.positive, 0, 'g', 4)
               .arg(a.negative, 0, 'g', 4);
  } else if (mode == Combined) {
    view_->setFigure(
        [series, days, events, main, style](QPainter &p, const QRectF &r) {
          drawCombined(p, r, series, days, events, main, style);
        },
        combinedSize(150));
    note = tr("%n day(s) end to end", nullptr, int(days.size()));
  } else {
    if (!main) {
      view_->clearFigure();
      note_->setText(
          tr("No M5+ event in the period, so there is nothing to centre on."));
      return;
    }
    const QDate eventDay = QDateTime::fromSecsSinceEpoch(
                               qint64(main->event.epoch), QTimeZone::utc())
                               .date();
    const auto window = centredDays(eventDay, days, windowDays_->value());
    const EventAtStation me = *main;
    view_->setFigure(
        [series, window, events, me, style](QPainter &p, const QRectF &r) {
          drawCentred(p, r, series, window, events, me, style);
        },
        centredSize(150));
    note = tr("%n day(s) around the M%1 of %2", nullptr, int(window.size()))
               .arg(main->event.mag, 0, 'f', 1)
               .arg(eventDay.toString(Qt::ISODate));
  }
  if (s.events.empty())
    note += tr(" (no catalog: no earthquakes drawn)");
  note_->setText(note);
}

void FiguresPane::exportShown(bool svg) {
  if (!view_->hasFigure())
    return;
  const QString stem =
      QStringLiteral("%1_%2_%3")
          .arg(state_->station.isEmpty() ? "figure" : state_->station,
               column_->currentText(), mode_->currentText().toLower());
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save the figure"), stem + (svg ? ".svg" : ".png"),
      svg ? tr("SVG (*.svg)") : tr("PNG (*.png)"));
  if (path.isEmpty())
    return;
  const bool ok = svg ? view_->exportSvg(path)
                      : view_->exportPng(path, dpi_->value() / 150.0);
  if (!ok)
    QMessageBox::warning(this, tr("Export"),
                         tr("Could not write %1.").arg(path));
}

void FiguresPane::exportAllDaily() {
  const AnalysisState &s = *state_;
  if (!s.hasTable() || column_->currentText().isEmpty())
    return;
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Folder for the daily figures"),
      QFileInfo(s.tablePath).absolutePath());
  if (dir.isEmpty())
    return;
  MetricSeries series;
  try {
    series = metricSeries(s.table, column_->currentText(), s.winSec);
  } catch (const std::exception &e) {
    QMessageBox::warning(this, tr("Export"), QString::fromUtf8(e.what()));
    return;
  }
  PlotStyle style;
  style.lang = s.lang;
  style.station = s.station;
  style.channel = channel_->currentText().trimmed();
  if (!autoRange_->isChecked())
    style.yRange = YRange{yMin_->value(), yMax_->value()};
  const auto events = s.eventsAtStation;
  const auto main = s.mainEvent;
  const double scale = dpi_->value() / 150.0;
  const QString column = column_->currentText();
  const QString prefix = QDir(dir).filePath(QStringLiteral("%1_%2%3").arg(
      s.station.isEmpty() ? "figure" : s.station, column,
      style.channel.isEmpty() ? "" : "_" + style.channel));
  auto *job = new TaskJob(
      tr("figures %1").arg(column), QStringLiteral("analysis"),
      tr("daily figures of %1 into %2").arg(column, dir),
      [=](TaskContext &ctx) -> int {
        const auto days = daysOf(series);
        int n = 0;
        for (const QDate &d : days) {
          if (ctx.cancelled())
            return 130;
          const QImage img = renderImage(
              dailySize(150) * scale, [&](QPainter &p, const QRectF &r) {
                drawDaily(p, r, series, d, events, style);
              });
          const QString path =
              prefix + '_' + d.toString(QStringLiteral("yyyy_MM_dd")) + ".png";
          if (!img.save(path)) {
            ctx.log(tr("could not write %1").arg(path));
            return 1;
          }
          ++n;
          ctx.progress(double(n) / double(days.size()),
                       d.toString(Qt::ISODate));
        }
        const QImage all = renderImage(
            combinedSize(150) * scale, [&](QPainter &p, const QRectF &r) {
              drawCombined(p, r, series, days, events, main, style);
            });
        all.save(prefix + QStringLiteral("_all_days.png"));
        ctx.log(tr("%n daily figure(s) and the combined one written to %1",
                   nullptr, n)
                    .arg(dir));
        return 0;
      });
  ctx_.jobs->add(job);
}

void FiguresPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void FiguresPane::retranslate() {
  mode_->setItemText(0, tr("One day"));
  mode_->setItemText(1, tr("All days"));
  mode_->setItemText(2, tr("Around the main event"));
  autoRange_->setText(tr("automatic range"));
  exportPng_->setText(tr("PNG…"));
  exportSvg_->setText(tr("SVG…"));
  exportAll_->setText(tr("All daily figures…"));
  windowDays_->setSuffix(tr(" days"));
  channel_->setToolTip(tr("Channel, for the title"));
}

} // namespace skg
