#include "modules/analysis/gridpane.h"

#include "core/ui.h"

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>

#include <set>

#include "analysis/grid.h"
#include "analysis/label.h"
#include "analysis/plot.h"
#include "analysis/series.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/analysis/figureview.h"

namespace skg {

using namespace analysis;

GridPane::GridPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
                   QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::move(state)) {
  tables_ = new QListWidget(this);
  tables_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  add_ = new QPushButton(this);
  remove_ = new QPushButton(this);
  build_ = new QPushButton(this);
  save_ = new QPushButton(this);
  column_ = new QComboBox(this);
  column_->setEditable(true);
  column_->setMinimumWidth(160);
  maxDays_ = new QSpinBox(this);
  maxDays_->setRange(1, 60);
  maxDays_->setValue(14);
  note_ = new QLabel(this);
  note_->setWordWrap(true);
  view_ = new FigureView(this);
  auto *bar = new QHBoxLayout;
  bar->addWidget(add_);
  bar->addWidget(remove_);
  bar->addSpacing(12);
  bar->addWidget(ui::buddyLabel(tr("Column"), column_, this));
  bar->addWidget(column_);
  bar->addWidget(ui::buddyLabel(tr("At most"), maxDays_, this));
  bar->addWidget(maxDays_);
  bar->addStretch(1);
  bar->addWidget(build_);
  bar->addWidget(save_);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(bar);
  layout->addWidget(tables_);
  layout->addWidget(note_);
  layout->addWidget(view_, 1);
  tables_->setMaximumHeight(110);
  connect(add_, &QPushButton::clicked, this, &GridPane::addTables);
  connect(remove_, &QPushButton::clicked, this, [this] {
    for (QListWidgetItem *it : tables_->selectedItems())
      delete it;
  });
  connect(build_, &QPushButton::clicked, this, &GridPane::build);
  connect(save_, &QPushButton::clicked, this, [this] {
    if (grid_.isNull())
      return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save the grid"),
        QStringLiteral("StationComparison_%1.png").arg(column_->currentText()),
        tr("PNG (*.png)"));
    if (!path.isEmpty() && !grid_.save(path))
      QMessageBox::warning(this, tr("Export"),
                           tr("Could not write %1.").arg(path));
  });
  retranslate();
}

void GridPane::refresh() {
  const AnalysisState &s = *state_;
  if (s.hasTable()) {
    const QString keep = column_->currentText();
    const QSignalBlocker b(column_);
    column_->clear();
    column_->addItems(plottableColumns(s.table));
    if (!keep.isEmpty())
      column_->setCurrentText(keep);
    if (!s.tablePath.isEmpty() &&
        tables_->findItems(s.tablePath, Qt::MatchExactly).isEmpty() &&
        tables_->count() == 0)
      tables_->addItem(s.tablePath);
  }
  save_->setEnabled(!grid_.isNull());
}

void GridPane::addTables() {
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("Add feature tables"),
      QFileInfo(state_->tablePath).absolutePath(),
      tr("Feature tables (*.npy *.csv);;All files (*)"));
  for (const QString &p : paths)
    if (tables_->findItems(p, Qt::MatchExactly).isEmpty())
      tables_->addItem(p);
}

