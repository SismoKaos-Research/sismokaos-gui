// The study selector in the page header: the open study's name, and a menu
// to open a recent study, open a folder, start a new study or close.
#pragma once

#include <QToolButton>

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

protected:
  void changeEvent(QEvent *e) override;

private:
  void refresh();
  void fillMenu();

  study::StudyStore *store_;
};

} // namespace skg
