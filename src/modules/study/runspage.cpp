#include "modules/study/runspage.h"

#include "modules/study/settinghelp.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <iterator>

#include "core/theme.h"
#include "core/defaults.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/study/runlog.h"
#include "modules/study/runtext.h"
#include "modules/study/studydisk.h"
#include "study/runinfo.h"
#include "study/store.h"

namespace skg {

using study::Case;
using study::Cell;
using study::CellState;
using study::Recipe;
using study::Study;

namespace {

constexpr double kLabelWidth = 250, kHeaderHeight = 58, kRowHeight = 44;

QString toolName(const QString &tool) {
  if (tool == study::kToolIndicators)
    return QCoreApplication::translate("skg::RunsPage", "Indicators");
  if (tool == study::kToolFeatures)
    return QCoreApplication::translate("skg::RunsPage", "Features");
  if (tool == study::kToolClassification)
    return QCoreApplication::translate("skg::RunsPage", "Classification");
  return tool;
}

// A setting's value as one line of text.
QString valueText(const QVariant &v) {
  switch (v.metaType().id()) {
  case QMetaType::Bool:
    return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  case QMetaType::QVariantList:
  case QMetaType::QStringList: {
    QStringList parts;
    for (const QVariant &e : v.toList())
      parts << valueText(e);
    return parts.join(QStringLiteral(", "));
  }
  case QMetaType::Double:
    return QString::number(v.toDouble(), 'g', 10);
  default:
    break;
  }
  const QString s = v.toString();
  const qsizetype lines = s.count(QLatin1Char('\n')) + 1;
  if (lines > 1 || s.size() > 80)
    return QCoreApplication::translate("skg::RunsPage", "(text, %n line(s))",
                                       nullptr, int(lines));
  return s;
}

bool isLongText(const QVariant &v) {
  if (v.metaType().id() != QMetaType::QString)
    return false;
  const QString s = v.toString();
  return s.contains(QLatin1Char('\n')) || s.size() > 80;
}

// Text typed for a setting, as the type the setting had.
QVariant parseAs(const QVariant &old, const QString &text, bool *ok) {
  *ok = true;
  const QString t = text.trimmed();
  switch (old.metaType().id()) {
  case QMetaType::Bool: {
    const QString l = t.toLower();
    if (l == QLatin1String("true") || l == QLatin1String("yes") ||
        l == QLatin1String("1"))
      return true;
    if (l == QLatin1String("false") || l == QLatin1String("no") ||
        l == QLatin1String("0"))
      return false;
    *ok = false;
    return old;
  }
  case QMetaType::Int:
  case QMetaType::LongLong: {
    const qlonglong v = t.toLongLong(ok);
    return *ok ? QVariant(v) : old;
  }
  case QMetaType::Double: {
    const double v = t.toDouble(ok);
    return *ok ? QVariant(v) : old;
  }
  case QMetaType::QVariantList:
  case QMetaType::QStringList: {
    QVariantList list;
    for (const QString &part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const QString p = part.trimmed();
      bool isInt = false, isDouble = false;
      const qlonglong i = p.toLongLong(&isInt);
      const double d = p.toDouble(&isDouble);
      if (isInt)
        list << i;
      else if (isDouble)
        list << d;
      else
        list << p;
    }
    return list;
  }
  default:
    return t;
  }
}

bool fittedPerCase(const Recipe &r, const QString &key) {
  if (key == QLatin1String("mc"))
    return r.settings.value(QStringLiteral("estimate_mc")).toBool();
  return r.fitted.contains(key);
}

} // namespace

// ---------------------------------------------------------------- matrix

RunMatrix::RunMatrix(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFocusPolicy(Qt::StrongFocus);
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void RunMatrix::setContents(const QStringList &rowLabels,
                            const QList<QPair<QString, QString>> &columns,
                            const QList<QList<MatrixCell>> &cells) {
  rows_ = rowLabels;
  columns_ = columns;
  cells_ = cells;
  if (row_ >= rows_.size())
    row_ = -1;
  if (column_ >= columns_.size())
    column_ = columns_.isEmpty() ? -1 : 0;
  if (column_ < 0 && !columns_.isEmpty())
    column_ = 0;
  for (auto it = more_.begin(); it != more_.end();)
    it = (*it / 65536 >= rows_.size() || *it % 65536 >= columns_.size())
             ? more_.erase(it)
             : std::next(it);
  updateGeometry();
  update();
}

QList<QPair<int, int>> RunMatrix::selectedCells() const {
  QList<QPair<int, int>> out;
  if (row_ >= 0 && column_ >= 0)
    out << qMakePair(row_, column_);
  QList<qint64> rest(more_.begin(), more_.end());
  std::sort(rest.begin(), rest.end());
  for (const qint64 v : rest) {
    const auto cell = qMakePair(int(v / 65536), int(v % 65536));
    if (!out.contains(cell))
      out << cell;
  }
  return out;
}

void RunMatrix::toggle(int row, int column) {
  if (row < 0 || column < 0 || row >= rows_.size() ||
      column >= columns_.size())
    return;
  const qint64 v = qint64(row) * 65536 + column;
  if (row == row_ && column == column_) {
    // The main cell leaves: another selected one takes its place.
    more_.remove(v);
    if (more_.isEmpty()) {
      row_ = -1;
    } else {
      const qint64 next = *std::min_element(more_.begin(), more_.end());
      more_.remove(next);
      row_ = int(next / 65536);
      column_ = int(next % 65536);
    }
  } else if (more_.contains(v)) {
    more_.remove(v);
  } else if (row_ < 0) {
    row_ = row;
    column_ = column;
  } else {
    more_.insert(v);
  }
  update();
  emit selectionChanged();
}

bool RunMatrix::hit(const QPointF &pos, int *row, int *column) const {
  *row = *column = -1;
  if (pos.y() >= kHeaderHeight) {
    const int r = int((pos.y() - kHeaderHeight) / kRowHeight);
    if (r < rows_.size())
      *row = r;
  }
  if (pos.x() >= kLabelWidth)
    for (int c = 0; c < int(columns_.size()); ++c)
      if (pos.x() >= cellRect(0, c).left() && pos.x() < cellRect(0, c).right())
        *column = c;
  if (pos.y() >= kHeaderHeight && *row < 0)
    return false; // under the last row
  if (pos.x() >= kLabelWidth && *column < 0)
    return false; // right of the last column
  return *row >= 0 || *column >= 0;
}

QString RunMatrix::labelTip(int row, int column) const {
  if (row >= 0 && column < 0 && row < rows_.size())
    return rows_[row];
  if (row < 0 && column >= 0 && column < columns_.size())
    return tr("%1 (%2)").arg(columns_[column].first,
                             toolName(columns_[column].second));
  return {};
}

void RunMatrix::select(int row, int column) {
  if (row == row_ && column == column_ && more_.isEmpty())
    return;
  more_.clear();
  row_ = row;
  column_ = column;
  update();
  // Keep the cell in view when the matrix scrolls.
  if (row_ >= 0 && column_ >= 0)
    for (QWidget *w = parentWidget(); w; w = w->parentWidget())
      if (auto *area = qobject_cast<QScrollArea *>(w)) {
        const QPointF c = cellRect(row_, column_).center();
        area->ensureVisible(int(c.x()), int(c.y()), 90, int(kRowHeight));
        break;
      }
  emit selectionChanged();
}

QSize RunMatrix::minimumSizeHint() const {
  return {int(kLabelWidth + 170 * std::max<qsizetype>(1, columns_.size()) + 24),
          int(kHeaderHeight +
              kRowHeight * std::max<qsizetype>(1, rows_.size()) + 16)};
}

QRectF RunMatrix::cellRect(int row, int column) const {
  const double w = (width() - kLabelWidth - 24) /
                   double(std::max<qsizetype>(1, columns_.size()));
  return {kLabelWidth + column * w, kHeaderHeight + row * kRowHeight, w,
          kRowHeight};
}

QString RunMatrix::stateText(const MatrixCell &c) {
  if (c.running)
    return c.fraction >= 0 ? tr("◔  Running %1%").arg(qRound(c.fraction * 100))
                           : tr("◔  Running");
  // Asked for and waiting its turn: said as that, whatever the cell was
  // before (missing, stale, failed).
  if (c.queued)
    return c.place > 0 ? tr("⋯  Queued %1").arg(c.place) : tr("⋯  Queued");
  switch (c.state) {
  case CellState::Done:
    return c.warnings.isEmpty() ? tr("✓  Done") : tr("✓  Done  ⚠");
  case CellState::Missing:
    return tr("○  Missing");
  case CellState::Stale:
    return tr("↻  Stale");
  case CellState::Failed:
    return tr("✕  Failed");
  case CellState::Blocked:
    return tr("⏸  Waiting");
  case CellState::NotApplicable:
    break;
  }
  return QStringLiteral("–");
}

void RunMatrix::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  QPainterPath card;
  card.addRoundedRect(r, 8, 8);
  p.fillPath(card, col("surface"));
  p.setPen(col("border"));
  p.drawPath(card);

  QFont bold = font();
  bold.setWeight(QFont::DemiBold);
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.85);

