// Small helpers the modules share so that every page reads the same way:
// the one main action of a page, compact "…" browse buttons, a hint inside
// an empty table or list, and a pass that tidies a finished page.
#pragma once

#include <QLabel>
#include <QPointer>
#include <QString>

#include <initializer_list>

class QAbstractButton;
class QAbstractItemView;
class QSettings;
class QSplitter;
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

// Hover help for an option: `text` (plain, one or two sentences) becomes the
// tooltip and the What's This text of the field and of its label, so
// hovering either explains the option. The label is the field's row label in
// a QFormLayout (also when the field sits in a nested row layout) or a
// QLabel whose buddy is the field; it is looked up at the time of the call,
// so call this after the field is in its layout, i.e. from retranslate().
// Long text is wrapped. The plain text is kept in the "help" property.
void setHelp(QWidget *field, const QString &text);
// The same for several widgets that make up one option (a path field and its
// "…" button, a value and its unit).
void setHelp(std::initializer_list<QWidget *> fields, const QString &text);
// A label for a field that sits in a plain row (not a form): the field is
// its buddy, so setHelp() on the field reaches the label too.
QLabel *buddyLabel(const QString &text, QWidget *field, QWidget *parent);
// The label setHelp() would use for this field, or nullptr.
QLabel *helpLabel(const QWidget *field);
// The tooltip form of a help text (wrapped rich text), for item views and
// other places that take a string.
QString helpText(const QString &text);

// Keeps a splitter where the user left it, between sessions, under `key`
// (e.g. "features/split"). Call after the splitter has its widgets.
void persistSplitter(QSplitter *splitter, QSettings *settings,
                     const QString &key);

// Applied once to each page by the main window: "..." buttons become
// compact, spin boxes get a text margin, tables lose their grid and get
// alternating rows, and a scrolling form column is given the width its
// contents need.
void polishPage(QWidget *page);

} // namespace skg::ui
