// The Cases page: the study's inputs, the list of cases, and the selected
// case with its map, its stations, its catalog zone and its span.
#pragma once

#include <QWidget>

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

class CasesPage : public QWidget {
  Q_OBJECT
public:
  CasesPage(AppContext &ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void refresh();  // the study changed
  void showCase(); // the selection changed
  void saveCase(); // the detail fields changed
  void addFromCatalog();
  void addReference();
  void removeCase();
  void chooseCatalog();
  void chooseDataRoot();
  QString currentId() const;
  const std::vector<analysis::Station> &stationList();

  AppContext ctx_;
  bool updating_ = false;
  QString stationsFrom_;
  std::vector<analysis::Station> stations_;
  QList<study::StationRef> candidates_;

  QStackedWidget *stack_;
  QLabel *noStudy_;
  QPushButton *newStudy_, *openStudy_;
  QGroupBox *inputs_;
  QLabel *catalogLabel_, *dataLabel_;
  QLineEdit *catalog_, *dataRoot_;
  QPushButton *catalogBrowse_, *dataBrowse_;
  QComboBox *catalogTime_;
  QPushButton *add_, *addRef_, *remove_;
  QLabel *summary_;
  QTableWidget *table_;
  QWidget *detail_;
  QLineEdit *name_;
  QLabel *facts_;
  CaseMap *map_;
  QLabel *stationsTitle_;
  QTableWidget *stationTable_;
  QLabel *zoneLabel_, *spanLabel_;
  QComboBox *zone_;
  QDoubleSpinBox *radius_;
  QSpinBox *span_;
};

} // namespace skg