  for (int c = 0; c < int(columns_.size()); ++c) {
    const QRectF h = cellRect(-1, c).adjusted(8, 6, -4, 0);
    if (c == column_) {
      p.setPen(Qt::NoPen);
      p.setBrush(col("surface2"));
      p.drawRoundedRect(
          QRectF(h.left() - 6, 6, h.width() + 4,
                 kHeaderHeight - 10 + kRowHeight * double(rows_.size())),
          6, 6);
    }
    p.setPen(col("text"));
    p.setFont(bold);
    p.drawText(QRectF(h.left(), 10, h.width(), 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               p.fontMetrics().elidedText(columns_[c].first, Qt::ElideRight,
                                          int(h.width())));
    p.setPen(col("muted"));
    p.setFont(small);
    p.drawText(QRectF(h.left(), 30, h.width(), 18),
               Qt::AlignLeft | Qt::AlignVCenter, toolName(columns_[c].second));
  }
  for (int i = 0; i < int(rows_.size()); ++i) {
    const double y = kHeaderHeight + i * kRowHeight;
    p.setPen(col("border"));
    p.drawLine(QPointF(12, y), QPointF(width() - 12, y));
    p.setPen(col("text"));
    p.setFont(font());
    p.drawText(QRectF(18, y, kLabelWidth - 24, kRowHeight), Qt::AlignVCenter,
               p.fontMetrics().elidedText(rows_[i], Qt::ElideRight,
                                          int(kLabelWidth - 24)));
    for (int c = 0; c < int(columns_.size()); ++c) {
      const MatrixCell &cell = cells_[i][c];
      const QRectF whole = cellRect(i, c);
      const QRectF chip(whole.left() + 8, whole.top() + 9,
                        std::min(150.0, whole.width() - 16), 26);
      const QString text = stateText(cell);
      if (!cell.running && cell.state == CellState::NotApplicable) {
        p.setPen(col("muted"));
        p.drawText(chip, Qt::AlignVCenter, text);
        continue;
      }
      // A status colour with a glyph and a word, never the colour alone.
      QColor tone = col("muted");
      bool dashed = false;
      if (cell.running)
        tone = col("accent");
      else if (cell.queued)
        tone = col("muted"); // a full outline, where a missing cell is dashed
      else if (cell.state == CellState::Done)
        tone = theme_->statusColor(QStringLiteral("good"));
      else if (cell.state == CellState::Stale)
        tone = theme_->statusColor(QStringLiteral("warning"));
      else if (cell.state == CellState::Failed)
        tone = theme_->statusColor(QStringLiteral("serious"));
      else
        dashed = true;
      QColor fill = tone;
      fill.setAlphaF(dashed ? 0.0f : 0.14f);
      p.setPen(QPen(dashed ? col("border") : tone, 1,
                    dashed ? Qt::DashLine : Qt::SolidLine));
      p.setBrush(fill);
      p.drawRoundedRect(chip, 13, 13);
      const bool main = i == row_ && c == column_;
      if (main || more_.contains(qint64(i) * 65536 + c)) {
        // A selected cell; a second, dotted ring on the one the keys act on.
        p.setPen(QPen(col("accent"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chip.adjusted(-3, -3, 3, 3), 15, 15);
        if (main && hasFocus()) {
          p.setPen(QPen(col("text"), 1, Qt::DotLine));
          p.drawRoundedRect(chip.adjusted(-6, -6, 6, 6), 18, 18);
        }
      }
      p.setPen(col("text"));
      p.setFont(font());
      p.drawText(chip.adjusted(12, 0, -6, 0), Qt::AlignVCenter, text);
    }
  }
}

// What a cell says on hover: its state in words, the place in the queue and
// what it waits for, why it waits or failed, and what to know about its run.
QString RunMatrix::cellTip(const MatrixCell &c) {
  QStringList lines;
  if (c.queued && !c.running) {
    lines << (!c.waitsFor.isEmpty()
                  ? tr("Queued: waiting for %1, which it is run after")
                        .arg(c.waitsFor)
                  : tr("Queued, number %1 in the queue").arg(c.place));
  } else {
    lines << stateText(c).trimmed();
    if (c.running && c.elapsedMs >= 0)
      lines << tr("running for %1").arg(RunWords::duration(c.elapsedMs / 1000));
    if (c.running && !c.doing.isEmpty())
      lines << c.doing; // "2/3 Calibrating … · about 4 min left"
    if (!c.running && !c.why.isEmpty())
      lines << c.why;
  }
  for (const QString &w : c.warnings)
    lines << QStringLiteral("⚠  ") + w;
  return lines.join(QLatin1Char('\n'));
}

bool RunMatrix::event(QEvent *e) {
  if (e->type() == QEvent::ToolTip) {
    const auto *help = static_cast<QHelpEvent *>(e);
    for (int i = 0; i < int(rows_.size()); ++i)
      for (int c = 0; c < int(columns_.size()); ++c)
        if (cellRect(i, c).contains(help->pos()) &&
            cells_[i][c].state != CellState::NotApplicable) {
          QToolTip::showText(help->globalPos(),
                             ui::helpText(cellTip(cells_[i][c])), this);
          return true;
        }
    // The full name where a row's name or a heading is cut short.
    int row = -1, column = -1;
    if (hit(help->pos(), &row, &column) && (row < 0 || column < 0)) {
      QToolTip::showText(help->globalPos(), labelTip(row, column), this);
      return true;
    }
    QToolTip::hideText();
    e->ignore();
    return true;
  }
  return QWidget::event(e);
}

void RunMatrix::mousePressEvent(QMouseEvent *e) {
  const QPointF pos = e->position();
  int column = column_, row = -1;
  for (int c = 0; c < int(columns_.size()); ++c)
    if (pos.x() >= cellRect(0, c).left() && pos.x() < cellRect(0, c).right())
      column = c;
  if (pos.y() >= kHeaderHeight)
    row = int((pos.y() - kHeaderHeight) / kRowHeight);
  if (row >= rows_.size())
    row = -1;
  if (pos.x() < kLabelWidth)
    column = column_;
  const bool onCell = row >= 0 && pos.x() >= kLabelWidth;
  if (e->button() == Qt::RightButton) {
    // The menu acts on the selection when the click is in it; else on what
    // is under the pointer (contextMenuEvent).
    const bool inSelection =
        onCell && selectedCells().contains(qMakePair(row, column));
    if (!inSelection)
      select(row, column);
    return;
  }
  if (onCell && (e->modifiers() & Qt::ControlModifier)) {
    toggle(row, column);
    return;
  }
  if (onCell && (e->modifiers() & Qt::ShiftModifier) && row_ >= 0 &&
      column_ >= 0) {
    // The block between the main cell and this one.
    const int r0 = std::min(row_, row), r1 = std::max(row_, row);
    const int c0 = std::min(column_, column), c1 = std::max(column_, column);
    more_.clear();
    for (int r = r0; r <= r1; ++r)
      for (int c = c0; c <= c1; ++c)
        if (r != row_ || c != column_)
          more_.insert(qint64(r) * 65536 + c);
    update();
    emit selectionChanged();
    return;
  }
  select(row, column);
}

void RunMatrix::contextMenuEvent(QContextMenuEvent *e) {
  int row = -1, column = -1;
  if (e->reason() == QContextMenuEvent::Keyboard) {
    if (row_ >= 0 && column_ >= 0)
      emit menuRequested(row_, column_,
                         mapToGlobal(cellRect(row_, column_).center().toPoint()));
    return;
  }
  if (hit(QPointF(e->pos()), &row, &column))
    emit menuRequested(row, column, e->globalPos());
}

void RunMatrix::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->position().y() >= kHeaderHeight && e->position().x() >= kLabelWidth &&
      row_ >= 0 && column_ >= 0)
    emit opened();
}

void RunMatrix::keyPressEvent(QKeyEvent *e) {
  const int rows = int(rows_.size()), columns = int(columns_.size());
  if (columns == 0) {
    QWidget::keyPressEvent(e);
    return;
  }
  int row = row_, column = std::max(0, column_);
  switch (e->key()) {
  case Qt::Key_Left:
    column = std::max(0, column - 1);
    break;
  case Qt::Key_Right:
    column = std::min(columns - 1, column + 1);
    break;
  case Qt::Key_Up:
    row = std::max(rows > 0 ? 0 : -1, row - 1);
    break;
  case Qt::Key_Down:
    row = std::min(rows - 1, row + 1);
    break;
  case Qt::Key_Home:
    row = rows > 0 ? 0 : -1;
    break;
  case Qt::Key_End:
    row = rows - 1;
    break;
  case Qt::Key_A:
    if (e->modifiers() & Qt::ControlModifier) {
      more_.clear();
      for (int r = 0; r < rows; ++r)
        for (int c = 0; c < columns; ++c)
          if (r != row_ || c != column_)
            more_.insert(qint64(r) * 65536 + c);
      if (row_ < 0 && rows > 0) {
        row_ = 0;
        column_ = std::max(0, column_);
        more_.remove(qint64(row_) * 65536 + column_);
      }
      update();
      emit selectionChanged();
      return;
    }
    QWidget::keyPressEvent(e);
    return;
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Space:
    if (row_ >= 0 && column_ >= 0)
      emit activated();
    return;
  default:
    QWidget::keyPressEvent(e);
    return;
  }
  select(row, column);
}

void RunMatrix::focusInEvent(QFocusEvent *e) {
  QWidget::focusInEvent(e);
  if (row_ < 0 && !rows_.isEmpty() && !columns_.isEmpty())
    select(0, std::max(0, column_));
  update();
}

void RunMatrix::focusOutEvent(QFocusEvent *e) {
  QWidget::focusOutEvent(e);
  update();
}

// ---------------------------------------------------------------- recipe

namespace {

const QString kConfigKey = QStringLiteral("config_json");
const QString kFeaturesKey = QStringLiteral("features_recipe");

} // namespace

RecipeDialog::RecipeDialog(const Study &s, const Recipe &r, QWidget *parent)
    : QDialog(parent), recipe_(r) {
  setWindowTitle(tr("Recipe: %1").arg(toolName(r.tool)));
  name_ = new QLineEdit(r.name, this);
  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("settingsTable"));
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels({tr("Setting"), tr("Value")});
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  table_->setRowCount(int(r.settings.size()));
  int row = 0;
  for (auto it = r.settings.begin(); it != r.settings.end(); ++it, ++row) {
    // The readable name is shown; the key travels in the item.
    const SettingInfo info = settingInfo(r.tool, it.key());
    auto *k = new QTableWidgetItem(info.label);
    k->setData(Qt::UserRole, it.key());
    k->setToolTip(info.tooltip(it.key()));
    k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    auto *v = new QTableWidgetItem(valueText(it.value()));
    v->setData(Qt::UserRole, it.value());
    v->setToolTip(info.tooltip(it.key()));
    if (isLongText(it.value()))
      v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    table_->setItem(row, 0, k);
    table_->setItem(row, 1, v);
    if (r.tool == study::kToolClassification && it.key() == kFeaturesKey) {
      // Chosen from the study's features recipes, not typed as an id.
      featuresBox_ = new QComboBox(table_);
      featuresBox_->setObjectName(QStringLiteral("featuresRecipe"));
      featuresBox_->addItem(tr("(none)"), QString());
      for (const Recipe &o : s.recipes)
        if (o.tool == study::kToolFeatures)
          featuresBox_->addItem(o.name, o.id);
      const QString id = it.value().toString();
      if (!id.isEmpty() && featuresBox_->findData(id) < 0)
        featuresBox_->addItem(tr("%1 (no longer in the study)").arg(id), id);
      featuresBox_->setCurrentIndex(std::max(0, featuresBox_->findData(id)));
      ui::setHelp(featuresBox_, info.help);
      v->setText(QString());
      v->setFlags(Qt::ItemIsEnabled);
      table_->setCellWidget(row, 1, featuresBox_);
    }
  }
  table_->setColumnWidth(0, 250);

  QString note =
      tr("Values keep their kind: true/false, a number, text, or a list "
         "separated by commas. Changing a setting makes the recipe's earlier "
         "runs stale.");
  if (r.tool == study::kToolClassification) {
    note += QLatin1Char('\n') +
            (featuresBox_ && featuresBox_->count() > 1
                 ? tr("\"Features recipe\" is the recipe whose feature table "
                      "is classified. Classes are ranges of days before the "
                      "event.")
                 : tr("The study has no features recipe yet, and a "
                      "classification needs one to take its table from: add "
                      "a Features recipe, then choose it here. Classes are "
                      "ranges of days before the event."));
  } else if (r.tool == study::kToolFeatures) {
    note += QLatin1Char('\n') +
            tr("\"Features configuration\" is a config.json of the Features "
               "page; load it with the button below. Empty uses the "
               "defaults.");
  }
  auto *noteLabel = new QLabel(note, this);
  noteLabel->setObjectName(QStringLiteral("fieldLabel"));
  noteLabel->setWordWrap(true);
  auto *load =
      new QPushButton(tr("Load a file into the selected setting…"), this);
  load->setToolTip(ui::helpText(
      tr("Reads a text file (a config.json, a project.toml) into the "
         "selected setting. Only settings that hold text take a file.")));
  connect(load, &QPushButton::clicked, this, &RecipeDialog::loadFile);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ui::setPrimary(buttons->button(QDialogButtonBox::Ok));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto *form = new QFormLayout;
  form->addRow(tr("Name"), name_);
  auto *bottom = new QHBoxLayout;
  bottom->addWidget(load);
  bottom->addStretch(1);
  bottom->addWidget(buttons);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(table_, 1);
  layout->addWidget(noteLabel);
  if (r.tool == study::kToolFeatures) {
    // Which catalog the calibration reads is said, never left implicit in
    // the configuration's text.
    override_ = new QLabel(this);
    override_->setObjectName(QStringLiteral("calibrationCatalog"));
    override_->setWordWrap(true);
    override_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    clearOverride_ = new QPushButton(tr("Use the study's catalog"), this);
    clearOverride_->setObjectName(QStringLiteral("clearOverride"));
    clearOverride_->setToolTip(ui::helpText(
        tr("Takes the configuration's own calibration catalog out, so that "
           "this recipe reads the study's catalog like every other.")));
    connect(clearOverride_, &QPushButton::clicked, this, [this] {
      const int cfg = rowOf(kConfigKey);
      if (cfg >= 0)
        loadText(kConfigKey,
                 table_->item(cfg, 1)->data(Qt::UserRole).toString(), false);
    });
    auto *line = new QHBoxLayout;
    line->addWidget(override_, 1);
    line->addWidget(clearOverride_);
    layout->addLayout(line);
    showOverride();
  }
  layout->addLayout(bottom);
  resize(640, 620);
  ui::setHelp(name_, tr("The recipe's name: the column heading on the Runs "
                        "page and the entry in the Compare lists. The "
                        "result folders use an id made from the first "
                        "name, which does not change on a rename."));
}

int RecipeDialog::rowOf(const QString &key) const {
  for (int row = 0; row < table_->rowCount(); ++row)
    if (table_->item(row, 0)->data(Qt::UserRole).toString() == key)
      return row;
  return -1;
}

