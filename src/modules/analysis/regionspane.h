// Regions: split the loaded catalog into seismic regions by epicentre (a
// linear SOM, and a SOM grouped by Mean Shift), as a job that writes the
// maps and one catalog per region; the summary table and the maps are
// shown here. Port of earthquake-clustering-eafz.
#pragma once

#include <QWidget>

#include <memory>
#include <utility>
#include <vector>

#include "analysis/regionrun.h"
#include "core/module.h"
#include "modules/analysis/analysismodule.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace skg {

class FigureView;
class Job;

class RegionsPane : public QWidget {
  Q_OBJECT
public:
  RegionsPane(AppContext ctx, std::shared_ptr<AnalysisState> state,
              QWidget *parent = nullptr);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  // What the job fills; read here once the job has finished.
  struct Run {
    analysis::RegionRunOptions options;
    analysis::RegionRunResult result;
    bool done = false;
  };

  void retranslate();
  void run();
  void finished(bool ok);
  bool options(analysis::RegionRunOptions *o, QString *error);
  void showResult();
  void showMap();
  void updateControls();
  void say(const QString &message, bool warning = false);
  void save();
  QLabel *label(const char *source);

  AppContext ctx_;
  std::shared_ptr<AnalysisState> state_;
  std::shared_ptr<Run> run_;
  Job *running_ = nullptr;
  // Layers read from files, kept while their path stays the same.
  QString provincesPath_, citiesPath_;
  std::shared_ptr<const analysis::ShapeLayer> provinces_;
  std::vector<analysis::City> cities_;

  std::vector<std::pair<QLabel *, const char *>> labels_;
  QGroupBox *methodBox_, *mapBox_;
  QComboBox *methods_, *mapChoice_;
  QSpinBox *regions_, *gridRows_, *gridCols_, *iterations_, *seed_, *dpi_;
  QDoubleSpinBox *quantile_, *markerLat_, *markerLon_;
  QLineEdit *area_, *markerLabel_, *provincesEdit_, *citiesEdit_, *outDir_;
  QCheckBox *marker_, *showCountries_, *showFaults_, *showCities_, *maps_,
      *svg_;
  QPushButton *provincesBrowse_, *citiesBrowse_, *outBrowse_, *start_, *stop_,
      *open_;
  QLabel *status_, *catalog_;
  QTableWidget *table_;
  QPlainTextEdit *log_;
  FigureView *view_;
};

} // namespace skg
