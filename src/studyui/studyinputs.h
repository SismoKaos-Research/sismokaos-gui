// What the tool pages share with the open study.
//
// A tool page (Features, Indicators, Classification, Analysis) keeps its own
// inputs so it can be used alone. When a study is open, the bar at its top
// offers to take them from the study instead: the catalog and its time
// offset, and, for a case and one of its stations chosen in the bar, the
// place, the station and its waveform folder. The page fills its fields from
// the bar whenever inputsChanged() is emitted (the study changed, another
// case was chosen, the box was ticked), so nothing is typed twice and a page
// never keeps pointing at the study that was open before.
//
// The bar also holds "Save as study recipe…": the page's current settings
// become a recipe of the study, to be run on every case from the Runs page.
#pragma once

#include <QFrame>
#include <QString>
#include <QVariantMap>

#include "study/study.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSettings;

namespace skg {

namespace study {
class StudyStore;
}

class StudyInputsBar : public QFrame {
  Q_OBJECT
public:
  enum class Needs { Case, CaseAndStation };

  // `store` may be null (the bar then stays hidden). `settingsKey` keeps the
  // tick per page, e.g. "features/useStudy". `recipeTool` is the tool the
  // page's settings make a recipe of, or empty for a page with no recipe.
  StudyInputsBar(study::StudyStore *store, QSettings *settings,
                 const QString &settingsKey, Needs needs,
                 const QString &recipeTool, QWidget *parent = nullptr);

  // A study is open and the user wants its inputs on this page.
  bool active() const;
  // Valid while active(); null otherwise or when the study has no cases (or
  // the case no stations).
  const study::Study *study() const;
  const study::Case *currentCase() const;
  const study::StationRef *currentStation() const;
  // The feature table a finished features run of the study wrote for the
  // chosen case and station (the first features recipe that has one), or
  // nothing.
  QString featureTable() const;

  // Adds a recipe of `tool` to the open study: the tool's defaults with
  // `settings` laid over them. Returns its id, or nothing with `error` set.
  static QString addRecipe(study::StudyStore *store, const QString &tool,
                           const QString &name, const QVariantMap &settings,
                           QString *error = nullptr);
  // Asks for the name, adds the recipe and says where to run it.
  void saveRecipe(const QVariantMap &settings);

signals:
  // The page should fill its fields again (or stop being held to the study).
  void inputsChanged();
  // "Save as study recipe…" was pressed: the page answers with saveRecipe().
  void recipeRequested();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void refill();

  study::StudyStore *store_;
  QSettings *settings_;
  QString key_;
  Needs needs_;
  QString tool_;
  QCheckBox *use_;
  QLabel *caseLabel_;
  QComboBox *case_;
  QLabel *stationLabel_;
  QComboBox *station_;
  QPushButton *recipe_;
};

} // namespace skg