QString RecipeDialog::ownCatalog() const {
  const int row = rowOf(kConfigKey);
  if (recipe_.tool != study::kToolFeatures || row < 0)
    return {};
  Recipe r = recipe_;
  r.settings.insert(kConfigKey, table_->item(row, 1)->data(Qt::UserRole));
  return study::calibrationOverride(r);
}

void RecipeDialog::showOverride() {
  if (!override_)
    return;
  const QString own = ownCatalog();
  override_->setText(
      own.isEmpty()
          ? tr("Calibration catalog: the study's.")
          : tr("⚠  Calibration catalog: the configuration's own, %1. This "
               "recipe does not read the study's catalog.")
                .arg(QDir::toNativeSeparators(own)));
  clearOverride_->setVisible(!own.isEmpty());
}

bool RecipeDialog::loadText(const QString &key, const QString &text,
                            bool keepOwnCatalog) {
  const int row = rowOf(key);
  if (row < 0)
    return false;
  QTableWidgetItem *v = table_->item(row, 1);
  if (v->data(Qt::UserRole).metaType().id() != QMetaType::QString ||
      table_->cellWidget(row, 1))
    return false;
  QString value = text;
  if (recipe_.tool == study::kToolFeatures && key == kConfigKey &&
      !keepOwnCatalog) {
    // The study's catalog is used: the configuration's own name is cleared.
    QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
    QJsonObject cal = o.value(QStringLiteral("chaos_calibration")).toObject();
    if (!cal.value(QStringLiteral("catalog")).toString().isEmpty()) {
      cal.insert(QStringLiteral("catalog"), QString());
      o.insert(QStringLiteral("chaos_calibration"), cal);
      value = QString::fromUtf8(QJsonDocument(o).toJson());
    }
  }
  v->setData(Qt::UserRole, value);
  v->setText(valueText(value));
  v->setFlags(isLongText(value) ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                : Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsEditable);
  showOverride();
  return true;
}

void RecipeDialog::loadFile() {
  int row = table_->currentRow();
  if (row < 0 && recipe_.tool == study::kToolFeatures)
    row = rowOf(kConfigKey); // the only setting a file can go into
  if (row < 0) {
    QMessageBox::information(
        this, windowTitle(),
        tr("Select the setting the file is for first (a setting that holds "
           "text)."));
    return;
  }
  const QTableWidgetItem *v = table_->item(row, 1);
  if (v->data(Qt::UserRole).metaType().id() != QMetaType::QString ||
      table_->cellWidget(row, 1)) {
    QMessageBox::information(this, windowTitle(),
                             tr("Only a text setting can take a file."));
    return;
  }
  const QString path = ui::pickFile(this, settings,
                                    QStringLiteral("recipeFile"), windowTitle());
  if (path.isEmpty())
    return;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, windowTitle(), f.errorString());
    return;
  }
  const QString text = QString::fromUtf8(f.readAll());
  const QString key = table_->item(row, 0)->data(Qt::UserRole).toString();
  bool keep = true;
  if (recipe_.tool == study::kToolFeatures && key == kConfigKey) {
    Recipe probe = recipe_;
    probe.settings.insert(kConfigKey, text);
    const QString own = study::calibrationOverride(probe);
    if (!own.isEmpty()) {
      QMessageBox box(QMessageBox::Question, windowTitle(),
                      tr("This configuration names a calibration catalog of "
                         "its own:\n%1\n\nIn a study every case is normally "
                         "calibrated on the study's catalog. Which one "
                         "should this recipe read?")
                          .arg(QDir::toNativeSeparators(own)),
                      QMessageBox::NoButton, this);
      QPushButton *studys =
          box.addButton(tr("The study's catalog"), QMessageBox::AcceptRole);
      box.addButton(tr("The configuration's own"), QMessageBox::RejectRole);
      box.setDefaultButton(studys);
      box.exec();
      keep = box.clickedButton() != studys;
    }
  }
  loadText(key, text, keep);
}

void RecipeDialog::accept() {
  Recipe next = recipe_;
  if (!name_->text().trimmed().isEmpty())
    next.name = name_->text().trimmed();
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QString k = table_->item(row, 0)->data(Qt::UserRole).toString();
    const QTableWidgetItem *v = table_->item(row, 1);
    const QVariant old = v->data(Qt::UserRole);
    if (featuresBox_ && table_->cellWidget(row, 1) == featuresBox_) {
      next.settings.insert(k, featuresBox_->currentData().toString());
      continue;
    }
    if (isLongText(old) || v->text() == valueText(old)) {
      next.settings.insert(k, old);
      continue;
    }
    bool ok = false;
    const QVariant parsed = parseAs(old, v->text(), &ok);
    if (!ok) {
      QMessageBox::warning(
          this, windowTitle(),
          tr("%1: \"%2\" is not the kind of value this setting takes.")
              .arg(table_->item(row, 0)->text(), v->text()));
      return;
    }
    next.settings.insert(k, parsed);
  }
  if (next.settings != recipe_.settings)
    next.revision = recipe_.revision + 1;
  recipe_ = next;
  QDialog::accept();
}

// ------------------------------------------------------------------ page

RunsPage::RunsPage(AppContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  confirm = [this](const QString &title, const QString &text) {
    return QMessageBox::question(this, title, text) == QMessageBox::Yes;
  };
  freeSpace = [](const QString &dir) { return study::freeBytesAt(dir); };
  stack_ = new QStackedWidget(this);

  // No study open: the window's own bar above offers New and Open.
  noStudy_ = new QLabel(this);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  stack_->addWidget(noStudy_);

  auto *main = new QWidget(this);
  run_ = new QPushButton(main);
  run_->setObjectName(QStringLiteral("runAll"));
  ui::setPrimary(run_);
  stop_ = new QPushButton(main);
  stop_->setObjectName(QStringLiteral("stopBatch"));
  runOne_ = new QPushButton(main);
  runOne_->setObjectName(QStringLiteral("runOne"));
  new_ = new QToolButton(main);
  new_->setObjectName(QStringLiteral("studySwitch"));
  new_->setPopupMode(QToolButton::InstantPopup);
  auto *menu = new QMenu(new_);
  new_->setMenu(menu);
  connect(menu, &QMenu::aboutToShow, this, [this, menu] {
    menu->clear();
    for (const QString &tool : study::recipeTools())
      menu->addAction(toolName(tool), this, [this, tool] { newRecipe(tool); });
  });
  edit_ = new QPushButton(main);
  remove_ = new QPushButton(main);
  hint_ = new QLabel(main);
  hint_->setObjectName(QStringLiteral("fieldLabel"));
  auto *bar = new QHBoxLayout;
  bar->addWidget(run_);
  bar->addWidget(stop_);
  bar->addWidget(runOne_);
  recal_ = new QPushButton(main);
  recal_->setObjectName(QStringLiteral("recalibrate"));
  recal_->hide();
  bar->addWidget(recal_);
  bar->addSpacing(12);
  bar->addWidget(new_);
  bar->addWidget(edit_);
  bar->addWidget(remove_);
  bar->addStretch(1);
  bar->addWidget(hint_);

  // What to do next, when the matrix alone does not say it.
  guide_ = new QLabel(main);
  guide_->setObjectName(QStringLiteral("runsGuide"));
  guide_->setWordWrap(true);
  guideGo_ = new QPushButton(main);
  guideGo_->setObjectName(QStringLiteral("runsGuideGo"));
  auto *guideRow = new QHBoxLayout;
  guideRow->addWidget(guide_, 1);
  guideRow->addWidget(guideGo_);

  // Which rows are shown, when the study has many.
  filter_ = new QLineEdit(main);
  filter_->setObjectName(QStringLiteral("runsFilter"));
  filter_->setClearButtonEnabled(true);
  filter_->setMinimumWidth(250);
  filter_->setMaximumWidth(320);
  show_ = new QComboBox(main);
  show_->setObjectName(QStringLiteral("runsShow"));
  shown_ = new QLabel(main);
  shown_->setObjectName(QStringLiteral("fieldLabel"));
  disk_ = new QPushButton(main);
  disk_->setObjectName(QStringLiteral("studyOnDisk"));
  auto *filterRow = new QHBoxLayout;
  filterRow->addWidget(filter_);
  filterRow->addWidget(show_);
  filterRow->addWidget(shown_, 1);
  filterRow->addWidget(disk_);

  matrix_ = new RunMatrix(ctx_.theme, main);
  auto *scroll = new QScrollArea(main);
  scroll->setWidget(matrix_);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  cellInfo_ = new QLabel(main);
  cellInfo_->setObjectName(QStringLiteral("cellInfo"));
  cellInfo_->setWordWrap(true);
  cellInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  fix_ = new QPushButton(main);
  fix_->setObjectName(QStringLiteral("fixCell"));
  showLog_ = new QPushButton(main);
  showLog_->setObjectName(QStringLiteral("showLog"));
  openFolder_ = new QPushButton(main);
  openFolder_->setObjectName(QStringLiteral("openFolder"));
  auto *infoRow = new QHBoxLayout;
  infoRow->addWidget(cellInfo_, 1);
  infoRow->addWidget(fix_);
  infoRow->addWidget(showLog_);
  infoRow->addWidget(openFolder_);

  auto *card = new QFrame(main);
  card->setObjectName(QStringLiteral("card"));
  recipeTitle_ = new QLabel(card);
  recipeTitle_->setObjectName(QStringLiteral("cardTitle"));
  recipeTable_ = new QTableWidget(card);
  recipeTable_->setObjectName(QStringLiteral("recipeTable"));
  recipeTable_->setColumnCount(3);
  recipeTable_->verticalHeader()->hide();
  recipeTable_->setShowGrid(false);
  recipeTable_->setAlternatingRowColors(true);
  recipeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  recipeTable_->setSelectionMode(QAbstractItemView::NoSelection);
  recipeTable_->horizontalHeader()->setStretchLastSection(true);
  recipeTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                        Qt::AlignVCenter);
  auto *cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(16, 12, 16, 12);
  cardLayout->addWidget(recipeTitle_);
  cardLayout->addWidget(recipeTable_, 1);

  // What ran, when, how long and how it ended: read from the run folders.
  auto *activityPage = new QWidget(main);
  activity_ = new QTableWidget(activityPage);
  activity_->setObjectName(QStringLiteral("activityTable"));
  activity_->setColumnCount(7);
  activity_->verticalHeader()->hide();
  activity_->setShowGrid(false);
  activity_->setAlternatingRowColors(true);
  activity_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  activity_->setSelectionBehavior(QAbstractItemView::SelectRows);
  activity_->setSelectionMode(QAbstractItemView::SingleSelection);
  activity_->horizontalHeader()->setStretchLastSection(true);
  activity_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                     Qt::AlignVCenter);
  activity_->setSortingEnabled(true);
  zone_ = new QLabel(activityPage);
  zone_->setObjectName(QStringLiteral("fieldLabel"));
  auto *activityLayout = new QVBoxLayout(activityPage);
  activityLayout->setContentsMargins(0, 8, 0, 0);
  activityLayout->addWidget(activity_, 1);
  activityLayout->addWidget(zone_);
  tabs_ = new QTabWidget(main);
  tabs_->setObjectName(QStringLiteral("runsTabs"));
  tabs_->addTab(card, QString());
  tabs_->addTab(activityPage, QString());

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addLayout(bar);
  mainLayout->addLayout(guideRow);
  mainLayout->addLayout(filterRow);
  mainLayout->addWidget(scroll, 3);
  mainLayout->addLayout(infoRow);
  mainLayout->addWidget(tabs_, 2);
  stack_->addWidget(main);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  // While jobs run only their progress is redrawn; the disk is read again
  // when a job ends.
  tick_ = new QTimer(this);
  tick_->setInterval(700);
  connect(tick_, &QTimer::timeout, this, [this] {
    const qsizetype before = working_.size();
    render(false);
    if (working_.size() != before) {
      reload(); // a worker has left its run: its cell can be run again,
      pump();   // and its slot is free for the next in the queue
    }
    if (running_.isEmpty() && working_.isEmpty() && queue_.isEmpty())
      tick_->stop();
  });
  connect(run_, &QPushButton::clicked, this, &RunsPage::runAll);
  connect(stop_, &QPushButton::clicked, this, &RunsPage::stopBatch);
  connect(runOne_, &QPushButton::clicked, this, [this] { runSelected(); });
  connect(recal_, &QPushButton::clicked, this, [this] { runSelected(true); });
  connect(edit_, &QPushButton::clicked, this, &RunsPage::editRecipe);
  connect(remove_, &QPushButton::clicked, this, &RunsPage::removeRecipe);
  connect(fix_, &QPushButton::clicked, this, &RunsPage::fixSelected);
  connect(showLog_, &QPushButton::clicked, this, &RunsPage::showLog);
  connect(openFolder_, &QPushButton::clicked, this,
          &RunsPage::openSelectedFolder);
  connect(guideGo_, &QPushButton::clicked, this, [this] {
    const QString page = guideGo_->property("page").toString();
    if (ctx_.nav && !page.isEmpty())
      ctx_.nav->show(page);
  });
  connect(matrix_, &RunMatrix::selectionChanged, this,
          &RunsPage::showSelection);
  // Enter runs the cell when it can run; otherwise it does what the cell
  // needs (go to Cases, edit the recipe). A double click opens the folder.
  connect(matrix_, &RunMatrix::activated, this, [this] {
    if (runOne_->isEnabled())
      runSelected();
    else if (!fix_->isHidden())
      fixSelected();
  });
  connect(matrix_, &RunMatrix::opened, this, &RunsPage::openSelectedFolder);
  connect(matrix_, &RunMatrix::menuRequested, this, &RunsPage::showMenu);
  connect(filter_, &QLineEdit::textChanged, this, &RunsPage::reload);
  connect(show_, &QComboBox::activated, this, &RunsPage::reload);
  connect(disk_, &QPushButton::clicked, this,
          [this] { showStudyOnDisk(ctx_, this); });
  connect(activity_->horizontalHeader(), &QHeaderView::sectionClicked, this,
          [this] { activity_->setProperty("sorted", true); });
  // A double click on a line of the activity list selects its cell.
  connect(activity_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    const QTableWidgetItem *it = activity_->item(row, 0);
    if (!it || !ctx_.study->isOpen())
      return;
    const QStringList k = it->data(Qt::UserRole).toString().split('|');
    const Study &s = ctx_.study->study();
    for (int i = 0; k.size() == 3 && i < int(rows_.size()); ++i)
      for (int j = 0; j < int(s.recipes.size()); ++j)
        if (keyOf(i, s.recipes[j]) == it->data(Qt::UserRole).toString())
          matrix_->select(i, j);
  });
  // The widths the reader gave the columns are kept.
  if (ctx_.settings) {
    const auto keep = [this](QTableWidget *t, const char *name) {
      const QString key = QStringLiteral("runs/") + QLatin1String(name);
      const QByteArray state = ctx_.settings->value(key).toByteArray();
      if (!state.isEmpty())
        t->horizontalHeader()->restoreState(state);
      connect(t->horizontalHeader(), &QHeaderView::sectionResized, this,
              [this, t, key] {
                ctx_.settings->setValue(key, t->horizontalHeader()->saveState());
              });
    };
    keep(activity_, "activityColumns");
    tabs_->setCurrentIndex(
        std::clamp(ctx_.settings->value(QStringLiteral("runs/tab")).toInt(), 0, 1));
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
      ctx_.settings->setValue(QStringLiteral("runs/tab"), i);
    });
  }
  connect(ctx_.study, &study::StudyStore::changed, this, &RunsPage::reload);

  retranslate();
  reload();
}

