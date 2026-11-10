#include "modules/analysis/labelspane.h"

#include "core/ui.h"

#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableView>
#include <QVBoxLayout>

#include "analysis/label.h"
#include "fx/npy.h"
#include "modules/features/npymodel.h"

namespace skg {

LabelsPane::LabelsPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
                       QWidget *parent)
    : QWidget(parent), ctx_(ctx), state_(std::move(state)) {
  info_ = new QLabel(this);
  info_->setWordWrap(true);
  result_ = new QLabel(this);
  result_->setWordWrap(true);
  winSec_ = new QDoubleSpinBox(this);
  winSec_->setRange(1, 86400);
  winSec_->setDecimals(0);
  winSec_->setValue(200);
  run_ = new QPushButton(this);
  model_ = new NpyModel(this);
  preview_ = new QTableView(this);
  preview_->setModel(model_);
  preview_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  auto *bar = new QHBoxLayout;
  bar->addWidget(ui::buddyLabel(tr("Window length (s)"), winSec_, this));
  bar->addWidget(winSec_);
  bar->addWidget(run_);
  bar->addStretch(1);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(info_);
  layout->addLayout(bar);
  layout->addWidget(result_);
  layout->addWidget(preview_, 1);
  connect(run_, &QPushButton::clicked, this, &LabelsPane::run);
  retranslate();
}

void LabelsPane::refresh() {
  const AnalysisState &s = *state_;
  winSec_->setValue(s.winSec);
  const bool ready = s.hasTable() && !s.events.empty();
  run_->setEnabled(ready);
  if (!s.hasTable())
    info_->setText(tr("Load a feature table first."));
  else if (s.events.empty())
    info_->setText(tr("Load the earthquake catalog to label %1.")
                       .arg(QFileInfo(s.tablePath).fileName()));
  else if (!s.position)
    info_->setText(tr("Without the station's position the labels carry "
                      "magnitudes and counts only; set the "
                      "station for distances and directions."));
  else
    info_->setText(
        tr("Every window gets the events whose origin time falls inside it: "
           "magnitude, position, "
           "distance and direction from %1, the distance class and a 0/1 flag.")
            .arg(s.position->code));
}

void LabelsPane::run() {
  AnalysisState &s = *state_;
  if (!s.hasTable() || s.events.empty())
    return;
  fx::NpyTable table = s.table;
  analysis::LabelOptions opt;
  opt.winSec = winSec_->value();
  opt.lang = s.lang;
  if (s.position) {
    opt.stationLat = s.position->lat;
    opt.stationLon = s.position->lon;
  }
  const analysis::LabelSummary sum =
      analysis::labelWindows(table, s.events, opt);
  QString base = s.tablePath;
  base.replace(QRegularExpression(QStringLiteral("(_etiketli)?\\.(npy|csv)$")),
               QString());
  const QString csv = base + QStringLiteral("_etiketli.csv"),
                npy = base + QStringLiteral("_etiketli.npy");
  bool ok = analysis::writeCsv(csv, table);
  try {
    ok = fx::writeNpy(npy, table) && ok;
  } catch (const std::exception &e) {
    QMessageBox::warning(this, tr("Labels"), QString::fromUtf8(e.what()));
    ok = false;
  }
  if (!ok) {
    QMessageBox::warning(this, tr("Labels"),
                         tr("Could not write %1.").arg(csv));
    return;
  }
  result_->setText(
      tr("%n window(s), %1 with events (%2%). Written: %3 and %4", nullptr,
         sum.windows)
          .arg(sum.withEvents)
          .arg(sum.windows ? 100.0 * sum.withEvents / sum.windows : 0.0, 0, 'f',
               1)
          .arg(QFileInfo(csv).fileName(), QFileInfo(npy).fileName()));
  try {
    model_->loadNpy(npy);
  } catch (const std::exception &e) {
    result_->setText(result_->text() + QStringLiteral("\n") +
                     QString::fromUtf8(e.what()));
  }
}

void LabelsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void LabelsPane::retranslate() {
  run_->setText(tr("Label the windows"));
  ui::setHelp(winSec_,
              tr("Length of one feature window in seconds. An earthquake is "
                 "assigned to the window its origin time falls in. Filled "
                 "from the table's run metadata when that is found."));
  run_->setToolTip(ui::helpText(
      tr("Adds to every window the earthquakes that happened in it "
         "(magnitude, position, distance and direction from the station, "
         "a 0/1 flag) and writes <name>_etiketli.csv and .npy beside the "
         "table.")));
}

} // namespace skg
