// Small helpers the modules share so that every page reads the same way:
// the one main action of a page, compact "…" browse buttons, a hint inside
// an empty table or list, and a pass that tidies a finished page.
#pragma once

#include <QLabel>
#include <QPointer>
#include <QString>

class QAbstractButton;
class QAbstractItemView;
class QWidget;

namespace skg::ui {

// The page's main action (Run, Start): the only filled button.
void setPrimary(QAbstractButton *button, bool on = true);
// A narrow button for "…" next to a path field.
void setCompact(QAbstractButton *button, bool on = true);

// A centred hint shown inside a view while its model has no rows, e.g.
// "No campaign open. Choose a data folder…". Set with setEmptyHint(), which
// can be called again from retranslate(). The view must already have its
// model.
class EmptyHint : public QLabel {
  Q_OBJECT
public:
  EmptyHint(QAbstractItemView *view, const QString &text);
  void setHint(const QString &text);

protected:
  bool eventFilter(QObject *o, QEvent *e) override;

private:
  void refresh();
  QPointer<QAbstractItemView> view_;
};
void setEmptyHint(QAbstractItemView *view, const QString &text);

// Applied once to each page by the main window: "..." buttons become
// compact, spin boxes get a text margin, tables lose their grid and get
// alternating rows, and a scrolling form column is given the width its
// contents need.
void polishPage(QWidget *page);

} // namespace skg::ui
