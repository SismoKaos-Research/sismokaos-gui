// The study selector in the page header: the open study's name, and a menu
// to open a recent study, open a folder, start a new study or close.
#pragma once

#include <QToolButton>

#include <functional>

#include "study/study.h"

namespace skg {

namespace study {
class StudyStore;
}

class StudySwitcher : public QToolButton {
  Q_OBJECT
public:
  explicit StudySwitcher(study::StudyStore *store, QWidget *parent = nullptr);

  // The dialogs, also used by the pages' "no study open" state.
  static void newStudy(QWidget *parent, study::StudyStore *store);
  static void openStudy(QWidget *parent, study::StudyStore *store);
  // Asks where each input of the open study that is not on disk has gone
  // (a moved catalog, a data disk mounted elsewhere) and rewrites the paths.
  // One answer fixes everything that moved with it. `ask` returns the new
  // place of the input, or nothing to skip it; the default asks the user.
  using Locator = std::function<QString(const study::MissingInput &)>;
  static int locateMissing(QWidget *parent, study::StudyStore *store,
                           Locator ask = {});

protected:
  void changeEvent(QEvent *e) override;

private:
  void refresh();
  void fillMenu();

  study::StudyStore *store_;
  QString checkedDir_; // the study whose inputs were last checked
};

} // namespace skg
