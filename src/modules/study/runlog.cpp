#include "modules/study/runlog.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "core/ui.h"

namespace skg {

namespace {
// The end of a very long log is where a failure is: no more than this is
// shown from a file when it is first opened.
constexpr qint64 kMost = 2 * 1024 * 1024;
} // namespace

RunLogDialog::RunLogDialog(const QString &path, QWidget *parent)
    : QDialog(parent), path_(path) {
  setObjectName(QStringLiteral("runLog"));
  setWindowTitle(tr("Run log"));
  where_ = new QLabel(QDir::toNativeSeparators(path), this);
  where_->setObjectName(QStringLiteral("fieldLabel"));
  where_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  where_->setWordWrap(true);

  view_ = new QPlainTextEdit(this);
  view_->setObjectName(QStringLiteral("runLogText"));
  view_->setReadOnly(true);
  view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  view_->setLineWrapMode(QPlainTextEdit::NoWrap);
  view_->setPlaceholderText(tr("The run has not written anything yet."));

  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("runLogSearch"));
  search_->setPlaceholderText(tr("find in the log…"));
  search_->setClearButtonEnabled(true);
  ui::setHelp(search_, tr("Type a word (error, warning, a file name) and "
                          "press Enter: the next place it occurs in is "
                          "selected. Enter again goes on to the one after."));
  auto *next = new QPushButton(tr("Find next"), this);
  next->setToolTip(ui::helpText(
      tr("Selects the next place the text on the left occurs in, starting "
         "again at the top after the last one.")));
  follow_ = new QCheckBox(tr("Follow"), this);
  follow_->setObjectName(QStringLiteral("runLogFollow"));
  follow_->setChecked(true);
  ui::setHelp(follow_,
              tr("While ticked, the view stays at the end as the run writes "
                 "new lines. Scrolling up to read unticks it; tick it again "
                 "to go back to the end."));
  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("fieldLabel"));

  auto *refreshButton = new QPushButton(tr("Refresh"), this);
  refreshButton->setToolTip(ui::helpText(
      tr("Reads the file again now. It is also read by itself every second "
         "while this window is open.")));
  auto *copy = new QPushButton(tr("Copy"), this);
  copy->setObjectName(QStringLiteral("runLogCopy"));
  copy->setToolTip(ui::helpText(
      tr("Copies the selected lines, or the whole log when nothing is "
         "selected, to the clipboard (for a problem report, say).")));
  auto *open = new QPushButton(tr("Open in editor"), this);
  open->setToolTip(ui::helpText(
      tr("Opens run.log in the program this computer uses for text files.")));
  auto *close = new QPushButton(tr("Close"), this);

  auto *tools = new QHBoxLayout;
  tools->addWidget(search_, 1);
  tools->addWidget(next);
  tools->addSpacing(12);
  tools->addWidget(follow_);
  auto *bottom = new QHBoxLayout;
  bottom->addWidget(status_, 1);
  bottom->addWidget(refreshButton);
  bottom->addWidget(copy);
  bottom->addWidget(open);
  bottom->addWidget(close);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(where_);
  layout->addLayout(tools);
  layout->addWidget(view_, 1);
  layout->addLayout(bottom);
  resize(900, 600);

  connect(close, &QPushButton::clicked, this, &QDialog::close);
  connect(refreshButton, &QPushButton::clicked, this, &RunLogDialog::refresh);
  connect(copy, &QPushButton::clicked, this, [this] {
    const QString selected = view_->textCursor().selectedText();
    // A selection uses U+2029 between its lines.
    QGuiApplication::clipboard()->setText(
        selected.isEmpty()
            ? view_->toPlainText()
            : QString(selected).replace(QChar(0x2029), QLatin1Char('\n')));
  });
  connect(open, &QPushButton::clicked, this,
          [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(path_)); });
  const auto find = [this] {
    if (!findNext(search_->text()) && !search_->text().isEmpty())
      status_->setText(tr("\"%1\" is not in the log.").arg(search_->text()));
  };
  connect(next, &QPushButton::clicked, this, find);
  connect(search_, &QLineEdit::returnPressed, this, find);
  // Reading further up stops the following; the end takes it up again.
  connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this](int value) {
            const QScrollBar *bar = view_->verticalScrollBar();
            const bool atEnd = value >= bar->maximum();
            if (follow_->isChecked() != atEnd && !bar->signalsBlocked())
              follow_->setChecked(atEnd);
          });
  connect(follow_, &QCheckBox::toggled, this, [this](bool on) {
    if (on)
      view_->verticalScrollBar()->setValue(
          view_->verticalScrollBar()->maximum());
  });
  timer_ = new QTimer(this);
  timer_->setInterval(1000);
  connect(timer_, &QTimer::timeout, this, &RunLogDialog::refresh);
  timer_->start();
  refresh();
}

bool RunLogDialog::following() const { return follow_->isChecked(); }

QString RunLogDialog::text() const { return view_->toPlainText(); }

void RunLogDialog::refresh() {
  QFile f(path_);
  if (!f.open(QIODevice::ReadOnly)) {
    status_->setText(QFileInfo::exists(path_)
                         ? tr("Cannot read the file.")
                         : tr("No log yet: the run has not started writing."));
    return;
  }
  const qint64 size = f.size();
  if (size < read_) { // a new run of the cell has started the file again
    view_->clear();
    read_ = 0;
  }
  if (size == read_) {
    status_->setText(tr("%n line(s)", nullptr, view_->blockCount()));
    return;
  }
  const bool follow = follow_->isChecked();
  QScrollBar *bar = view_->verticalScrollBar();
  const int keep = bar->value();
  bool cut = false;
  if (read_ == 0 && size > kMost) {
    read_ = size - kMost;
    cut = true;
  }
  f.seek(read_);
  QByteArray more = f.readAll();
  // Whole lines only: a line the run is in the middle of waits for its end.
  const qsizetype end = more.lastIndexOf('\n');
  if (end < 0 && size - read_ < 64 * 1024)
    return;
  if (end >= 0)
    more.truncate(end + 1);
  read_ += more.size();
  QString text = QString::fromUtf8(more);
  if (cut)
    text = tr("(the beginning is left out; the whole log is in the file)") +
           QLatin1Char('\n') + text.mid(text.indexOf(QLatin1Char('\n')) + 1);
  if (text.endsWith(QLatin1Char('\n')))
    text.chop(1);
  {
    // Adding at the end must not count as the reader scrolling.
    const QSignalBlocker block(bar);
    QTextCursor at(view_->document());
    at.movePosition(QTextCursor::End);
    if (!view_->document()->isEmpty())
      at.insertText(QStringLiteral("\n"));
    at.insertText(text);
    bar->setValue(follow ? bar->maximum() : keep);
  }
  status_->setText(tr("%n line(s)", nullptr, view_->blockCount()));
}

bool RunLogDialog::findNext(const QString &text) {
  if (text.isEmpty())
    return false;
  if (view_->find(text))
    return true;
  // From the top again.
  QTextCursor top = view_->textCursor();
  const QTextCursor was = top;
  top.movePosition(QTextCursor::Start);
  view_->setTextCursor(top);
  if (view_->find(text))
    return true;
  view_->setTextCursor(was);
  return false;
}

} // namespace skg