RunsPage::~RunsPage() {
  reportQueue(0); // nothing waits on a page that is gone
  for (const QString &dir : std::as_const(live_))
    study::LiveRuns::leave(dir);
}

void RunsPage::reportQueue(int waiting) {
  if (ctx_.queue)
    ctx_.queue->set(QStringLiteral("runs"), waiting);
}

QString RunsPage::lockProblem() const {
  if (!ctx_.study->isOpen() || ctx_.study->holdsLock())
    return {};
  return tr("⚠  Nothing can be run into this study now: its folder is not "
            "held by this window. Either the folder cannot be written "
            "(read-only media, a drive that is full or was unplugged), or "
            "the study is open in another window, which is where runs are "
            "started. Results that are there can still be looked at.");
}

void RunsPage::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    reload(); // the reasons are text, too
  }
  QWidget::changeEvent(e);
}

void RunsPage::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  reload(); // another page, or the user, may have changed what is on disk
}

void RunsPage::retranslate() {
  noStudy_->setText(tr("No study is open.\nA study is a folder that holds "
                       "the cases, the recipes and every result. Start or "
                       "open one with the buttons above."));
  stop_->setText(tr("Stop"));
  recal_->setText(tr("Recalibrate"));
  recal_->setToolTip(ui::helpText(
      tr("Runs the selected features cell with a new calibration. A "
         "features run keeps its calibration (the chaos parameters fitted "
         "on the station's quiet periods) and a later run of the same cell "
         "uses it again as long as the data, the catalog, the station, the "
         "span and the calibration settings are the same; this button makes "
         "it afresh all the same.")));
  runOne_->setText(tr("Run the selected cell"));
  // Wide enough for either of its two labels (see showSelection).
  runOne_->setMinimumWidth(
      std::max({runOne_->fontMetrics().horizontalAdvance(
                    tr("Run the selected cell")),
                runOne_->fontMetrics().horizontalAdvance(
                    tr("Take out of the queue")),
                runOne_->fontMetrics().horizontalAdvance(tr("Stop this run")),
                runOne_->fontMetrics().horizontalAdvance(
                    tr("Run the %n selected cell(s)", nullptr, 99))}) +
      40);
  filter_->setPlaceholderText(tr("show rows with… (case or station)"));
  ui::setHelp(filter_,
              tr("Shows only the rows whose case name or station contains "
                 "this text. The buttons then act on the rows shown; runs "
                 "of hidden rows go on."));
  {
    const int was = std::max(0, show_->currentIndex());
    show_->clear();
    show_->addItem(tr("All rows"));
    show_->addItem(tr("Rows with a failed or waiting cell"));
    show_->addItem(tr("Rows with a cell that is not done"));
    show_->addItem(tr("Rows that are running or queued"));
    show_->setCurrentIndex(was);
  }
  ui::setHelp(show_, tr("Which rows are shown: all of them, or only those "
                        "that need attention. \"Not done\" is missing, "
                        "stale, failed or waiting."));
  disk_->setText(tr("Study on disk…"));
  disk_->setToolTip(ui::helpText(
      tr("Shows what the study's results take on disk, per case and "
         "recipe, and what is left over (working folders of runs that "
         "crashed, files of failed runs, results of removed cases and "
         "recipes), with a way to remove the leftovers.")));
  tabs_->setTabText(0, tr("Recipe"));
  tabs_->setTabText(1, tr("Activity"));
  tabs_->setTabToolTip(
      1, ui::helpText(tr("What ran, when, for how long and how it ended, "
                         "read from the run folders: also what ran while "
                         "you were away. A stopped run leaves nothing and is "
                         "not listed.")));
  activity_->setHorizontalHeaderLabels({tr("Ended"), tr("Case"), tr("Station"),
                                        tr("Recipe"), tr("Outcome"),
                                        tr("Took"), tr("Note")});
  activity_->setToolTip(ui::helpText(
      tr("Latest first; click a heading to sort. A double click selects "
         "the run's cell in the table above.")));
  zone_->setText(RunWords::zoneNote());
  new_->setText(tr("New recipe  ▾"));
  edit_->setText(tr("Edit recipe…"));
  remove_->setText(tr("Remove recipe"));
  showLog_->setText(tr("Show log"));
  openFolder_->setText(tr("Open folder"));
  hint_->setText(tr("A recipe is one tool's saved settings; the same recipe "
                    "on every case is the same analysis."));
  run_->setToolTip(ui::helpText(
      tr("Queues every cell that is missing or stale. A few run at a time "
         "(the number is on the Settings page), the others show Queued; "
         "it goes on to the cells they unblock (a classification after its "
         "features).")));
  stop_->setToolTip(ui::helpText(
      tr("Stops the runs this page started and empties the queue: the "
         "waiting cells are what they were before. A stopped run leaves "
         "nothing behind; what was finished before stays as it was.")));
  runOne_->setToolTip(ui::helpText(
      tr("Runs the recipe of the selected cell on its case and station. A "
         "cell that is already done is run again only after you confirm; "
         "its results are replaced when the new run has succeeded. On a "
         "cell that is running the button stops that one run, on a queued "
         "one it takes it out of the queue; the other runs go on. With "
         "several cells selected (Ctrl or Shift and a click) it runs them "
         "all. A right click on a cell, a row or a heading has more.")));
  new_->setToolTip(ui::helpText(
      tr("Adds a recipe: one tool's settings under a name, run on every "
         "case. Indicators run once per case; Features and Classification "
         "once per station. A classification needs a features recipe.")));
  edit_->setToolTip(ui::helpText(
      tr("Changes the settings of the selected column's recipe. A changed "
         "setting raises the revision and marks its finished runs stale; "
         "you are asked first when there are any.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected column's recipe out of the study. The result "
         "folders stay on disk.")));
  showLog_->setToolTip(ui::helpText(
      tr("Shows what the selected cell's last run wrote (run.log in its "
         "folder), also for a run that failed.")));
  openFolder_->setToolTip(ui::helpText(
      tr("Opens the selected cell's result folder in the file manager.")));
  guideGo_->setToolTip(
      ui::helpText(tr("Opens the page where the next step is done.")));
  recipeTable_->setHorizontalHeaderLabels(
      {tr("Setting"), tr("Value"), tr("Across cases")});
}

QString RunsPage::key(const QString &caseId, const QString &station,
                      const QString &recipeId) {
  return caseId + QLatin1Char('|') + station + QLatin1Char('|') + recipeId;
}

QString RunsPage::keyOf(int row, const Recipe &r) const {
  return key(rows_[row].caseId,
             study::perStation(r.tool) ? rows_[row].station : QString(), r.id);
}

bool RunsPage::busy(const QString &k) const {
  if (running_.contains(k))
    return true;
  const auto it = working_.constFind(k);
  return it != working_.constEnd() && it.value()->load();
}

const Cell *RunsPage::cellAt(int row, int column) const {
  if (row < 0 || row >= cells_.size() || column < 0 ||
      column >= cells_[row].size())
    return nullptr;
  return &cells_[row][column];
}

void RunsPage::reload() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  rows_.clear();
  cells_.clear();
  if (!open) {
    // The study was closed: what waited for it does not wait any more.
    queue_.clear();
    runAll_ = false;
    reportQueue(0);
    syncLive();
    return;
  }
  const Study &s = ctx_.study->study();
  const QString needle = filter_->text().trimmed();
  const int mode = show_->currentIndex();
  const QStringList places = queued();
  int all = 0;
  for (const Case &c : s.cases) {
    QList<Row> rows;
    if (c.stations.isEmpty())
      rows << Row{c.id, QString()};
    for (const study::StationRef &st : c.stations)
      rows << Row{c.id, st.code};
    for (const Row &row : std::as_const(rows)) {
      ++all;
      if (!needle.isEmpty() && !c.name.contains(needle, Qt::CaseInsensitive) &&
          !row.station.contains(needle, Qt::CaseInsensitive))
        continue;
      QList<Cell> line;
      bool attention = false, notDone = false, going = false;
      for (const Recipe &r : s.recipes) {
        const Cell cell = study::cellOf(s, c, row.station, r);
        attention = attention || cell.state == CellState::Failed ||
                    cell.state == CellState::Blocked;
        notDone = notDone || (cell.state != CellState::Done &&
                              cell.state != CellState::NotApplicable);
        const QString k = key(
            c.id, study::perStation(r.tool) ? row.station : QString(), r.id);
        going = going || busy(k) || places.contains(k);
        line << cell;
      }
      if ((mode == 1 && !attention) || (mode == 2 && !notDone) ||
          (mode == 3 && !going))
        continue;
      rows_ << row;
      cells_ << line;
    }
  }
  shown_->setText(all != rows_.size()
                      ? tr("%1 of %2 rows shown").arg(rows_.size()).arg(all)
                      : QString());
  render();
  fillActivity();
}

