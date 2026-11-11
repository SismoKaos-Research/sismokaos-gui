// The editor of a feature-extraction Config: general, chaos and calibration
// settings and the band table. setConfig() fills the widgets, config() reads
// them back on top of the last config set (fields the editor does not show,
// such as the calibrated SampEn tolerances, pass through unchanged).
#pragma once

#include <QList>
#include <QWidget>

#include "fx/config.h"

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QTableView;
class QLabel;
class QGroupBox;

namespace skg {

class BandsModel;

class ConfigPane : public QWidget {
  Q_OBJECT
public:
  explicit ConfigPane(QWidget *parent = nullptr);

  void setConfig(const fx::Config &cfg);
  fx::Config config() const;

signals:
  void edited();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  QDoubleSpinBox *dspin(double lo, double hi, int decimals, double step);
  QSpinBox *ispin(int lo, int hi);
  void browseFile(QLineEdit *target, const QString &filter);

  fx::Config base_;
  struct Row {
    QLabel *label;
    const char *source;
  };
  QList<Row> rows_;
  QGroupBox *general_, *chaos_, *calibration_, *bandsBox_;

  QLineEdit *station_;
  // Optional: the period read (ISO dates, UTC) and the sensor read.
  QLineEdit *from_, *to_, *channels_;
  QDoubleSpinBox *fs_, *staSec_, *freqmin_, *freqmax_, *splitHz_;
  QSpinBox *winSec_, *stepSec_, *ltaSec_;
  QCheckBox *chaosOnly_;

  QSpinBox *tau_, *dim_, *evolve_, *minSamples_, *sampentM_;
  QDoubleSpinBox *meanPeriod_, *riseLo_, *riseHi_, *horizon_, *sampentR_;
  QLabel *sampentTol_;

  QLineEdit *catalog_, *stationCoords_;
  QPushButton *catalogBrowse_, *stationCoordsBrowse_;
  QCheckBox *useCoords_;
  QDoubleSpinBox *lat_, *lon_, *minMag_, *maxDist_, *preSec_, *minPost_, *coda_,
      *loud_;
  QSpinBox *maxWindows_, *seed_, *maxTau_, *maxDim_;

  BandsModel *bands_;
  QTableView *bandsView_;
  QPushButton *addBand_, *removeBand_;
};

} // namespace skg
