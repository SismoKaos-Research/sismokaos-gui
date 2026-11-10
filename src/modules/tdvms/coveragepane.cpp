#include "modules/tdvms/coveragepane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"

namespace skg {

using namespace tdvms;

CoveragePane::CoveragePane(JobModel *jobs, QWidget *parent)
    : QWidget(parent), jobs_(jobs) {
  compute_ = new QPushButton(this);
  refresh_ = new QCheckBox(this);
  summary_ = new QLabel(this);
  table_ = new QTableWidget(0, 6, this);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  gaps_ = new QPlainTextEdit(this);
  gaps_->setReadOnly(true);
  gaps_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  auto *top = new QHBoxLayout;
  top->addWidget(compute_);
  top->addWidget(refresh_);
  top->addWidget(summary_, 1);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(table_, 3);
  layout->addWidget(gaps_, 2);

  connect(compute_, &QPushButton::clicked, this, &CoveragePane::compute);
  connect(table_, &QTableWidget::currentCellChanged, this,
          [this](int row, int, int, int) { showGaps(row); });
  retranslate();
}

void CoveragePane::setCampaign(const QString &ledgerPath, const QString &raw) {
  ledgerPath_ = ledgerPath;
  raw_ = raw;
  report_.clear();
  rowIndex_.clear();
  table_->setRowCount(0);
  gaps_->clear();
  summary_->setText(raw.isEmpty() ? QString()
                                  : tr("Archives under %1").arg(raw));
}

void CoveragePane::compute() {
  if (ledgerPath_.isEmpty())
    return;
  const QString ledgerPath = ledgerPath_, raw = raw_;
  const bool refresh = refresh_->isChecked();
  QPointer<CoveragePane> self(this);
  auto *job = new TaskJob(
      tr("coverage"), QStringLiteral("tdvms"),
      tr("read the archives under %1").arg(raw),
      [ledgerPath, raw, refresh, self](TaskContext &ctx) -> int {
        Ledger ledger(ledgerPath);
        const auto report = coverageReport(
            ledger, raw, {}, refresh,
            [&](int done, int total, const QString &name) {
              if (total)
                ctx.progress(double(done) / total,
                             name.isEmpty() ? tr("done")
                                            : tr("reading %1").arg(name));
            });
        int channels = 0;
        for (const StationCoverage &st : report)
          channels += st.channels.size();
        ctx.log(tr("%n station(s), %1 channel(s)", nullptr, report.size())
                    .arg(channels));
        QMetaObject::invokeMethod(
            self.data(),
            [self, report] {
              if (self)
                self->show(report);
            },
            Qt::QueuedConnection);
        return 0;
      });
  jobs_->add(job);
}

void CoveragePane::show(const QList<StationCoverage> &report) {
  report_ = report;
  rowIndex_.clear();
  table_->setRowCount(0);
  int stations = 0, channels = 0;
  for (int si = 0; si < report.size(); ++si) {
    const StationCoverage &st = report[si];
    ++stations;
    if (st.channels.isEmpty()) {
      const int row = table_->rowCount();
      table_->insertRow(row);
      table_->setItem(row, 0, new QTableWidgetItem(st.code));
      table_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("—")));
      table_->setItem(row, 5, new QTableWidgetItem(tr("no archives")));
      rowIndex_.append({si, -1});
      continue;
    }
    for (int ci = 0; ci < st.channels.size(); ++ci) {
      const ChannelCoverage &ch = st.channels[ci];
      ++channels;
      const int row = table_->rowCount();
      table_->insertRow(row);
      const Gap *big = nullptr;
      for (const Gap &g : ch.gaps)
        if (g.seconds() >= 1 && (!big || g.seconds() > big->seconds()))
          big = &g;
      QString bar;
      for (const TimelineCell &c : timeline(st, &ch, 48))
        bar += c.glyph;
      table_->setItem(row, 0, new QTableWidgetItem(st.code));
      table_->setItem(row, 1, new QTableWidgetItem(ch.label()));
      table_->setItem(row, 2,
                      new QTableWidgetItem(QStringLiteral("%1%").arg(
                          ch.fraction() * 100, 0, 'f', 1)));
      table_->setItem(row, 3,
                      new QTableWidgetItem(formatSeconds(ch.requested)));
      table_->setItem(row, 4,
                      new QTableWidgetItem(QString::number(ch.gaps.size())));
      auto *barItem = new QTableWidgetItem(bar);
      barItem->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
      barItem->setToolTip(big ? tr("largest gap: %1 at %2 (%3)")
                                    .arg(formatSeconds(big->seconds()),
                                         formatTime(big->start).left(16),
                                         big->why)
                              : tr("no gaps"));
      table_->setItem(row, 5, barItem);
      rowIndex_.append({si, ci});
    }
  }
  table_->resizeColumnsToContents();
  table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
  summary_->setText(
      tr("%n station(s), %1 channel(s); archives under %2", nullptr, stations)
          .arg(channels)
          .arg(raw_));
  if (table_->rowCount())
    table_->selectRow(0);
}

void CoveragePane::showGaps(int row) {
  gaps_->clear();
  if (row < 0 || row >= rowIndex_.size())
    return;
  const auto [si, ci] = rowIndex_[row];
  if (ci < 0)
    return;
  const ChannelCoverage &ch = report_[si].channels[ci];
  QStringList lines;
  for (const Gap &g : ch.gaps)
    if (g.seconds() >= 1)
      lines << QStringLiteral("%1 → %2  %3  %4")
                   .arg(formatTime(g.start), formatTime(g.end))
                   .arg(formatSeconds(g.seconds()), 8)
                   .arg(g.why);
  gaps_->setPlainText(lines.isEmpty() ? tr("no gaps of a second or more")
                                      : lines.join('\n'));
}

void CoveragePane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void CoveragePane::retranslate() {
  compute_->setText(tr("Read the archives"));
  refresh_->setText(tr("ignore the cache"));
  table_->setHorizontalHeaderLabels({tr("Station"), tr("Channel"),
                                     tr("Covered"), tr("Of"), tr("Gaps"),
                                     tr("Timeline")});
  gaps_->setPlaceholderText(tr("Select a channel to list its gaps."));
  compute_->setToolTip(ui::helpText(
      tr("Reads the headers of every miniSEED record in the downloaded "
         "archives (not the samples) and shows how much of the requested "
         "time each channel covers.")));
  ui::setHelp(refresh_,
              tr("Rereads every archive instead of using the results "
                 "cached from the last time. Slower; use it when archives "
                 "were replaced."));
}

} // namespace skg