void RunsPage::fillActivity() {
  if (!ctx_.study->isOpen())
    return;
  // Sorts by the number it holds (a duration), not by its text.
  class Number : public QTableWidgetItem {
  public:
    Number(const QString &text, qint64 value) : QTableWidgetItem(text) {
      setData(Qt::UserRole + 1, value);
    }
    bool operator<(const QTableWidgetItem &o) const override {
      return data(Qt::UserRole + 1).toLongLong() <
             o.data(Qt::UserRole + 1).toLongLong();
    }
  };
  const QList<study::Activity> list = study::activity(ctx_.study->study());
  const int column = activity_->horizontalHeader()->sortIndicatorSection();
  const Qt::SortOrder order = activity_->horizontalHeader()->sortIndicatorOrder();
  const bool chosen = activity_->property("sorted").toBool();
  activity_->setSortingEnabled(false);
  activity_->setRowCount(int(list.size()));
  for (int i = 0; i < int(list.size()); ++i) {
    const study::Activity &a = list[i];
    const QString k = key(a.caseId, a.station, a.recipeId);
    const QString outcome =
        a.ok ? (a.warnings.isEmpty() ? tr("✓  Done") : tr("✓  Done  ⚠"))
             : tr("✕  Failed");
    const QString note =
        a.ok ? a.warnings.join(QStringLiteral(" · ")) : a.error;
    const QStringList cols{RunWords::moment(a.finished), a.caseName,
                           a.station.isEmpty() ? QStringLiteral("–") : a.station,
                           a.recipeName, outcome};
    for (int j = 0; j < int(cols.size()); ++j) {
      auto *item = new QTableWidgetItem(cols[j]);
      item->setData(Qt::UserRole, k);
      item->setToolTip(QDir::toNativeSeparators(a.dir));
      activity_->setItem(i, j, item);
    }
    auto *took = new Number(RunWords::duration(a.seconds()), a.seconds());
    took->setData(Qt::UserRole, k);
    took->setToolTip(
        tr("started %1, ended %2")
            .arg(RunWords::moment(a.started), RunWords::moment(a.finished)));
    activity_->setItem(i, 5, took);
    auto *why = new QTableWidgetItem(note.simplified());
    why->setData(Qt::UserRole, k);
    why->setToolTip(note.isEmpty() ? QString() : ui::helpText(note));
    activity_->setItem(i, 6, why);
  }
  activity_->setSortingEnabled(true);
  // Latest first until the reader sorts by something else.
  if (chosen)
    activity_->sortByColumn(column, order);
  else
    activity_->sortByColumn(0, Qt::DescendingOrder);
  tabs_->setTabText(1, list.isEmpty() ? tr("Activity")
                                      : tr("Activity (%1)").arg(list.size()));
}

void RunsPage::render(bool selection) {
  if (!ctx_.study->isOpen())
    return;
  // Jobs that have ended leave the running set.
  for (auto it = running_.begin(); it != running_.end();) {
    const Job *job = it.value();
    if (!job || (job->state() != Job::State::Running &&
                 job->state() != Job::State::Queued))
      it = running_.erase(it);
    else
      ++it;
  }
  for (auto it = working_.begin(); it != working_.end();)
    it = it.value()->load() ? std::next(it) : working_.erase(it);

  const Study &s = ctx_.study->study();
  QStringList labels;
  for (const Row &row : rows_) {
    const Case *c = s.findCase(row.caseId);
    const QString name = c ? c->name : row.caseId;
    labels << (row.station.isEmpty()
                   ? name
                   : name + QStringLiteral(" · ") + row.station);
  }
  QList<QPair<QString, QString>> columns;
  for (const Recipe &r : s.recipes)
    columns << qMakePair(r.name, r.tool);
  QList<QList<MatrixCell>> cells;
  const QStringList places = queued();
  int waiting = 0, done = 0, waitingOnCells = 0;
  for (int i = 0; i < int(rows_.size()); ++i) {
    QList<MatrixCell> line;
    for (int j = 0; j < int(s.recipes.size()); ++j) {
      MatrixCell m;
      if (const Cell *cell = cellAt(i, j)) {
        m.state = cell->state;
        m.why = cell->why;
        waiting += cell->state == CellState::Blocked ? 1 : 0;
        done += cell->state == CellState::Done ? 1 : 0;
      }
      const QString k = keyOf(i, s.recipes[j]);
      if (const Job *job = running_.value(k)) {
        m.running = true;
        m.fraction = job->progress().fraction;
        m.doing = job->progress().text;
        m.elapsedMs = job->elapsedMs();
      } else if (const int at = int(places.indexOf(k)); at >= 0) {
        m.queued = true;
        m.place = at + 1;
      } else if (const Cell *cell = cellAt(i, j);
                 runAll_ && cell && cell->state == CellState::Blocked &&
                 cell->fix == study::Fix::RunFirst) {
        // In a batch, a classification follows its features by itself: it
        // is in the queue too, behind the cell it stands on.
        const Recipe *f = s.findRecipe(
            s.recipes[j].settings.value(kFeaturesKey).toString());
        const QString up =
            f ? key(rows_[i].caseId, rows_[i].station, f->id) : QString();
        if (f && (places.contains(up) || busy(up))) {
          m.queued = true;
          m.waitsFor = rows_[i].station.isEmpty()
                           ? f->name
                           : f->name + QStringLiteral(" · ") + rows_[i].station;
        }
      }
      if (const Cell *cell = cellAt(i, j))
        m.warnings = cell->warnings;
      waitingOnCells += m.queued && !m.waitsFor.isEmpty() ? 1 : 0;
      line << m;
    }
    cells << line;
  }
  matrix_->setContents(labels, columns, cells);
  const int todo = int(runnable().size());
  const QString locked = lockProblem();
  run_->setText(tr("Run missing and stale (%1)").arg(todo));
  run_->setEnabled(todo > 0 && locked.isEmpty());
  stop_->setEnabled(runAll_ || !running_.isEmpty() || !queue_.isEmpty());

  // The next step, in words, with the page that it is done on.
  QString guide, page, go;
  const int inQueue = int(queue_.size()) + waitingOnCells;
  // The shell counts what waits here (status strip, the quit question).
  reportQueue(inQueue);
  if (!locked.isEmpty()) {
    // Before anything else: why no run can be started here, and where the
    // study's folder is looked at.
    guide = locked;
    page = QStringLiteral("cases");
    go = tr("Go to Cases");
  } else if (!running_.isEmpty() || inQueue > 0) {
    // What is going on: how many run, how many wait their turn.
    guide = tr("%1 running, %2 queued (%3 at a time; the number is on the "
               "Settings page).")
                .arg(running_.size())
                .arg(inQueue)
                .arg(atOnce());
    if (batchStarted_.isValid() && batchAsked_ > 0) {
      // How the batch is doing; a time left only once a run of it has ended,
      // and as an estimate from those.
      guide += QLatin1Char(' ') +
               tr("Started %1: %2 of %3 done")
                   .arg(RunWords::clock(batchStarted_))
                   .arg(batchDone_)
                   .arg(std::max(batchAsked_, batchDone_));
      const int left = int(running_.size()) + inQueue;
      if (batchDone_ > 0 && left > 0)
        guide += tr(", about %1 left")
                     .arg(RunWords::duration(batchMs_ / batchDone_ / 1000 *
                                            left / atOnce()));
      guide += QLatin1Char('.');
    }
    if (!spaceNote_.isEmpty())
      guide += QLatin1Char(' ') + spaceNote_;
  } else if (!spaceNote_.isEmpty()) {
    guide = spaceNote_;
  } else if (s.cases.isEmpty()) {
    guide = tr("This study has no cases yet. A case is an earthquake (or a "
               "reference date) and the stations it is studied from.");
    page = QStringLiteral("cases");
    go = tr("Add cases…");
  } else if (s.recipes.isEmpty()) {
    guide = tr("No recipes yet. Use New recipe to save one tool's settings; "
               "the recipe is then run on every case.");
  } else if (todo == 0 && running_.isEmpty() && done > 0 && waiting == 0) {
    guide = tr("Every cell has a finished run.");
    page = QStringLiteral("compare");
    go = tr("Compare the results");
  } else if (todo == 0 && running_.isEmpty() && waiting > 0) {
    guide = tr("%n cell(s) are waiting for something. Select one to see "
               "what it needs and where that is set.",
               nullptr, waiting);
  }
  guide_->setText(guide);
  guide_->setVisible(!guide.isEmpty());
  guideGo_->setText(go);
  guideGo_->setProperty("page", page);
  guideGo_->setVisible(!go.isEmpty() && ctx_.nav != nullptr);
  syncLive();
  if (selection)
    showSelection();
}

QString RunsPage::dirOf(const QString &k) const {
  if (!ctx_.study->isOpen())
    return {};
  const Study &s = ctx_.study->study();
  const QStringList parts = k.split(QLatin1Char('|'));
  if (parts.size() != 3)
    return {};
  const Case *c = s.findCase(parts[0]);
  const Recipe *r = s.findRecipe(parts[2]);
  return c && r ? study::runDir(s, *c, parts[1], *r) : QString();
}

void RunsPage::syncLive() {
  // The folders of the cells that are running, winding down or queued: not
  // to be taken for leftovers by "Study on disk".
  QSet<QString> now;
  const auto take = [this, &now](const QString &k) {
    const QString dir = dirOf(k);
    if (!dir.isEmpty())
      now.insert(dir);
  };
  for (auto it = running_.constBegin(); it != running_.constEnd(); ++it)
    take(it.key());
  for (auto it = working_.constBegin(); it != working_.constEnd(); ++it)
    take(it.key());
  for (const Queued &q : std::as_const(queue_))
    take(q.key);
  for (const QString &dir : std::as_const(now))
    if (!live_.contains(dir))
      study::LiveRuns::enter(dir);
  for (const QString &dir : std::as_const(live_))
    if (!now.contains(dir))
      study::LiveRuns::leave(dir);
  live_ = now;
}

QList<QPair<RunsPage::Row, QString>> RunsPage::runnable() const {
  QList<QPair<Row, QString>> out;
  if (!ctx_.study->isOpen())
    return out;
  const Study &s = ctx_.study->study();
  const QStringList places = queued();
  QSet<QString> seen; // a per-case tool appears on every station row once
  for (int i = 0; i < int(rows_.size()); ++i)
    for (int j = 0; j < int(s.recipes.size()); ++j) {
      const Cell *cell = cellAt(i, j);
      if (!cell)
        continue;
      const QString k = keyOf(i, s.recipes[j]);
      if (seen.contains(k) || busy(k) || places.contains(k))
        continue;
      seen.insert(k);
      if (cell->state == CellState::Missing ||
          cell->state == CellState::Stale || cell->state == CellState::Failed)
        out << qMakePair(rows_[i], s.recipes[j].id);
    }
  return out;
}

