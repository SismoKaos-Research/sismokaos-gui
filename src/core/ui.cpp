#include "core/ui.h"

#include "core/appsettings.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QSplitter>
#include <QSettings>

#include <clocale>
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLayout>
#include <QLineEdit>
#include <QMargins>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QTableView>
#include <QWidget>

namespace skg::ui {

namespace {

void setFlag(QWidget *w, const char *name, bool on) {
  if (w->property(name).toBool() == on)
    return;
  w->setProperty(name, on);
  w->style()->unpolish(w);
  w->style()->polish(w);
}

} // namespace

namespace {

bool layoutHolds(QLayout *layout, const QWidget *w) {
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem *it = layout->itemAt(i);
    if (it->widget() == w)
      return true;
    if (it->layout() && layoutHolds(it->layout(), w))
      return true;
  }
  return false;
}

// The row label of `w` in a form layout at or below `layout`.
QLabel *formLabel(QLayout *layout, const QWidget *w) {
  if (auto *form = qobject_cast<QFormLayout *>(layout)) {
    for (int row = 0; row < form->rowCount(); ++row) {
      QLayoutItem *field = form->itemAt(row, QFormLayout::FieldRole);
      if (!field)
        continue;
      const bool here = field->widget() == w ||
                        (field->layout() && layoutHolds(field->layout(), w));
      if (!here)
        continue;
      QLayoutItem *label = form->itemAt(row, QFormLayout::LabelRole);
      return label ? qobject_cast<QLabel *>(label->widget()) : nullptr;
    }
  }
  for (int i = 0; i < layout->count(); ++i)
    if (QLayout *child = layout->itemAt(i)->layout())
      if (QLabel *l = formLabel(child, w))
        return l;
  return nullptr;
}

} // namespace

QLabel *helpLabel(const QWidget *field) {
  const QWidget *parent = field->parentWidget();
  if (!parent)
    return nullptr;
  // The field itself may be a form row's field, or sit in a plain holder
  // widget (a path field with its "…" button) that is.
  const QWidget *w = field;
  for (int up = 0; up < 3 && w->parentWidget(); ++up, w = w->parentWidget()) {
    const QWidget *p = w->parentWidget();
    if (p->layout())
      if (QLabel *l = formLabel(p->layout(), w))
        return l;
  }
  const auto labels =
      parent->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
  for (QLabel *l : labels)
    if (l->buddy() == field)
      return l;
  return nullptr;
}

QLabel *buddyLabel(const QString &text, QWidget *field, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setBuddy(field);
  return label;
}

QString helpText(const QString &text) {
  if (text.isEmpty())
    return text;
  // A short text stays on one line. A longer one is rich text in a column
  // of a fixed, readable width: plain text would run in one long line, and
  // rich text left to itself wraps into a narrow strip.
  const QString escaped = text.toHtmlEscaped();
  if (text.size() <= 48)
    return QStringLiteral("<qt><nobr>%1</nobr></qt>").arg(escaped);
  return QStringLiteral("<qt><table width=\"340\" cellspacing=\"0\" "
                        "cellpadding=\"0\"><tr><td>%1</td></tr></table></qt>")
      .arg(escaped);
}

void setHelp(QWidget *field, const QString &text) {
  if (!field)
    return;
  const QString tip = helpText(text);
  field->setToolTip(tip);
  field->setWhatsThis(tip);
  field->setProperty("help", text);
  if (QLabel *label = helpLabel(field)) {
    label->setToolTip(tip);
    label->setWhatsThis(tip);
  }
}

void setHelp(std::initializer_list<QWidget *> fields, const QString &text) {
  for (QWidget *f : fields)
    setHelp(f, text);
}

void setPrimary(QAbstractButton *button, bool on) {
  setFlag(button, "primary", on);
}

void setCompact(QAbstractButton *button, bool on) {
  setFlag(button, "compact", on);
}

EmptyHint::EmptyHint(QAbstractItemView *view, const QString &text)
    : QLabel(text, view->viewport()), view_(view) {
  setObjectName(QStringLiteral("emptyState"));
  setAlignment(Qt::AlignCenter);
  setWordWrap(true);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  view->viewport()->installEventFilter(this);
  if (QAbstractItemModel *m = view->model()) {
    connect(m, &QAbstractItemModel::rowsInserted, this, &EmptyHint::refresh);
    connect(m, &QAbstractItemModel::rowsRemoved, this, &EmptyHint::refresh);
    connect(m, &QAbstractItemModel::modelReset, this, &EmptyHint::refresh);
    connect(m, &QAbstractItemModel::layoutChanged, this, &EmptyHint::refresh);
  }
  refresh();
}

