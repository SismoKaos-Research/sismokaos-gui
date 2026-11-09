#include "jobs/jobspanel.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTableView>
#include <QVBoxLayout>

#include "jobs/job.h"
#include "jobs/jobmodel.h"

namespace skg {

JobsPanel::JobsPanel(JobModel *model, QWidget *parent)
    : QWidget(parent), model_(model) {
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(JobModel::Name,
                                                   QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(JobModel::Progress,
                                                   QHeaderView::Stretch);
  table_->verticalHeader()->hide();

  log_ = new QPlainTextEdit(this);
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(20000);
  log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

  cancel_ = new QPushButton(this);
  copy_ = new QPushButton(this);
  summary_ = new QLabel(this);
  summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);

  auto *buttons = new QHBoxLayout;
  buttons->addWidget(summary_, 1);
  buttons->addWidget(copy_);
  buttons->addWidget(cancel_);

  auto *right = new QWidget(this);
  auto *rightLayout = new QVBoxLayout(right);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->addWidget(log_, 1);
  rightLayout->addLayout(buttons);

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(table_);
  split->addWidget(right);
  split->setStretchFactor(0, 2);
  split->setStretchFactor(1, 3);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &JobsPanel::onSelectionChanged);
  connect(model_, &JobModel::jobAdded, this,
          [this](Job *job) { table_->selectRow(model_->rowOf(job)); });
  connect(model_, &JobModel::jobFinished, this, [this](Job *job) {
    if (job == current_)
      updateButtons();
  });
  connect(cancel_, &QPushButton::clicked, this, [this] {
    if (current_)
      current_->cancel();
  });
  connect(copy_, &QPushButton::clicked, this, [this] {
    if (current_)
      QApplication::clipboard()->setText(current_->log().join('\n'));
  });
  retranslate();
  updateButtons();
}

void JobsPanel::showJob(Job *job) {
  if (current_)
    disconnect(current_, nullptr, this, nullptr);
  current_ = job;
  log_->clear();
  if (!job) {
    summary_->clear();
    updateButtons();
    return;
  }
  log_->setPlainText(job->log().join('\n'));
  log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
  connect(job, &Job::lineReceived, this, &JobsPanel::appendLine);
  connect(job, &Job::stateChanged, this, &JobsPanel::updateButtons);
  summary_->setText(job->commandLine());
  updateButtons();
}

void JobsPanel::onSelectionChanged() {
  const auto rows = table_->selectionModel()->selectedRows();
  showJob(rows.isEmpty() ? nullptr : model_->jobAt(rows.first().row()));
}

void JobsPanel::appendLine(const QString &line) {
  const bool atBottom = log_->verticalScrollBar()->value() ==
                        log_->verticalScrollBar()->maximum();
  log_->appendPlainText(line);
  if (atBottom)
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

void JobsPanel::updateButtons() {
  cancel_->setEnabled(current_ && current_->state() == Job::State::Running);
  copy_->setEnabled(current_ != nullptr);
}

void JobsPanel::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void JobsPanel::retranslate() {
  cancel_->setText(tr("Cancel"));
  copy_->setText(tr("Copy log"));
  log_->setPlaceholderText(tr("Select a job to see its output."));
}

} // namespace skg