void RunsPage::showSelection() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn(), row = matrix_->selectedRow();
  const Recipe *r =
      col >= 0 && col < s.recipes.size() ? &s.recipes[col] : nullptr;
  edit_->setEnabled(r != nullptr);
  remove_->setEnabled(r != nullptr);
  QString info, dir, log, fixText, fixHelp;
  study::Fix fix = study::Fix::None;
  bool canRun = false, isQueued = false, isRunning = false;
  const Cell *cell = r ? cellAt(row, col) : nullptr;
  if (cell) {
    const QString k = keyOf(row, *r);
    MatrixCell m;
    m.state = cell->state;
    m.running = running_.contains(k);
    if (const Job *job = running_.value(k)) {
      m.fraction = job->progress().fraction;
      m.doing = job->progress().text;
    }
    const int at = int(queued().indexOf(k));
    m.queued = !m.running && at >= 0;
    m.place = at + 1;
    isQueued = m.queued;
    info = RunMatrix::stateText(m).trimmed();
    if (m.queued)
      info = tr("⋯  Queued, number %1 of %2: it starts when a run before it "
                "has ended (%3 run at a time)")
                 .arg(m.place)
                 .arg(queue_.size())
                 .arg(atOnce());
    else if (m.running && !m.doing.isEmpty())
      info += QStringLiteral(" — ") + m.doing;
    else if (!m.running && !cell->why.isEmpty())
      info += QStringLiteral(" — ") + cell->why;
    for (const QString &w : cell->warnings)
      info += QStringLiteral("\n⚠  ") + w;
    if (m.running && running_.value(k))
      info += QStringLiteral(" · ") +
              tr("running for %1")
                  .arg(RunWords::duration(running_.value(k)->elapsedMs() / 1000));
    if (cell->record && cell->record->finished.isValid()) {
      // When the finished run ended and how long it took (local time; the
      // zone is said under the activity list).
      const qint64 took = cell->record->started.isValid()
                              ? cell->record->started.secsTo(
                                    cell->record->finished)
                              : -1;
      info += QStringLiteral(" · ") +
              (took >= 0 ? tr("last run ended %1, took %2")
                               .arg(RunWords::moment(cell->record->finished),
                                    RunWords::duration(took))
                         : tr("last run ended %1")
                               .arg(RunWords::moment(cell->record->finished)));
    }
    if (!cell->note.isEmpty())
      info += QStringLiteral(" · ⚠ ") + cell->note;
    if (!cell->dir.isEmpty() && QDir(cell->dir).exists())
      dir = cell->dir;
    else if (!cell->dir.isEmpty() &&
             QDir(study::runFailedDir(cell->dir)).exists())
      dir = study::runFailedDir(cell->dir); // what the failed run left
    log = cell->log;
    if (m.running) // a running cell writes its log in its working folder
      log = QDir(study::runTmpDir(cell->dir)).filePath(study::kRunLogName);
    // Where a waiting cell is put right; also a failed one with a known
    // remedy (too little data: Cases; the catalog's events: the recipe).
    if (!m.running && !m.queued &&
        (cell->state == CellState::Blocked || cell->state == CellState::Failed))
      fix = cell->fix;
    isRunning = m.running;
    canRun = m.queued || m.running ||
             (cell->state != CellState::Blocked &&
              cell->state != CellState::NotApplicable && !busy(k));
  } else if (s.recipes.isEmpty()) {
    info = tr("No recipes yet. Use New recipe to add one.");
  } else if (rows_.isEmpty()) {
    info = tr("No cases yet: the rows of this table are the study's cases.");
  } else {
    info = tr("Select a cell to see why it is waiting, what its last run "
              "said, or where its files are. The arrow keys move, Enter "
              "runs.");
  }
  switch (fix) {
  case study::Fix::Cases:
    fixText = tr("Go to Cases");
    fixHelp = tr("Opens the Cases page, where the catalog, the stations and "
                 "the waveform folder are set.");
    break;
  case study::Fix::Recipe:
    fixText = tr("Edit recipe…");
    fixHelp = tr("Opens this recipe's settings, where what it waits for is "
                 "chosen.");
    break;
  case study::Fix::RunFirst:
    fixText = tr("Select the cell it needs");
    fixHelp = tr("Selects the features cell this classification waits for, "
                 "so that it can be run or looked at.");
    break;
  case study::Fix::None:
    break;
  }
  if (!lockProblem().isEmpty()) // looking is allowed, running is not
    canRun = isQueued || isRunning;
  const int many = int(matrix_->selectedCells().size());
  cellInfo_->setText(
      many > 1 ? tr("%n cell(s) selected. Run them with the button, or "
                    "right-click for more (stop, open the folders).",
                    nullptr, many)
               : info);
  runOne_->setEnabled(canRun || (many > 1 && lockProblem().isEmpty()));
  runOne_->setText(many > 1    ? tr("Run the %n selected cell(s)", nullptr, many)
                   : isRunning ? tr("Stop this run")
                   : isQueued  ? tr("Take out of the queue")
                               : tr("Run the selected cell"));
  // A features cell that has a calibration kept: it can be made anew.
  const bool hasKept =
      cell && r && r->tool == study::kToolFeatures && !cell->dir.isEmpty() &&
      !QDir(study::runCalibrationDir(cell->dir))
           .entryList({QStringLiteral("*.json")}, QDir::Files)
           .isEmpty();
  recal_->setVisible(hasKept);
  recal_->setEnabled(hasKept && canRun && !isQueued && !isRunning);
  fix_->setText(fixText);
  fix_->setToolTip(fixHelp.isEmpty() ? QString() : ui::helpText(fixHelp));
  fix_->setProperty("fix", int(fix));
  fix_->setVisible(fix != study::Fix::None &&
                   (fix != study::Fix::Cases || ctx_.nav != nullptr));
  showLog_->setEnabled(!log.isEmpty());
  showLog_->setProperty("log", log);
  openFolder_->setEnabled(!dir.isEmpty());
  openFolder_->setProperty("dir", dir);

  if (!r) {
    recipeTitle_->setText(tr("RECIPE"));
    recipeTable_->setRowCount(0);
    return;
  }
  recipeTitle_->setText(tr("RECIPE · %1 · %2 · REVISION %3")
                            .arg(toolName(r->tool).toUpper(), r->name.toUpper())
                            .arg(r->revision));
  QList<QStringList> lines;
  for (auto it = r->settings.begin(); it != r->settings.end(); ++it) {
    QString value = valueText(it.value());
    if (r->tool == study::kToolClassification && it.key() == kFeaturesKey) {
      // The recipe by its name, not its id.
      const Recipe *f = s.findRecipe(it.value().toString());
      value = f ? f->name
                : it.value().toString().isEmpty()
                      ? tr("⚠  none chosen")
                      : tr("⚠  %1 (no longer in the study)")
                            .arg(it.value().toString());
    }
    if (r->tool == study::kToolFeatures && it.key() == kConfigKey &&
        it.value().toString().trimmed().isEmpty())
      value = tr("the defaults");
    lines << QStringList{it.key(), value,
                         fittedPerCase(*r, it.key()) ? tr("fitted per case")
                                                     : tr("fixed")};
  }
  if (r->tool == study::kToolFeatures) {
    // Said outright: which catalog the calibration reads.
    const QString own = study::calibrationOverride(*r);
    lines << QStringList{
        QStringLiteral("calibration catalog"),
        own.isEmpty() ? tr("the study's")
                      : tr("⚠  the configuration's own: %1")
                            .arg(QDir::toNativeSeparators(own)),
        tr("fixed")};
  }
  for (const QString &f : r->fitted)
    if (!r->settings.contains(f))
      lines << QStringList{f, QStringLiteral("–"), tr("fitted per case")};
  recipeTable_->setRowCount(int(lines.size()));
  for (int i = 0; i < int(lines.size()); ++i) {
    // Readable name in the first column; the key and what the setting does
    // on hover.
    const SettingInfo help = settingInfo(r->tool, lines[i][0]);
    for (int j = 0; j < 3; ++j) {
      auto *item = new QTableWidgetItem(j == 0 ? help.label : lines[i][j]);
      item->setData(Qt::UserRole, lines[i][0]);
      item->setToolTip(help.tooltip(lines[i][0]));
      recipeTable_->setItem(i, j, item);
    }
  }
  recipeTable_->setColumnWidth(0, 200);
  recipeTable_->setColumnWidth(1, 430);
}

void RunsPage::newRecipe(const QString &tool) {
  if (!ctx_.study->isOpen())
    return;
  const auto firstFeatures = [this]() -> QString {
    for (const Recipe &o : ctx_.study->study().recipes)
      if (o.tool == study::kToolFeatures)
        return o.id;
    return {};
  };
  QString features = firstFeatures();
  if (tool == study::kToolClassification && features.isEmpty() &&
      confirm(tr("New recipe"),
              tr("A classification is trained on the feature table of a "
                 "features recipe, and this study has none yet.\n\nAdd a "
                 "features recipe with the default settings first? (It can "
                 "be edited afterwards.)"))) {
    QString error;
    if (!ctx_.study->update(
            [](Study &st) {
              Recipe f = study::defaultRecipe(study::kToolFeatures);
              f.id = st.freeRecipeId(f.name);
              st.recipes << f;
            },
            &error)) {
      QMessageBox::warning(this, tr("Recipe"), error);
      return;
    }
    features = firstFeatures();
  }
  const Study &s = ctx_.study->study();
  Recipe r = study::defaultRecipe(tool);
  if (tool == study::kToolClassification)
    r.settings.insert(kFeaturesKey, features);
  RecipeDialog dialog(s, r, this);
  dialog.settings = ctx_.settings;
  if (dialog.exec() != QDialog::Accepted)
    return;
  Recipe made = dialog.recipe();
  made.revision = 1;
  QString error;
  if (!ctx_.study->update(
          [&made](Study &st) {
            made.id = st.freeRecipeId(made.name);
            st.recipes << made;
          },
          &error))
    QMessageBox::warning(this, tr("Recipe"), error);
  else
    matrix_->select(-1, int(ctx_.study->study().recipes.size()) - 1);
}

void RunsPage::editRecipe() {
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn();
  if (col < 0 || col >= s.recipes.size())
    return;
  const Recipe original = s.recipes[col];
  Recipe shown = original;
  for (;;) {
    RecipeDialog dialog(s, shown, this);
    dialog.settings = ctx_.settings;
    if (dialog.exec() != QDialog::Accepted)
      return;
    Recipe edited = dialog.recipe();
    edited.revision = edited.settings != original.settings
                          ? original.revision + 1
                          : original.revision;
    const auto apply = [&edited](Study &st) {
      for (Recipe &r : st.recipes)
        if (r.id == edited.id)
          r = edited;
    };
    // A changed setting makes finished runs stale: say how many, and ask.
    // A no goes back to the dialog with what was typed.
    if (edited.revision != original.revision) {
      Study after = s;
      apply(after);
      const int stale = study::runsMadeStale(s, after);
      if (stale > 0 &&
          !confirm(tr("Edit recipe"),
                   tr("Changing a setting of \"%1\" makes %n finished "
                      "run(s) stale (its own, and those that build on "
                      "them). They stay on disk until they are run "
                      "again.\n\nSave the change?",
                      nullptr, stale)
                       .arg(edited.name))) {
        shown = edited;
        shown.revision = original.revision;
        continue;
      }
    }
    QString error;
    if (!ctx_.study->update(apply, &error))
      QMessageBox::warning(this, tr("Recipe"), error);
    return;
  }
}

void RunsPage::removeRecipe() {
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn();
  if (col < 0 || col >= s.recipes.size())
    return;
  const QString id = s.recipes[col].id;
  // The recipes that take their table from this one are named.
  QStringList dependants;
  for (const Recipe &r : s.recipes)
    if (r.tool == study::kToolClassification &&
        r.settings.value(kFeaturesKey).toString() == id)
      dependants << r.name;
  QString text =
      tr("Remove the recipe \"%1\"? The folders of its runs stay on disk.")
          .arg(s.recipes[col].name);
  if (!dependants.isEmpty())
    text += QStringLiteral("\n\n") +
            tr("%n recipe(s) take their feature table from it and will wait "
               "until they are given another features recipe: %1.",
               nullptr, int(dependants.size()))
                .arg(dependants.join(QStringLiteral(", ")));
  if (!confirm(tr("Remove recipe"), text))
    return;
  QString error;
  if (!ctx_.study->update(
          [&id](Study &st) {
            st.recipes.removeIf([&id](const Recipe &r) { return r.id == id; });
          },
          &error))
    QMessageBox::warning(this, tr("Recipe"), error);
}

