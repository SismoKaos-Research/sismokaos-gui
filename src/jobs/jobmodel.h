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
  enum Column { Name, Tool, State, Progress, Started, Elapsed, ColumnCount };

  explicit JobModel(QObject *parent = nullptr);

  // Takes ownership and starts the job.
  Job *add(Job *job);
  // Convenience: a ProcessJob for `spec`.
  Job *addProcess(JobSpec spec);
  Job *jobAt(int row) const;
  int rowOf(const Job *job) const;
  int runningCount() const;
  // Jobs that are waiting for their turn (State::Queued).
  int queuedCount() const;
  // Jobs that have ended, whatever the outcome.
  int finishedCount() const;
  // Takes the ended jobs out of the table. Their objects stay alive (pages
  // keep pointers to the job they last started), only the rows go.
  void clearFinished();
  // "12 s", "3:07", "2:14:09".
  static QString elapsedText(qint64 ms);

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
  QList<Job *> cleared_; // out of the table, still owned
  QTimer tick_; // refreshes the Elapsed column of running jobs
};

} // namespace skg
