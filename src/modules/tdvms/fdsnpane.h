// Whole days from an FDSN service (KOERI by default) beside a campaign.
#pragma once

#include <QWidget>

#include "tdvms/campaign.h"

class QLineEdit;
class QDateEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QTableView;
class QLabel;

namespace skg {

class JobModel;
class LedgerModel;
class Job;

class FdsnPane : public QWidget {
  Q_OBJECT
public:
  explicit FdsnPane(JobModel *jobs, QWidget *parent = nullptr);
  void setCampaign(const tdvms::Campaign &c);
  void reload();
  // Fills the form (a case's station and span, sent from the Cases page).
  // An empty network keeps the one in the form.
  void prefill(const QString &station, const QString &network,
               const QDate &firstDay, const QDate &dayAfterLast);
  // The station code in the form, as typed.
  QString station() const;
  // Why Fetch cannot be pressed now, or nothing.
  QString blockedReason() const;

signals:
  // A pull was started that files its days under this folder.
  void fetchStarted(const QString &outDir);
  // The station code in the form changed.
  void stationChanged(const QString &code);

protected:
  void changeEvent(QEvent *e) override;

private:
  void start();
  void retranslate();
  void updateButtons();

  JobModel *jobs_;
  tdvms::Campaign campaign_;
  Job *running_ = nullptr;
  LedgerModel *model_;
  QTableView *table_;
  QLineEdit *station_;
  QLineEdit *network_;
  QLineEdit *channels_;
  QLineEdit *service_;
  QDateEdit *start_;
  QDateEdit *end_;
  QSpinBox *retries_;
  QCheckBox *planOnly_;
  QCheckBox *retryFailed_;
  QPushButton *go_;
  QPushButton *stop_;
  QLabel *where_;
  QLabel *why_;
};

} // namespace skg
