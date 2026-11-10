// The Cases page: the study's inputs (the catalog and the waveform folders),
// the list of cases, and the selected case with its map, its stations and
// what the waveform folders hold of each, its catalog zone and its span.
#pragma once

#include <QWidget>

#include <functional>
#include <vector>

#include "analysis/events.h"
#include "core/module.h"
#include "study/study.h"

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace skg {

class CaseMap;
class WaveScan;

class CasesPage : public QWidget {
  Q_OBJECT
public:
  CasesPage(AppContext &ctx, QWidget *parent = nullptr);

  // Asks the user a yes/no question before a change that costs something
  // (finished runs going stale). Tests replace it; the default is a message
  // box.
  static std::function<bool(QWidget *parent, const QString &title,
                            const QString &text)>
      confirm;

  // How many finished runs of the case would no longer be current after the
  // change from `before` to `after`.
  static int runsMadeStale(const study::Study &before,
                           const study::Study &after, const QString &caseId);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void refresh();  // the study changed
  void showCase(); // the selection changed
  void saveCase(); // the detail fields changed
  void addFromCatalog();
  void addNear(const QStringList &stations); // the same, filtered to these
  void getCatalog();
  void updateBuiltIn();
  void openSetup();
  void addReference();
  void removeCase();
  void chooseCatalog();
  void offsetChosen(int index);
  void openWaveforms();
  void chooseStationFolder();
  void clearStationFolder();
  void getWaveforms();
  void updateWaveSummary();
  void rescanIfChanged();
  void updateStationButtons();
  // Applies the edit; on failure shows the banner and puts the page back to
  // what is saved.
  bool apply(const std::function<void(study::Study &)> &edit);
  void showBanner(const QString &text);
  QString currentId() const;
  QString selectedStation() const;
  const std::vector<analysis::Station> &stationList();

  AppContext ctx_;
  WaveScan *scan_;
  bool updating_ = false;
  QString stationsFrom_;
  std::vector<analysis::Station> stations_;
  QList<study::StationRef> candidates_;

  QStackedWidget *stack_;
  QLabel *noStudy_;
  QPushButton *newStudy_, *openStudy_;
  QLabel *banner_;
  QGroupBox *inputs_;
  QLabel *catalogLabel_, *dataLabel_, *waveSummary_;
  QLineEdit *catalog_;
  QPushButton *catalogBrowse_, *waveButton_;
  QPushButton *catalogGet_, *catalogUpdate_, *setup_;
  QComboBox *catalogTime_;
  QPushButton *add_, *addRef_, *remove_, *coverageButton_;
  QLabel *summary_;
  QTableWidget *table_;
  QWidget *detail_;
  QLineEdit *name_;
  QLabel *facts_;
  CaseMap *map_;
  QLabel *stationsTitle_, *stationHint_;
  QTableWidget *stationTable_;
  QPushButton *folder_, *folderClear_, *getWave_, *next_;
  QLabel *zoneLabel_, *spanLabel_;
  QComboBox *zone_;
  QDoubleSpinBox *radius_;
  QSpinBox *span_;
};

} // namespace skg
