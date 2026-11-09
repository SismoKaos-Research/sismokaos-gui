// Table of every job of this session, newest last.
#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QTimer>

#include "jobs/job.h"
#include "jobs/processjob.h"
#include <QVariant>

namespace skg {

class JobModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column { Name, Tool, State, Progress, Elapsed, ColumnCount };

  explicit JobModel(QObject *parent = nullptr);

  // Takes ownership and starts the job.
  Job *add(Job *job);
  // Convenience: a ProcessJob for `spec`.
  Job *addProcess(JobSpec spec);
  Job *jobAt(int row) const;
  int rowOf(const Job *job) const;
  int runningCount() const;

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

signals:
  void jobAdded(Job *job);
  void jobFinished(Job *job);

private:
  void rowChanged(const Job *job, int firstCol, int lastCol);

  QList<Job *> jobs_;
  QTimer tick_; // refreshes the Elapsed column of running jobs
};

} // namespace skg
