#include "modules/study/studyswitcher.h"

#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>

#include "modules/study/studysetup.h"
#include "study/store.h"

namespace skg {

bool StudySwitcher::setupAfterCreate = true;
std::function<void(const QString &)> StudySwitcher::showPage;

StudySwitcher::StudySwitcher(study::StudyStore *store, QWidget *parent)
    : QToolButton(parent), store_(store) {
  setObjectName(QStringLiteral("studySwitch"));
  setPopupMode(QToolButton::InstantPopup);
  setCursor(Qt::PointingHandCursor);
  auto *menu = new QMenu(this);
  setMenu(menu);
  connect(menu, &QMenu::aboutToShow, this, &StudySwitcher::fillMenu);
  connect(store_, &study::StudyStore::changed, this, &StudySwitcher::refresh);
  refresh();
}

void StudySwitcher::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    refresh();
  QToolButton::changeEvent(e);
}

void StudySwitcher::refresh() {
  setText(store_->isOpen() ? tr("Study: %1  ▾").arg(store_->study().name)
                           : tr("No study open  ▾"));
  setToolTip(store_->isOpen() ? store_->study().dir : QString());
  // A study that has just been opened: are its inputs where it says?
  const QString dir = store_->isOpen() ? store_->study().dir : QString();
  if (dir != checkedDir_) {
    checkedDir_ = dir;
    if (!dir.isEmpty() && !store_->study().missingInputs().isEmpty())
      QTimer::singleShot(0, this, [this, dir] {
        if (store_->isOpen() && store_->study().dir == dir)
          locateMissing(this, store_);
      });
  }
}

namespace {

QString kindName(study::MissingInput::Kind k) {
  using K = study::MissingInput::Kind;
  switch (k) {
  case K::Catalog:
    return StudySwitcher::tr("The earthquake catalog");
  case K::StationList:
    return StudySwitcher::tr("The station list");
  case K::WaveformRoot:
    return StudySwitcher::tr("A waveform folder");
  case K::StationData:
    return StudySwitcher::tr("A station's waveform folder");
  case K::FaultFile:
    return StudySwitcher::tr("A fault file");
  }
  return {};
}

bool isFolder(study::MissingInput::Kind k) {
  return k == study::MissingInput::Kind::WaveformRoot ||
         k == study::MissingInput::Kind::StationData;
}

} // namespace

int StudySwitcher::locateMissing(QWidget *parent, study::StudyStore *store,
                                 Locator ask) {
  if (!ask)
    ask = [parent, store](const study::MissingInput &m) -> QString {
      QMessageBox box(parent);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(tr("Study inputs"));
      box.setText(tr("%1 of this study is not where it was:\n%2")
                      .arg(kindName(m.kind), m.path));
      box.setInformativeText(
          tr("If it moved (another disk, another folder), show where it is "
             "now. Everything else that moved with it is found from that."));
      QPushButton *locate =
          box.addButton(tr("Locate…"), QMessageBox::AcceptRole);
      box.addButton(tr("Skip"), QMessageBox::RejectRole);
      box.exec();
      if (box.clickedButton() != locate)
        return {};
      const QString start = store->study().dir;
      return isFolder(m.kind)
                 ? QFileDialog::getExistingDirectory(
                       parent, tr("Where is %1 now?").arg(m.path), start)
                 : QFileDialog::getOpenFileName(
                       parent, tr("Where is %1 now?").arg(m.path), start);
    };
  int fixed = 0;
  QStringList skipped;
  for (;;) {
    if (!store->isOpen())
      break;
    study::MissingInput next{};
    bool any = false;
    for (const study::MissingInput &m : store->study().missingInputs())
      if (!skipped.contains(m.path)) {
        next = m;
        any = true;
        break;
      }
    if (!any)
      break;
    skipped << next.path; // asked once, whatever the answer
    const QString now = ask(next);
    if (now.isEmpty())
      continue;
    int changed = 0;
    QString error;
    if (!store->update(
            [&](study::Study &s) { changed = s.relocate(next.path, now); },
            &error)) {
      QMessageBox::warning(parent, tr("Study inputs"), error);
      break;
    }
    fixed += changed;
  }
  return fixed;
}

void StudySwitcher::fillMenu() {
  QMenu *m = menu();
  m->clear();
  for (const QString &dir : store_->recent()) {
    QAction *a = m->addAction(QFileInfo(dir).fileName());
    a->setToolTip(dir);
    a->setCheckable(true);
    a->setChecked(store_->isOpen() && store_->study().dir == dir);
    connect(a, &QAction::triggered, this, [this, dir] {
      QString error;
      if (!store_->open(dir, &error))
        QMessageBox::warning(this, tr("Study"), error);
    });
  }
  if (!m->isEmpty())
    m->addSeparator();
  m->addAction(tr("New study…"), this, [this] { newStudy(this, store_); });
  m->addAction(tr("Open study…"), this, [this] { openStudy(this, store_); });
  QAction *close =
      m->addAction(tr("Close study"), store_, &study::StudyStore::close);
  close->setEnabled(store_->isOpen());
}

void StudySwitcher::newStudy(QWidget *parent, study::StudyStore *store) {
  const QString dir = QFileDialog::getExistingDirectory(
      parent, tr("Folder for the new study (its results are written here)"));
  if (dir.isEmpty())
    return;
  // A folder that already is a study: that is the one they mean.
  if (QFileInfo::exists(QDir(dir).filePath(study::Study::kFileName))) {
    if (QMessageBox::question(
            parent, tr("New study"),
            tr("%1 already holds a study. Open it instead?").arg(dir)) ==
        QMessageBox::Yes) {
      QString error;
      if (!store->open(dir, &error))
        QMessageBox::warning(parent, tr("Open study"), error);
    }
    return;
  }
  // Results are written next to whatever is there; say so before it happens.
  if (!QDir(dir).isEmpty() &&
      QMessageBox::question(
          parent, tr("New study"),
          tr("%1 is not empty. A study writes one folder per earthquake "
             "there, beside what the folder holds now. Use it anyway?")
              .arg(dir)) != QMessageBox::Yes)
    return;
  bool ok = false;
  const QString name =
      QInputDialog::getText(parent, tr("New study"), tr("Name"),
                            QLineEdit::Normal, QFileInfo(dir).fileName(), &ok);
  if (!ok)
    return;
  QString error;
  if (!store->create(dir, name.trimmed(), &error)) {
    QMessageBox::warning(parent, tr("New study"), error);
    return;
  }
  // The catalog and the waveform folders in one go; both can be skipped.
  if (setupAfterCreate) {
    StudySetupDialog setup(store, parent);
    setup.exec();
  }
  if (showPage)
    showPage(QStringLiteral("cases"));
}

void StudySwitcher::openStudy(QWidget *parent, study::StudyStore *store) {
  const QString dir = QFileDialog::getExistingDirectory(
      parent, tr("Open a study folder (the one holding study.toml)"));
  if (dir.isEmpty())
    return;
  QString error;
  if (!store->open(dir, &error))
    QMessageBox::warning(parent, tr("Open study"), error);
}

} // namespace skg
