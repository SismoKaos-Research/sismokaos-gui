// The Cases page: the study's inputs (the catalog and the waveform folders),
// the list of cases, and the selected case with its map, its stations and
// what the waveform folders hold of each, its catalog zone and its span.
#pragma once

#include <QSet>
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
class QToolButton;
class QMenu;

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

  // What the buttons and menus do, also for tests. Each asks through
  // `confirm` before a change that costs finished runs.
  // The ids of the selected cases, in the order of the table.
  QStringList selectedIds() const;
  // Adds the cases; one whose id names an existing case replaces that
  // case's name, span and stations and keeps its folder.
  bool addCases(const QList<study::Case> &cases);
  // Removes the selected cases. They can be put back with restoreRemoved()
  // until the study is closed; their result folders are not touched, so
  // their runs come back with them.
  void removeCase();
  QStringList removedCases() const; // names, the last removed first
  bool restoreRemoved(int index = 0);
  bool setSpanOf(const QStringList &ids, int days);
  // For each case: the nearest `n` stations within `km` that have waveforms
  // in its span (stationsToStartWith).
  bool applyStationRule(const QStringList &ids, int n, double km);
  QString duplicateCase(const QString &id); // the new case's id
  // Sends the Data page the stations and the period of these cases.
  void getWaveformsFor(const QStringList &ids);
  // Files and folders dropped on the page: a catalog (asked first, with
  // what it holds), waveform folders, or a StationXML file.
  void handleDrop(const QStringList &paths);

protected:
  void changeEvent(QEvent *e) override;
  bool eventFilter(QObject *o, QEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

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
  void chooseCatalog();
  void applyFilter();
  void fillMenu(QMenu *menu);
  void saveTableState();
  void fitColumns();
  void tickNearest();
  void untickAll();
  int staleAfter(const std::function<void(study::Study &)> &edit,
                 const QStringList &ids) const;
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
  QSet<QString> resumed_; // studies whose cut-short scan was taken up again
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
  QLabel *catalogFacts_; // how many events, how far down the magnitudes go
  QPushButton *catalogBrowse_, *waveButton_;
  QPushButton *catalogGet_, *catalogUpdate_, *setup_;
  QComboBox *catalogTime_;
  QPushButton *add_, *addRef_, *remove_, *coverageButton_;
  QPushButton *more_;
  QLineEdit *filter_;
  QPushButton *scanJob_, *tickNearest_, *untickAll_;
  // Removed in this session, the last first; cleared with the study.
  struct Removed {
    study::Case c;
    int position = 0;
  };
  QList<Removed> removed_;
  QString removedFrom_; // the study they belong to
  QString tableStateOf_; // the study whose column widths are applied
  int stationSort_ = 0;  // 0 ticked first then nearest, 2 distance, 4 coverage
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
