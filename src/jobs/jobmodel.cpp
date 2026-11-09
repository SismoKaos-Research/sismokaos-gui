#include "jobs/jobmodel.h"

#include <QBrush>
#include <QColor>

namespace skg {

JobModel::JobModel(QObject *parent) : QAbstractTableModel(parent) {
  tick_.setInterval(1000);
  connect(&tick_, &QTimer::timeout, this, [this] {
    for (const Job *j : jobs_)
      if (j->state() == Job::State::Running)
        rowChanged(j, Elapsed, Elapsed);
  });
}

Job *JobModel::addProcess(JobSpec spec) {
  return add(new ProcessJob(std::move(spec)));
}

Job *JobModel::add(Job *job) {
  job->setParent(this);
  beginInsertRows({}, jobs_.size(), jobs_.size());
  jobs_ << job;
  endInsertRows();
  connect(job, &Job::progressChanged, this,
          [this, job] { rowChanged(job, Progress, Progress); });
  connect(job, &Job::stateChanged, this, [this, job] {
    rowChanged(job, State, Elapsed);
    if (job->state() != Job::State::Running) {
      emit jobFinished(job);
      if (runningCount() == 0)
        tick_.stop();
    }
  });
  emit jobAdded(job);
  job->start();
  if (!tick_.isActive())
    tick_.start();
  return job;
}

Job *JobModel::jobAt(int row) const {
  return row >= 0 && row < jobs_.size() ? jobs_[row] : nullptr;
}

int JobModel::rowOf(const Job *job) const {
  return jobs_.indexOf(const_cast<Job *>(job));
}

int JobModel::runningCount() const {
  int n = 0;
  for (const Job *j : jobs_)
    n += j->state() == Job::State::Running;
  return n;
}

int JobModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : jobs_.size();
}

int JobModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

static QString elapsedText(qint64 ms) {
  const qint64 s = ms / 1000;
  if (s < 60)
    return QStringLiteral("%1 s").arg(s);
  if (s < 3600)
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
  return QStringLiteral("%1:%2:%3")
      .arg(s / 3600)
      .arg(s % 3600 / 60, 2, 10, QChar('0'))
      .arg(s % 60, 2, 10, QChar('0'));
}

QVariant JobModel::data(const QModelIndex &index, int role) const {
  const Job *job = jobAt(index.row());
  if (!job)
    return {};
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case Name:
      return job->name();
    case Tool:
      return job->tool();
    case State:
      if (job->state() == Job::State::Failed && job->exitCode() >= 0)
        return tr("failed (%1)").arg(job->exitCode());
      return stateName(job->state());
    case Progress: {
      const ProgressState &p = job->progress();
      if (p.fraction < 0)
        return p.text;
      return QStringLiteral("%1%  %2").arg(int(p.fraction * 100)).arg(p.text);
    }
    case Elapsed:
      return elapsedText(job->elapsedMs());
    }
  }
  if (role == Qt::ForegroundRole && index.column() == State) {
    switch (job->state()) {
    case Job::State::Done:
      return QBrush(QColor(0x2e, 0x8b, 0x57));
    case Job::State::Failed:
      return QBrush(QColor(0xc0, 0x39, 0x2b));
    case Job::State::Cancelled:
      return QBrush(QColor(0x80, 0x80, 0x80));
    default:
      break;
    }
  }
  if (role == Qt::ToolTipRole)
    return job->description();
  return {};
}

QVariant JobModel::headerData(int section, Qt::Orientation o, int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case Name:
    return tr("Job");
  case Tool:
    return tr("Tool");
  case State:
    return tr("State");
  case Progress:
    return tr("Progress");
  case Elapsed:
    return tr("Elapsed");
  }
  return {};
}

void JobModel::rowChanged(const Job *job, int firstCol, int lastCol) {
  const int row = rowOf(job);
  if (row < 0)
    return;
  emit dataChanged(index(row, firstCol), index(row, lastCol));
}

} // namespace skg
