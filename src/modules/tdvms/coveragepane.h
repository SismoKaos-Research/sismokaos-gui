// Coverage of a campaign's archives: per station and channel, the fraction
// present, the gaps with their reasons, and a timeline across the request.
#pragma once

#include <QWidget>

#include "tdvms/coverage.h"

class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QLabel;

namespace skg {

class JobModel;

class CoveragePane : public QWidget {
  Q_OBJECT
public:
  explicit CoveragePane(JobModel *jobs, QWidget *parent = nullptr);
  void setCampaign(const QString &ledgerPath, const QString &raw);

protected:
  void changeEvent(QEvent *e) override;

private:
  void compute();
  void show(const QList<tdvms::StationCoverage> &report);
  void showGaps(int row);
  void retranslate();

  JobModel *jobs_;
  QString ledgerPath_, raw_;
  QList<tdvms::StationCoverage> report_;
  QList<QPair<int, int>> rowIndex_; // table row -> (station, channel)
  QTableWidget *table_;
  QPlainTextEdit *gaps_;
  QPushButton *compute_;
  QCheckBox *refresh_;
  QLabel *summary_;
};

} // namespace skg