void GridPane::build() {
  QStringList paths;
  for (int i = 0; i < tables_->count(); ++i)
    paths << tables_->item(i)->text();
  const QString column = column_->currentText().trimmed();
  if (paths.isEmpty() || column.isEmpty())
    return;
  const int maxDays = maxDays_->value();
  const QString lang = state_->lang;
  const auto events =
      state_->eventsAtStation; // the loaded station's events; others get none
  const double winSec = state_->winSec;
  QPointer<GridPane> self(this);
  auto *job = new TaskJob(
      tr("grid %1").arg(column), QStringLiteral("analysis"),
      tr("%n station(s), column %1", nullptr, paths.size()).arg(column),
      [=](TaskContext &ctx) -> int {
        struct Entry {
          QString station;
          MetricSeries series;
          std::vector<QDate> days;
        };
        std::vector<Entry> entries;
        for (const QString &p : paths) {
          try {
            const fx::NpyTable t = loadTable(p);
            Entry e;
            e.series = metricSeries(t, column, winSec);
            e.days = daysOf(e.series);
            // Station from the file name's prefix, or the folder.
            QString name = QFileInfo(p).completeBaseName();
            name.replace(
                QRegularExpression(QStringLiteral("_(chaos_)?features.*$")),
                QString());
            e.station = name;
            entries.push_back(std::move(e));
          } catch (const std::exception &ex) {
            ctx.log(
                tr("%1: %2 (skipped)").arg(p, QString::fromUtf8(ex.what())));
          }
        }
        if (entries.empty())
          return 1;
        std::set<QDate> common(entries[0].days.begin(), entries[0].days.end());
        for (size_t i = 1; i < entries.size(); ++i) {
          std::set<QDate> next;
          for (const QDate &d : entries[i].days)
            if (common.count(d))
              next.insert(d);
          common = next;
        }
        std::vector<QDate> days(common.begin(), common.end());
        if (int(days.size()) > maxDays)
          days.resize(size_t(maxDays));
        if (days.empty()) {
          ctx.log(tr("the tables share no day"));
          return 1;
        }
        GridSpec spec;
        spec.title = metricTitle(column, lang);
        for (const Entry &e : entries)
          spec.rowLabels << e.station;
        for (const QDate &d : days)
          spec.colLabels << d.toString(QStringLiteral("dd/MM"));
        int done = 0;
        const int total = int(entries.size() * days.size());
        QImage grid = composeGrid(spec, [&](int row, int col) {
          if (ctx.cancelled())
            return QImage();
          PlotStyle style;
          style.lang = lang;
          style.station = entries[size_t(row)].station;
          style.showDepthBar = false;
          style.showDistanceLegend = false;
          style.showAreaLegend = false;
          const QImage img =
              renderImage(dailySize(100), [&](QPainter &p, const QRectF &r) {
                drawDaily(p, r, entries[size_t(row)].series, days[size_t(col)],
                          events, style);
              });
          ++done;
          ctx.progress(double(done) / total,
                       QStringLiteral("%1/%2").arg(done).arg(total));
          return img;
        });
        if (ctx.cancelled())
          return 130;
        QMetaObject::invokeMethod(
            self.data(),
            [self, grid, days, entries = entries.size()] {
              if (!self)
                return;
              self->grid_ = grid;
              const QImage copy = grid;
              self->view_->setFigure(
                  [copy](QPainter &p, const QRectF &r) {
                    p.drawImage(r, copy);
                  },
                  grid.size());
              self->note_->setText(
                  tr("%n station(s) x %1 day(s)", nullptr, int(entries))
                      .arg(days.size()));
              self->save_->setEnabled(true);
            },
            Qt::QueuedConnection);
        return 0;
      });
  ctx_.jobs->add(job);
  note_->setText(tr("Building the grid; progress is in the Jobs panel."));
}

void GridPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void GridPane::retranslate() {
  add_->setText(tr("Add tables…"));
  remove_->setText(tr("Remove"));
  build_->setText(tr("Build grid"));
  ui::setPrimary(build_);
  save_->setText(tr("Save PNG…"));
  maxDays_->setSuffix(tr(" days"));
  add_->setToolTip(ui::helpText(
      tr("Adds feature tables of other stations to compare, one per "
         "station.")));
  remove_->setToolTip(
      ui::helpText(tr("Takes the selected table off the list.")));
  ui::setHelp(column_, tr("The column drawn for every station. It must "
                          "exist in every table."));
  ui::setHelp(maxDays_,
              tr("The grid shows at most this many of the days the "
                 "stations share, from the first one. More days make a "
                 "wider picture."));
  build_->setToolTip(ui::helpText(
      tr("Draws the daily figures of every station for the days they "
         "share, stations down and days across, as one picture.")));
  save_->setToolTip(
      ui::helpText(tr("Saves the grid as a PNG picture.")));
}

} // namespace skg
