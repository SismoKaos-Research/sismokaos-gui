#include "core/ui.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QEvent>
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
