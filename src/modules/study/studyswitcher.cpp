#include "modules/study/studyswitcher.h"

#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include "study/store.h"

namespace skg {

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
  bool ok = false;
  const QString name =
      QInputDialog::getText(parent, tr("New study"), tr("Name"),
                            QLineEdit::Normal, QFileInfo(dir).fileName(), &ok);
  if (!ok)
    return;
  QString error;
  if (!store->create(dir, name.trimmed(), &error))
    QMessageBox::warning(parent, tr("New study"), error);
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