bool RunsPage::start(const Row &row, const Recipe &recipe, bool fresh) {
  const Study study = ctx_.study->study(); // a snapshot for the worker
  const Case *found = study.findCase(row.caseId);
  if (!found)
    return false;
  const Case c = *found;
  const QString station =
      study::perStation(recipe.tool) ? row.station : QString();
  const QString k = key(c.id, station, recipe.id);
  if (busy(k))
    return false;
  const QString version = QCoreApplication::applicationVersion();
  const QString name =
      station.isEmpty()
          ? QStringLiteral("%1 · %2").arg(recipe.name, c.name)
          : QStringLiteral("%1 · %2 · %3").arg(recipe.name, c.name, station);
  // Stays set until the worker has left the run, which after a cancel is
  // later than the job's state says.
  const auto working = std::make_shared<std::atomic<bool>>(true);
  auto *job = new TaskJob(
      name, recipe.tool,
      station.isEmpty()
          ? tr("recipe \"%1\" on %2").arg(recipe.name, c.name)
          : tr("recipe \"%1\" on %2, station %3")
                .arg(recipe.name, c.name, station),
      [study, c, station, recipe, version, working,
       fresh](TaskContext &t) -> int {
        struct Leave {
          std::shared_ptr<std::atomic<bool>> flag;
          ~Leave() { flag->store(false); }
        } leave{working};
        study::RunHooks hooks;
        hooks.log = [&t](const QString &l) { t.log(l); };
        hooks.shouldStop = [&t] { return t.cancelled(); };
        hooks.progress = [&t](double f, const QString &what) {
          t.progress(f, what);
        };
        hooks.freshCalibration = fresh;
        try {
          return study::runCell(study, c, station, recipe, version, hooks) ? 0
                                                                           : 1;
        } catch (const std::exception &e) {
          t.log(QStringLiteral("error: %1").arg(QString::fromUtf8(e.what())));
          return 1;
        }
      });
  attempted_.insert(k);
  working_.insert(k, working);
  Job *added = ctx_.jobs->add(job);
  running_.insert(k, added);
  connect(added, &Job::stateChanged, this, [this, added, k] {
    if (added->state() == Job::State::Running ||
        added->state() == Job::State::Queued)
      return;
    // Cancelling a run in the Jobs panel, or with Stop, ends the batch: the
    // next cell is not started behind the user's back. A run stopped by
    // itself ("Stop this run") leaves the others and the queue alone.
    if (added->state() == Job::State::Cancelled) {
      if (!stoppingOne_.remove(k)) {
        runAll_ = false;
        queue_.clear();
        reportQueue(0); // said at once, not at the next redraw
      }
    } else {
      stoppingOne_.remove(k); // it ended by itself before the stop reached it
      if (batchStarted_.isValid()) {
        ++batchDone_;
        batchMs_ += added->elapsedMs();
      }
    }
    // After the job has settled: show the new state, start what is next.
    QTimer::singleShot(0, this, [this] {
      reload();
      pump();
    });
  });
  tick_->start();
  return true;
}

void RunsPage::runAll() {
  if (!ctx_.study->isOpen())
    return;
  if (!lockProblem().isEmpty()) {
    render(); // the guide line says why
    return;
  }
  // Room for what the first runs unpack is looked at before anything starts.
  QList<Want> wants;
  for (const auto &item : runnable())
    wants << Want{item.first, item.second};
  if (!spaceAllows(wants))
    return;
  attempted_.clear();
  runAll_ = true;
  batchStarted_ = QDateTime::currentDateTimeUtc();
  batchDone_ = batchAsked_ = 0;
  batchMs_ = 0;
  pump();
  render();
}

void RunsPage::runSelected(bool fresh) {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const QList<QPair<int, int>> chosen = matrix_->selectedCells();
  if (chosen.size() > 1) {
    runCells(chosen, fresh);
    return;
  }
  const int col = matrix_->selectedColumn(), row = matrix_->selectedRow();
  const Cell *cell = cellAt(row, col);
  if (!cell || col >= s.recipes.size())
    return;
  const Recipe &r = s.recipes[col];
  // A queued cell: the button takes it out again, and the cell is what it
  // was before. A running one: the button stops that run, and no other.
  const QString k = keyOf(row, r);
  if (queued().contains(k) || running_.contains(k)) {
    stopKeys({k});
    return;
  }
  request({Want{rows_[row], r.id}}, fresh, false);
}

void RunsPage::runCells(const QList<QPair<int, int>> &cells, bool fresh) {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  QList<Want> wants;
  for (const auto &cell : cells)
    if (cell.first >= 0 && cell.first < rows_.size() && cell.second >= 0 &&
        cell.second < s.recipes.size())
      wants << Want{rows_[cell.first], s.recipes[cell.second].id};
  request(wants, fresh, cells.size() > 1);
}

void RunsPage::runRow(int row) {
  if (!ctx_.study->isOpen() || row < 0 || row >= rows_.size())
    return;
  QList<QPair<int, int>> cells;
  for (int j = 0; j < int(ctx_.study->study().recipes.size()); ++j)
    cells << qMakePair(row, j);
  runCells(cells);
}

void RunsPage::runCase(int row) {
  if (!ctx_.study->isOpen() || row < 0 || row >= rows_.size())
    return;
  QList<QPair<int, int>> cells;
  for (int i = 0; i < int(rows_.size()); ++i)
    if (rows_[i].caseId == rows_[row].caseId)
      for (int j = 0; j < int(ctx_.study->study().recipes.size()); ++j)
        cells << qMakePair(i, j);
  runCells(cells);
}

void RunsPage::runColumn(int column) {
  if (!ctx_.study->isOpen() || column < 0 ||
      column >= ctx_.study->study().recipes.size())
    return;
  QList<QPair<int, int>> cells;
  for (int i = 0; i < int(rows_.size()); ++i)
    cells << qMakePair(i, column);
  runCells(cells);
}

void RunsPage::request(const QList<Want> &asked, bool fresh, bool bulk) {
  if (!ctx_.study->isOpen())
    return;
  if (!lockProblem().isEmpty()) {
    render(); // the guide line says why
    return;
  }
  const Study &s = ctx_.study->study();
  // What of it can run: not a cell that waits for something, is running or
  // queued already, or does not apply; a per-case tool once.
  QList<Want> wants;
  QSet<QString> seen;
  int again = 0;
  QString oneName, oneCase;
  const QStringList places = queued();
  for (const Want &w : asked) {
    const Recipe *r = s.findRecipe(w.recipeId);
    const Case *c = s.findCase(w.row.caseId);
    if (!r || !c)
      continue;
    const QString k = key(
        c->id, study::perStation(r->tool) ? w.row.station : QString(), r->id);
    if (seen.contains(k) || busy(k) || places.contains(k))
      continue;
    const Cell cell = study::cellOf(s, *c, w.row.station, *r);
    if (cell.state == CellState::Blocked ||
        cell.state == CellState::NotApplicable)
      continue;
    seen.insert(k);
    wants << w;
    if (cell.state == CellState::Done) {
      ++again;
      oneName = r->name;
      oneCase = c->name;
    }
  }
  if (wants.isEmpty()) {
    if (bulk)
      spaceNote_ = tr("None of those cells can be run now: they are running, "
                      "queued, or waiting for something.");
    render();
    return;
  }
  spaceNote_.clear();
  if (!bulk && again == 1) {
    if (!confirm(tr("Run again"),
                 tr("\"%1\" already has a finished run on %2. Run it "
                    "again?\n\nThe finished results stay as they are until "
                    "the new run has succeeded; a run that is stopped or "
                    "fails leaves them untouched.")
                     .arg(oneName, oneCase)))
      return;
  } else if (bulk) {
    // A group is confirmed with its count, and with what it will take.
    QList<qint64> needs;
    for (const Want &w : std::as_const(wants)) {
      const Recipe *r = s.findRecipe(w.recipeId);
      const Case *c = s.findCase(w.row.caseId);
      needs << study::tempSpaceFor(s, *c, w.row.station, *r).bytes;
    }
    const qint64 peak = study::peakNeed(needs, atOnce());
    QString text = tr("Run %n cell(s)?", nullptr, int(wants.size()));
    if (again > 0)
      text += QLatin1Char(' ') +
              tr("%n of them already have a finished run and are run again; "
                 "their results stay as they are until the new run has "
                 "succeeded.",
                 nullptr, again);
    text += QStringLiteral("\n\n") +
            tr("%1 run at a time; the others wait in the queue.").arg(atOnce());
    if (peak > 0)
      text += QLatin1Char(' ') +
              tr("While they run they need about %1 of temporary space for "
                 "unpacked archives.")
                  .arg(RunWords::bytes(peak));
    if (!confirm(tr("Run cells"), text))
      return;
  }
  if (!spaceAllows(wants))
    return;
  // Through the same queue as a batch: a cell asked for while every slot
  // is taken waits its turn instead of running beside the others.
  for (const Want &w : std::as_const(wants))
    if (const Recipe *r = s.findRecipe(w.recipeId))
      enqueue(w.row, *r, fresh);
  pump();
  render();
}

bool RunsPage::spaceAllows(const QList<Want> &wants) {
  if (!ctx_.study->isOpen() || !freeSpace)
    return true;
  const Study &s = ctx_.study->study();
  QList<qint64> needs;
  for (const Want &w : wants) {
    const Recipe *r = s.findRecipe(w.recipeId);
    const Case *c = s.findCase(w.row.caseId);
    if (r && c)
      needs << study::tempSpaceFor(s, *c, w.row.station, *r).bytes;
  }
  const qint64 peak = study::peakNeed(needs, atOnce());
  if (peak <= 0)
    return true;
  const qint64 free = freeSpace(s.dir);
  if (free < 0)
    return true; // unknown: the run itself will say if the disk fills
  const QString figures =
      tr("needs about %1 of temporary space for unpacked archives, and %2 "
         "are free on the study's disk")
          .arg(RunWords::bytes(peak), RunWords::bytes(free));
  if (peak > free) {
    // Clearly not enough: nothing is started.
    spaceNote_ = tr("⚠  Not started: this %1. Free some space (\"Study on "
                    "disk…\" lists leftovers), or run fewer cells at a time.")
                     .arg(figures);
    render();
    return false;
  }
  if (double(peak) > 0.8 * double(free))
    return confirm(tr("Little disk space"),
                   tr("This %1. That leaves little room for the results "
                      "themselves.\n\nRun all the same?")
                       .arg(figures));
  return true;
}

void RunsPage::stopKeys(const QStringList &keys) {
  for (const QString &k : keys) {
    if (dequeue(k))
      continue;
    if (const QPointer<Job> job = running_.value(k)) {
      stoppingOne_.insert(k); // the others, and the queue, go on
      job->cancel();
    }
  }
  render();
}

void RunsPage::stopCells(const QList<QPair<int, int>> &cells) {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  QStringList keys;
  for (const auto &cell : cells)
    if (cell.first >= 0 && cell.first < rows_.size() && cell.second >= 0 &&
        cell.second < s.recipes.size())
      keys << keyOf(cell.first, s.recipes[cell.second]);
  keys.removeDuplicates();
  stopKeys(keys);
}

void RunsPage::stopBatch() {
  runAll_ = false;
  queue_.clear(); // the waiting cells are what they were before
  reportQueue(0);
  spaceNote_.clear();
  // The queue is empty now; the stopped runs' endings must not empty it
  // again, should cells be asked for before they have wound down.
  for (auto it = running_.constBegin(); it != running_.constEnd(); ++it)
    if (it.value()) {
      stoppingOne_.insert(it.key());
      it.value()->cancel();
    }
  render();
}

QString RunsPage::duplicateRecipe(const QString &asName) {
  if (!ctx_.study->isOpen())
    return {};
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn();
  if (col < 0 || col >= s.recipes.size())
    return {};
  const Recipe original = s.recipes[col];
  QString name = asName.trimmed();
  if (name.isEmpty()) {
    bool ok = false;
    name = QInputDialog::getText(
               this, tr("Duplicate recipe"),
               tr("A copy of \"%1\" with the same settings, to change one of "
                  "them in. Its name:")
                   .arg(original.name),
               QLineEdit::Normal, tr("%1 (copy)").arg(original.name), &ok)
               .trimmed();
    if (!ok || name.isEmpty())
      return {};
  }
  Recipe copy = original;
  copy.name = name;
  copy.revision = 1; // a new recipe: nothing has been run with it
  QString error, id;
  if (!ctx_.study->update(
          [&copy, &id](Study &st) {
            copy.id = st.freeRecipeId(copy.name);
            id = copy.id;
            st.recipes << copy;
          },
          &error)) {
    QMessageBox::warning(this, tr("Recipe"), error);
    return {};
  }
  matrix_->select(-1, int(ctx_.study->study().recipes.size()) - 1);
  return id;
}

