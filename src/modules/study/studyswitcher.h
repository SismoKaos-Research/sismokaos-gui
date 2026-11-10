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
  // Whether a new study goes on to the "Set up the study" dialog (tests
  // switch it off), and how the page of the given id is shown afterwards
  // (set by the application; unset in tests).
  static bool setupAfterCreate;
  static std::function<void(const QString &)> showPage;
  static void openStudy(QWidget *parent, study::StudyStore *store);
  // Opens the study in `dir` (a recent one, a dropped folder), saying why
  // when it cannot be. A study that is open elsewhere is refused with who
  // has it; one held from another computer can be opened anyway, after a
  // question (`breakLockAnswer` answers it in tests: 1 yes, 0 no; -1 asks).
  static bool openPath(QWidget *parent, study::StudyStore *store,
                       const QString &dir, int breakLockAnswer = -1);
  // The "Set up the study" dialog for the open study (File > Study inputs…).
  static void studyInputs(QWidget *parent, study::StudyStore *store);
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