bool EmptyHint::eventFilter(QObject *o, QEvent *e) {
  if (e->type() == QEvent::Resize || e->type() == QEvent::Show)
    refresh();
  return QLabel::eventFilter(o, e);
}

void EmptyHint::refresh() {
  if (!view_)
    return;
  const QAbstractItemModel *m = view_->model();
  const bool empty = !m || m->rowCount(view_->rootIndex()) == 0;
  setGeometry(view_->viewport()->rect().adjusted(24, 0, -24, 0));
  setVisible(empty && !text().isEmpty());
}

void EmptyHint::setHint(const QString &text) {
  setText(text);
  refresh();
}

void setEmptyHint(QAbstractItemView *view, const QString &text) {
  auto *hint = view->viewport()->findChild<EmptyHint *>(
      QString(), Qt::FindDirectChildrenOnly);
  if (hint)
    hint->setHint(text);
  else
    new EmptyHint(view, text);
}

void persistSplitter(QSplitter *splitter, QSettings *settings,
                     const QString &key) {
  if (!splitter || !settings)
    return;
  const QByteArray state = settings->value(key).toByteArray();
  if (!state.isEmpty())
    splitter->restoreState(state);
  QObject::connect(splitter, &QSplitter::splitterMoved, splitter,
                   [splitter, settings, key] {
                     settings->setValue(key, splitter->saveState());
                   });
}

QString dialogFolder(QSettings *settings, const QString &key,
                     const QString &fallback) {
  const QString saved =
      settings ? settings
                     ->value(QLatin1String(appsettings::kDialogFolderPrefix) +
                             key)
                     .toString()
               : QString();
  if (!saved.isEmpty() && QFileInfo(saved).isDir())
    return saved;
  if (!fallback.isEmpty() && QFileInfo(fallback).isDir())
    return fallback;
  return QDir::homePath();
}

void rememberDialogFolder(QSettings *settings, const QString &key,
                          const QString &chosenPath) {
  if (!settings || chosenPath.isEmpty())
    return;
  // A chosen folder is remembered by its parent, so the dialog next opens
  // beside it and not inside it; a chosen file by the folder it is in.
  const QFileInfo fi(chosenPath);
  settings->setValue(QLatin1String(appsettings::kDialogFolderPrefix) + key,
                     fi.absolutePath());
}

QString pickFolder(QWidget *parent, QSettings *settings, const QString &key,
                   const QString &title, const QString &fallback) {
  const QString dir = QFileDialog::getExistingDirectory(
      parent, title, dialogFolder(settings, key, fallback));
  rememberDialogFolder(settings, key, dir);
  return dir;
}

QString pickFile(QWidget *parent, QSettings *settings, const QString &key,
                 const QString &title, const QString &filter,
                 const QString &fallback) {
  const QString file = QFileDialog::getOpenFileName(
      parent, title, dialogFolder(settings, key, fallback), filter);
  rememberDialogFolder(settings, key, file);
  return file;
}

QString pickSaveFile(QWidget *parent, QSettings *settings, const QString &key,
                     const QString &title, const QString &suggestedName,
                     const QString &filter, const QString &fallback) {
  const QString file = QFileDialog::getSaveFileName(
      parent, title,
      QDir(dialogFolder(settings, key, fallback)).filePath(suggestedName),
      filter);
  rememberDialogFolder(settings, key, file);
  return file;
}

void pinNumberFormat() {
  QLocale::setDefault(QLocale::c());
  std::setlocale(LC_NUMERIC, "C");
}

void polishPage(QWidget *page) {
  for (QPushButton *b : page->findChildren<QPushButton *>()) {
    const QString t = b->text();
    if (t == QLatin1String("...") || t == QStringLiteral("…"))
      setCompact(b);
  }
  for (QAbstractSpinBox *sb : page->findChildren<QAbstractSpinBox *>())
    if (auto *le = sb->findChild<QLineEdit *>())
      le->setTextMargins(6, 0, 0, 0);
  for (QScrollArea *a : page->findChildren<QScrollArea *>()) {
    if (!a->widget() || !a->widgetResizable())
      continue;
    a->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QLayout *l = a->widget()->layout()) {
      QMargins m = l->contentsMargins();
      m.setRight(m.right() + 12);
      l->setContentsMargins(m);
    }
    a->setMinimumWidth(a->widget()->minimumSizeHint().width() + 16);
  }
  for (QTableView *v : page->findChildren<QTableView *>()) {
    v->setShowGrid(false);
    v->setAlternatingRowColors(true);
  }
}

} // namespace skg::ui