QMenu *RunsPage::menuFor(int row, int column) {
  auto *menu = new QMenu(this);
  if (!ctx_.study->isOpen())
    return menu;
  const Study &s = ctx_.study->study();
  const auto add = [menu](const QString &text, const char *name,
                          std::function<void()> act, bool enabled = true) {
    QAction *a = menu->addAction(text);
    a->setObjectName(QLatin1String(name));
    a->setEnabled(enabled);
    QObject::connect(a, &QAction::triggered, menu, [act] { act(); });
    return a;
  };
  const bool onColumn = column >= 0 && column < s.recipes.size();
  const bool onRow = row >= 0 && row < rows_.size();
  if (onRow && onColumn) {
    const Recipe &r = s.recipes[column];
    const Cell *cell = cellAt(row, column);
    const QString k = keyOf(row, r);
    const bool isRunning = running_.contains(k);
    const bool isQueued = queued().contains(k);
    const bool runnable =
        cell && !busy(k) && !isQueued && cell->state != CellState::Blocked &&
        cell->state != CellState::NotApplicable;
    const QList<QPair<int, int>> chosen = matrix_->selectedCells();
    const QPair<int, int> here(row, column);
    if (chosen.size() > 1 && chosen.contains(here)) {
      add(tr("Run the %n selected cell(s)", nullptr, int(chosen.size())),
          "actRunSelected", [this, chosen] { runCells(chosen); });
      add(tr("Stop the selected cells"), "actStopSelected",
          [this, chosen] { stopCells(chosen); });
      menu->addSeparator();
    }
    if (isRunning)
      add(tr("Stop this run"), "actStop",
          [this, here] { stopCells({here}); });
    else if (isQueued)
      add(tr("Take out of the queue"), "actStop",
          [this, here] { stopCells({here}); });
    else
      add(cell && cell->state == CellState::Done ? tr("Run again…")
                                                 : tr("Run"),
          "actRun", [this, here] { runCells({here}); }, runnable);
    if (r.tool == study::kToolFeatures && cell && !cell->dir.isEmpty() &&
        !QDir(study::runCalibrationDir(cell->dir))
             .entryList({QStringLiteral("*.json")}, QDir::Files)
             .isEmpty())
      add(tr("Recalibrate"), "actRecalibrate",
          [this, here] { runCells({here}, true); }, runnable);
    menu->addSeparator();
    const QString log = cell ? cell->log : QString();
    // A running cell writes its log in its working folder.
    const QString liveLog =
        isRunning && cell
            ? QDir(study::runTmpDir(cell->dir)).filePath(study::kRunLogName)
            : QString();
    const QString shownLog = !liveLog.isEmpty() ? liveLog : log;
    add(tr("Show log"), "actLog",
        [this, shownLog] {
          showLog_->setProperty("log", shownLog);
          showLog();
        },
        !shownLog.isEmpty());
    QString dir;
    if (cell && !cell->dir.isEmpty()) {
      if (QDir(cell->dir).exists())
        dir = cell->dir;
      else if (QDir(study::runFailedDir(cell->dir)).exists())
        dir = study::runFailedDir(cell->dir);
    }
    add(tr("Open folder"), "actFolder",
        [dir] { QDesktopServices::openUrl(QUrl::fromLocalFile(dir)); },
        !dir.isEmpty());
    const QString path = cell ? cell->dir : QString();
    add(tr("Copy the folder's path"), "actCopyPath",
        [path] {
          QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        },
        !path.isEmpty());
    menu->addSeparator();
    const QString caseId = rows_[row].caseId, station = rows_[row].station;
    add(tr("Go to Cases"), "actCases",
        [this, caseId, station] {
          if (ctx_.nav)
            ctx_.nav->show(QStringLiteral("cases"),
                           {{QStringLiteral("case"), caseId},
                            {QStringLiteral("station"), station}});
        },
        ctx_.nav != nullptr);
  } else if (onRow) {
    const Case *c = s.findCase(rows_[row].caseId);
    add(tr("Run this row"), "actRunRow", [this, row] { runRow(row); },
        !s.recipes.isEmpty());
    add(tr("Run this case (%1)").arg(c ? c->name : rows_[row].caseId),
        "actRunCase", [this, row] { runCase(row); }, !s.recipes.isEmpty());
    menu->addSeparator();
    const QString caseId = rows_[row].caseId, station = rows_[row].station;
    add(tr("Go to Cases"), "actCases",
        [this, caseId, station] {
          if (ctx_.nav)
            ctx_.nav->show(QStringLiteral("cases"),
                           {{QStringLiteral("case"), caseId},
                            {QStringLiteral("station"), station}});
        },
        ctx_.nav != nullptr);
  } else if (onColumn) {
    add(tr("Run this recipe on every row"), "actRunRecipe",
        [this, column] { runColumn(column); }, !rows_.isEmpty());
    menu->addSeparator();
    add(tr("Edit recipe…"), "actEdit", [this, column] {
      matrix_->select(matrix_->selectedRow(), column);
      editRecipe();
    });
    add(tr("Duplicate recipe…"), "actDuplicate", [this, column] {
      matrix_->select(matrix_->selectedRow(), column);
      duplicateRecipe();
    });
    add(tr("Remove recipe…"), "actRemove", [this, column] {
      matrix_->select(matrix_->selectedRow(), column);
      removeRecipe();
    });
  }
  return menu;
}

void RunsPage::showMenu(int row, int column, const QPoint &globalPos) {
  QMenu *menu = menuFor(row, column);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  if (menu->isEmpty())
    delete menu;
  else
    menu->popup(globalPos);
}

void RunsPage::fixSelected() {
  const int col = matrix_->selectedColumn(), row = matrix_->selectedRow();
  const Cell *cell = cellAt(row, col);
  if (!cell || !ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  switch (cell->fix) {
  case study::Fix::Cases:
    if (ctx_.nav)
      ctx_.nav->show(QStringLiteral("cases"),
                     {{QStringLiteral("case"), rows_[row].caseId},
                      {QStringLiteral("station"), rows_[row].station}});
    break;
  case study::Fix::Recipe:
    editRecipe();
    break;
  case study::Fix::RunFirst: {
    const QString id = s.recipes[col].settings.value(kFeaturesKey).toString();
    for (int j = 0; j < int(s.recipes.size()); ++j)
      if (s.recipes[j].id == id)
        matrix_->select(row, j);
    break;
  }
  case study::Fix::None:
    break;
  }
}

void RunsPage::openSelectedFolder() {
  const QString dir = openFolder_->property("dir").toString();
  if (!dir.isEmpty())
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void RunsPage::showLog() {
  QString path = showLog_->property("log").toString();
  if (path.isEmpty())
    return;
  // One window per log: asked for again, it comes to the front.
  for (RunLogDialog *open : findChildren<RunLogDialog *>())
    if (open->path() == path) {
      open->refresh();
      open->raise();
      open->activateWindow();
      return;
    }
  auto *dialog = new RunLogDialog(path, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

QStringList RunsPage::queued() const {
  QStringList keys;
  for (const Queued &q : queue_)
    keys << q.key;
  return keys;
}

int RunsPage::atOnce() const {
  const int n = ctx_.settings
                    ? ctx_.settings
                          ->value(QLatin1String(defaults::kRunsAtOnceSetting),
                                  defaults::kRunsAtOnce)
                          .toInt()
                    : defaults::kRunsAtOnce;
  return std::clamp(n, 1, 16);
}

void RunsPage::enqueue(const Row &row, const Recipe &r, bool fresh) {
  const QString k =
      key(row.caseId, study::perStation(r.tool) ? row.station : QString(), r.id);
  if (busy(k) || queued().contains(k))
    return;
  queue_ << Queued{row, r.id, k, fresh};
  reportQueue(int(queue_.size()));
  if (runAll_)
    ++batchAsked_;
}

bool RunsPage::dequeue(const QString &k) {
  for (int i = 0; i < int(queue_.size()); ++i)
    if (queue_[i].key == k) {
      queue_.removeAt(i);
      reportQueue(int(queue_.size()));
      return true;
    }
  return false;
}

void RunsPage::pump() {
  if (!ctx_.study->isOpen() || !lockProblem().isEmpty()) {
    // No study, or one this window may not write into: nothing is started.
    queue_.clear();
    runAll_ = false;
    reportQueue(0);
    if (ctx_.study->isOpen())
      render();
    return;
  }
  const Study &s = ctx_.study->study();
  if (runAll_) {
    // What is missing or stale now, and was not tried in this batch (a cell
    // that failed is not run in a loop), joins the queue: this is how a
    // classification follows the features it was waiting for.
    for (const auto &item : runnable()) {
      const Recipe *r = s.findRecipe(item.second);
      if (!r)
        continue;
      const QString k =
          key(item.first.caseId,
              study::perStation(r->tool) ? item.first.station : QString(),
              r->id);
      if (!attempted_.contains(k))
        enqueue(item.first, *r);
    }
  }
  if (spaceWaits_) { // looked at afresh below
    spaceNote_.clear();
    spaceWaits_ = false;
  }
  // A worker that is still leaving a cancelled run holds its slot too.
  int active = 0;
  for (auto it = working_.constBegin(); it != working_.constEnd(); ++it)
    active += it.value()->load() ? 1 : 0;
  active = std::max(active, int(running_.size()));
  const int limit = atOnce();
  for (int i = 0; i < int(queue_.size()) && active < limit;) {
    const Queued q = queue_[i];
    const Recipe *r = s.findRecipe(q.recipeId);
    const Case *c = s.findCase(q.row.caseId);
    // The study may have changed while the cell waited: what can no longer
    // run (its case or recipe is gone, or it now waits for something)
    // leaves the queue.
    const Cell now = r && c ? study::cellOf(s, *c, q.row.station, *r) : Cell();
    if (!r || !c || now.state == CellState::Blocked ||
        now.state == CellState::NotApplicable) {
      queue_.removeAt(i);
      continue;
    }
    if (busy(q.key)) { // its earlier run is still winding down: later
      ++i;
      continue;
    }
    // The disk is looked at again when the cell's turn comes: what ran
    // before it may have filled it. With runs going on it waits for them;
    // with none it cannot start at all, and leaves the queue saying why.
    if (freeSpace) {
      const qint64 need = study::tempSpaceFor(s, *c, q.row.station, *r).bytes;
      const qint64 free = need > 0 ? freeSpace(s.dir) : -1;
      if (need > 0 && free >= 0 && need > free) {
        const QString what =
            q.row.station.isEmpty()
                ? QStringLiteral("%1 · %2").arg(r->name, c->name)
                : QStringLiteral("%1 · %2 · %3")
                      .arg(r->name, c->name, q.row.station);
        if (active > 0) {
          spaceNote_ = tr("⚠  %1 waits for disk space: it needs about %2 of "
                          "temporary space, %3 are free.")
                           .arg(what, RunWords::bytes(need),
                                RunWords::bytes(free));
          spaceWaits_ = true;
          ++i;
        } else {
          spaceNote_ = tr("⚠  %1 was not started: it needs about %2 of "
                          "temporary space and %3 are free on the study's "
                          "disk.")
                           .arg(what, RunWords::bytes(need),
                                RunWords::bytes(free));
          attempted_.insert(q.key);
          queue_.removeAt(i);
        }
        continue;
      }
    }
    queue_.removeAt(i);
    if (start(q.row, *r, q.fresh))
      ++active;
  }
  if (runAll_ && queue_.isEmpty() && active == 0)
    runAll_ = false;
  if (!runAll_ && queue_.isEmpty() && active == 0)
    batchStarted_ = QDateTime();
  render();
}

} // namespace skg
