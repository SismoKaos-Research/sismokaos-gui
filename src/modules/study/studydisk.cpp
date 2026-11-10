#include "modules/study/studydisk.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

#include "core/ui.h"
#include "modules/study/runtext.h"
#include "study/store.h"

namespace skg {

using study::DiskItem;
using study::DiskKind;

namespace {

// Sorts by the number it holds, not by its text.
class SizeItem : public QTableWidgetItem {
public:
  explicit SizeItem(qint64 bytes) : QTableWidgetItem(RunWords::bytes(bytes)) {
    setData(Qt::UserRole, bytes);
    setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }
  bool operator<(const QTableWidgetItem &other) const override {
    return data(Qt::UserRole).toLongLong() <
           other.data(Qt::UserRole).toLongLong();
  }
};

QString toolText(const QString &tool) {
  if (tool == study::kToolIndicators)
    return StudyDiskDialog::tr("Indicators");
  if (tool == study::kToolFeatures)
    return StudyDiskDialog::tr("Features");
  if (tool == study::kToolClassification)
    return StudyDiskDialog::tr("Classification");
  return tool;
}

QString kindText(const DiskItem &d) {
  if (d.live)
    return StudyDiskDialog::tr("◔  in use by a run");
  switch (d.kind) {
  case DiskKind::Result:
    return StudyDiskDialog::tr("✓  result");
  case DiskKind::Working:
    return StudyDiskDialog::tr("⚠  leftover: working folder of a run that "
                               "did not end");
  case DiskKind::Failed:
    return StudyDiskDialog::tr("⚠  leftover: files of a failed run");
  case DiskKind::Calibration:
    return StudyDiskDialog::tr("kept calibration");
  case DiskKind::Unpacked:
    return StudyDiskDialog::tr("⚠  leftover: unpacked archives");
  case DiskKind::Orphan:
    return StudyDiskDialog::tr("⚠  leftover: no longer in the study");
  }
  return {};
}

} // namespace

StudyDiskDialog::StudyDiskDialog(AppContext &ctx, QWidget *parent)
    : QDialog(parent), ctx_(ctx) {
  setObjectName(QStringLiteral("studyDisk"));
  setWindowTitle(tr("Study on disk"));
  confirm = [this](const QString &title, const QString &text) {
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
  };
  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("diskSummary"));
  summary_->setWordWrap(true);
  note_ = new QLabel(this);
  note_->setObjectName(QStringLiteral("fieldLabel"));
  note_->setWordWrap(true);
  note_->setText(
      tr("A leftover is something no finished result of the study needs: the "
         "working folder of a run that crashed, what a failed run left "
         "(with its log), archives a run unpacked, and results of cases, "
         "stations or recipes that were removed from the study. Finished "
         "results and kept calibrations are never removed here."));
  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("diskTable"));
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({tr("Case"), tr("Station"), tr("Tool"),
                                     tr("Recipe"), tr("What"), tr("Size")});
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
  table_->setSortingEnabled(true);
  table_->setToolTip(ui::helpText(
      tr("One line per result folder. Click a heading to sort (by size, "
         "say); a double click opens the folder.")));

  clean_ = new QPushButton(tr("Clean up leftovers…"), this);
  clean_->setObjectName(QStringLiteral("cleanLeftovers"));
  clean_->setToolTip(ui::helpText(
      tr("Lists exactly what would be removed, with sizes, and removes it "
         "after you confirm. Finished results, kept calibrations and "
         "anything of a run that is queued or going on are left alone.")));
  auto *again = new QPushButton(tr("Measure again"), this);
  again->setToolTip(
      ui::helpText(tr("Reads the sizes from the disk again.")));
  auto *open = new QPushButton(tr("Open the study folder"), this);
  open->setToolTip(
      ui::helpText(tr("Opens the study's folder in the file manager.")));
  auto *close = new QPushButton(tr("Close"), this);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(clean_);
  buttons->addWidget(again);
  buttons->addWidget(open);
  buttons->addStretch(1);
  buttons->addWidget(close);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(summary_);
  layout->addWidget(table_, 1);
  layout->addWidget(note_);
  layout->addLayout(buttons);
  resize(920, 560);

  connect(close, &QPushButton::clicked, this, &QDialog::close);
  connect(again, &QPushButton::clicked, this, &StudyDiskDialog::refresh);
  connect(clean_, &QPushButton::clicked, this, [this] { cleanUp(); });
  connect(open, &QPushButton::clicked, this, [this] {
    if (ctx_.study && ctx_.study->isOpen())
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(ctx_.study->study().dir));
  });
  connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    if (const QTableWidgetItem *it = table_->item(row, 0))
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
  });
  refresh();
}

