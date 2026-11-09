// The Analysis tab (the SCAP port): a feature table, the earthquake catalog
// and the station's position are loaded once at the top; the panes below
// answer SCAP's questions on them: which events and faults are near the
// station (Catalog, with the map), label the windows (Labels), draw the
// metric figures (Figures), compare stations (Grid).
#pragma once

#include <QWidget>

#include <memory>
#include <optional>
#include <vector>

#include "analysis/events.h"
#include "analysis/faults.h"
#include "core/module.h"
#include "fx/npy.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QTabWidget;

namespace skg {

class CatalogPane;
class LabelsPane;
class FiguresPane;
class GridPane;

// What the panes share. Loaded by the top bar; panes read it on refresh().
struct AnalysisState {
  QString tablePath;
  fx::NpyTable table;
  double winSec = 200;
  QString station; // code, from the run metadata or typed
  std::optional<analysis::Station> position;
  QString catalogPath;
  double catalogOffsetHours = 0;
  std::vector<analysis::Event> events;
  QString faultsPath;
  analysis::FaultIndex faults;
  std::vector<analysis::EventAtStation>
      eventsAtStation; // within the table's period, by SCAP's bands
  std::optional<analysis::EventAtStation> mainEvent;
  QString lang = QStringLiteral("tr");
  // The table's time span, UTC seconds; zero when no table.
  double spanStart = 0, spanEnd = 0;
  bool hasTable() const { return table.rows > 0; }
};

class AnalysisModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("analysis"); }
  QString title() const override { return tr("Analysis"); }
  QString description() const override {
    return tr("Earthquakes and faults around a station, labelled feature "
              "tables, metric figures and station "
              "comparison grids");
  }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class AnalysisWidget : public QWidget {
  Q_OBJECT
public:
  AnalysisWidget(AppContext ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void loadTable();
  void loadCatalog();
  void loadFaults();
  void resolveStation();
  void recompute();
  void say(const QString &message, bool warning = false);
  void refreshPanes();

  AppContext ctx_;
  std::shared_ptr<AnalysisState> state_;

  QLineEdit *table_, *catalog_, *stations_, *faults_, *stationCode_;
  QPushButton *tableBrowse_, *catalogBrowse_, *stationsBrowse_, *faultsBrowse_;
  QComboBox *catalogTz_;
  QCheckBox *manualPosition_;
  QDoubleSpinBox *lat_, *lon_;
  QLabel *tableLabel_, *catalogLabel_, *stationsLabel_, *faultsLabel_,
      *stationLabel_, *status_;
  QTabWidget *tabs_;
  CatalogPane *catalogPane_;
  LabelsPane *labelsPane_;
  FiguresPane *figuresPane_;
  GridPane *gridPane_;
};

} // namespace skg