QList<DiskItem> StudyDiskDialog::leftovers() const {
  QList<DiskItem> out;
  for (const DiskItem &d : items_)
    if (d.leftover && !d.live)
      out << d;
  return out;
}

void StudyDiskDialog::refresh() {
  if (!ctx_.study || !ctx_.study->isOpen())
    return;
  loading_ = true;
  const int generation = ++generation_;
  summary_->setText(tr("Measuring the study's folders…"));
  clean_->setEnabled(false);
  const study::Study snapshot = ctx_.study->study();
  const auto result = std::make_shared<QList<DiskItem>>();
  QThread *worker = QThread::create(
      [snapshot, result] { *result = study::studyOnDisk(snapshot); });
  const QPointer<StudyDiskDialog> self(this);
  connect(worker, &QThread::finished, this, [self, result, generation] {
    if (!self || generation != self->generation_)
      return;
    self->items_ = *result;
    self->loading_ = false;
    self->fill();
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void StudyDiskDialog::fill() {
  table_->setSortingEnabled(false);
  table_->setRowCount(int(items_.size()));
  qint64 results = 0, left = 0, other = 0;
  for (int i = 0; i < int(items_.size()); ++i) {
    const DiskItem &d = items_[i];
    if (d.leftover && !d.live)
      left += d.bytes;
    else if (d.kind == DiskKind::Result)
      results += d.bytes;
    else
      other += d.bytes;
    const QStringList cols{d.caseName, d.station, toolText(d.tool),
                           d.recipeName, kindText(d)};
    for (int j = 0; j < int(cols.size()); ++j) {
      auto *item = new QTableWidgetItem(cols[j]);
      item->setData(Qt::UserRole, d.path);
      item->setToolTip(QDir::toNativeSeparators(d.path));
      table_->setItem(i, j, item);
    }
    auto *size = new SizeItem(d.bytes);
    size->setToolTip(QDir::toNativeSeparators(d.path));
    table_->setItem(i, 5, size);
  }
  table_->setSortingEnabled(true);
  table_->sortByColumn(5, Qt::DescendingOrder);
  table_->resizeColumnsToContents();
  const qint64 freeNow =
      study::freeBytesAt(ctx_.study->isOpen() ? ctx_.study->study().dir
                                              : QString());
  QString text =
      items_.isEmpty()
          ? tr("This study has no run folders yet.")
          : tr("Results %1 · leftovers %2 · in use or kept %3")
                .arg(RunWords::bytes(results), RunWords::bytes(left),
                     RunWords::bytes(other));
  if (freeNow >= 0)
    text += QStringLiteral(" · ") +
            tr("%1 free on the study's disk").arg(RunWords::bytes(freeNow));
  summary_->setText(text);
  clean_->setEnabled(!leftovers().isEmpty());
}

qint64 StudyDiskDialog::cleanUp() {
  if (loading_ || !ctx_.study || !ctx_.study->isOpen())
    return 0;
  const QList<DiskItem> gone = leftovers();
  if (gone.isEmpty())
    return 0;
  const QString root = ctx_.study->study().dir;
  qint64 total = 0;
  QStringList lines, paths;
  for (const DiskItem &d : gone) {
    total += d.bytes;
    paths << d.path;
    lines << QStringLiteral("%1  (%2)").arg(
        QDir::toNativeSeparators(QDir(root).relativeFilePath(d.path)),
        RunWords::bytes(d.bytes));
  }
  constexpr int kShown = 25;
  QString list = lines.mid(0, kShown).join(QLatin1Char('\n'));
  if (lines.size() > kShown)
    list += QLatin1Char('\n') +
            tr("… and %n more", nullptr, int(lines.size()) - kShown);
  if (!confirm(tr("Clean up leftovers"),
               tr("Remove %n leftover folder(s), %1 in all, from the study "
                  "folder? This cannot be undone.\n\n%2\n\nFinished results "
                  "are not touched.",
                  nullptr, int(lines.size()))
                   .arg(RunWords::bytes(total), list)))
    return 0;
  QStringList failed;
  const qint64 freed = study::cleanUp(ctx_.study->study(), paths, &failed);
  if (!failed.isEmpty())
    QMessageBox::warning(
        this, tr("Clean up leftovers"),
        tr("%n folder(s) could not be removed (in use by a run, or by "
           "another program):\n%1",
           nullptr, int(failed.size()))
            .arg(failed.join(QLatin1Char('\n'))));
  refresh();
  return freed;
}

void showStudyOnDisk(AppContext &ctx, QWidget *parent) {
  if (!ctx.study || !ctx.study->isOpen())
    return;
  auto *dialog = new StudyDiskDialog(ctx, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

} // namespace skg
