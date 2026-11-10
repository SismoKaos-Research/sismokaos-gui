#include "modules/study/runspage.h"

#include "modules/study/settinghelp.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/store.h"

namespace skg {

using study::Case;
using study::Cell;
using study::CellState;
using study::Recipe;
using study::Study;

namespace {

constexpr double kLabelWidth = 250, kHeaderHeight = 58, kRowHeight = 44;
constexpr int kMaxAtOnce = 2;

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
  updateGeometry();
  update();
}

void RunMatrix::select(int row, int column) {
  if (row == row_ && column == column_)
    return;
  row_ = row;
  column_ = column;
  update();
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
  switch (c.state) {
  case CellState::Done:
    return tr("✓  Done");
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
      if (i == row_ && c == column_) {
        p.setPen(QPen(col("accent"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chip.adjusted(-3, -3, 3, 3), 15, 15);
      }
      p.setPen(col("text"));
      p.setFont(font());
      p.drawText(chip.adjusted(12, 0, -6, 0), Qt::AlignVCenter, text);
    }
  }
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
  select(row, column);
}

// ---------------------------------------------------------------- recipe

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
  }
  table_->setColumnWidth(0, 250);


  QString note =
      tr("Values keep their kind: true/false, a number, text, or a list "
         "separated by commas. Changing a setting makes the recipe's earlier "
         "runs stale.");
  if (r.tool == study::kToolClassification) {
    QStringList ids;
    for (const Recipe &o : s.recipes)
      if (o.tool == study::kToolFeatures)
        ids << o.id;
    note += QLatin1Char('\n') +
            tr("\"Features recipe\" takes the id of the features recipe "
               "whose table is classified (%1). Classes are ranges of days "
               "before the event.")
                .arg(ids.isEmpty() ? tr("none yet")
                                   : ids.join(QStringLiteral(", ")));
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
  layout->addLayout(bottom);
  resize(640, 620);
  ui::setHelp(name_, tr("The recipe's name: the column heading on the Runs "
                        "page and the entry in the Compare lists. The "
                        "result folders use an id made from the first "
                        "name, which does not change on a rename."));
}

void RecipeDialog::loadFile() {
  const int row = table_->currentRow();
  if (row < 0)
    return;
  QTableWidgetItem *v = table_->item(row, 1);
  if (v->data(Qt::UserRole).metaType().id() != QMetaType::QString) {
    QMessageBox::information(this, windowTitle(),
                             tr("Only a text setting can take a file."));
    return;
  }
  const QString path = QFileDialog::getOpenFileName(this, windowTitle());
  if (path.isEmpty())
    return;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, windowTitle(), f.errorString());
    return;
  }
  const QString text = QString::fromUtf8(f.readAll());
  v->setData(Qt::UserRole, text);
  v->setText(valueText(text));
  if (isLongText(text))
    v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

void RecipeDialog::accept() {
  Recipe next = recipe_;
  if (!name_->text().trimmed().isEmpty())
    next.name = name_->text().trimmed();
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QString k = table_->item(row, 0)->data(Qt::UserRole).toString();
    const QTableWidgetItem *v = table_->item(row, 1);
    const QVariant old = v->data(Qt::UserRole);
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
  stack_ = new QStackedWidget(this);
  noStudy_ = new QLabel(this);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  stack_->addWidget(noStudy_);

  auto *main = new QWidget(this);
  run_ = new QPushButton(main);
  ui::setPrimary(run_);
  runOne_ = new QPushButton(main);
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
  openFolder_ = new QPushButton(main);
  hint_ = new QLabel(main);
  hint_->setObjectName(QStringLiteral("fieldLabel"));
  auto *bar = new QHBoxLayout;
  bar->addWidget(run_);
  bar->addWidget(runOne_);
  bar->addSpacing(12);
  bar->addWidget(new_);
  bar->addWidget(edit_);
  bar->addWidget(remove_);
  bar->addStretch(1);
  bar->addWidget(hint_);

  matrix_ = new RunMatrix(ctx_.theme, main);
  auto *scroll = new QScrollArea(main);
  scroll->setWidget(matrix_);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  cellInfo_ = new QLabel(main);
  cellInfo_->setObjectName(QStringLiteral("fieldLabel"));
  cellInfo_->setWordWrap(true);
  cellInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto *infoRow = new QHBoxLayout;
  infoRow->addWidget(cellInfo_, 1);
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

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addLayout(bar);
  mainLayout->addWidget(scroll, 3);
  mainLayout->addLayout(infoRow);
  mainLayout->addWidget(card, 2);
  stack_->addWidget(main);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  tick_ = new QTimer(this);
  tick_->setInterval(700);
  connect(tick_, &QTimer::timeout, this, [this] {
    if (running_.isEmpty())
      tick_->stop();
    refresh();
  });
  connect(run_, &QPushButton::clicked, this, &RunsPage::runAll);
  connect(runOne_, &QPushButton::clicked, this, &RunsPage::runSelected);
  connect(edit_, &QPushButton::clicked, this, &RunsPage::editRecipe);
  connect(remove_, &QPushButton::clicked, this, &RunsPage::removeRecipe);
  connect(openFolder_, &QPushButton::clicked, this, [this] {
    const QString dir = openFolder_->property("dir").toString();
    if (!dir.isEmpty())
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  connect(matrix_, &RunMatrix::selectionChanged, this,
          &RunsPage::showSelection);
  connect(ctx_.study, &study::StudyStore::changed, this, &RunsPage::refresh);

  retranslate();
  refresh();
}

void RunsPage::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refresh();
  }
  QWidget::changeEvent(e);
}

void RunsPage::retranslate() {
  noStudy_->setText(tr("No study is open.\nOpen or start one on the Cases "
                       "page, or from the study selector above."));
  runOne_->setText(tr("Run the selected cell"));
  new_->setText(tr("New recipe  ▾"));
  edit_->setText(tr("Edit recipe…"));
  remove_->setText(tr("Remove recipe"));
  openFolder_->setText(tr("Open folder"));
  hint_->setText(tr("A recipe is one tool's saved settings; the same recipe "
                    "on every case is the same analysis."));
  run_->setToolTip(ui::helpText(
      tr("Queues every cell that is missing or stale as a job, two at a "
         "time, and goes on to the cells they unblock (a classification "
         "after its features).")));
  runOne_->setToolTip(ui::helpText(
      tr("Runs the recipe of the selected cell on its case and station, "
         "again if it was already done.")));
  new_->setToolTip(ui::helpText(
      tr("Adds a recipe: one tool's settings under a name, run on every "
         "case. Indicators run once per case; Features and Classification "
         "once per station.")));
  edit_->setToolTip(ui::helpText(
      tr("Changes the settings of the selected column's recipe. A changed "
         "setting raises the revision and marks its finished runs stale.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected column's recipe out of the study. The result "
         "folders stay on disk.")));
  openFolder_->setToolTip(ui::helpText(
      tr("Opens the selected cell's result folder in the file manager.")));
  recipeTable_->setHorizontalHeaderLabels(
      {tr("Setting"), tr("Value"), tr("Across cases")});
}

QString RunsPage::key(const QString &caseId, const QString &station,
                      const QString &recipeId) {
  return caseId + QLatin1Char('|') + station + QLatin1Char('|') + recipeId;
}

void RunsPage::refresh() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  if (!open)
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
  const Study &s = ctx_.study->study();
  rows_.clear();
  QStringList labels;
  for (const Case &c : s.cases) {
    if (c.stations.isEmpty()) {
      rows_ << Row{c.id, QString()};
      labels << c.name;
    }
    for (const study::StationRef &st : c.stations) {
      rows_ << Row{c.id, st.code};
      labels << c.name + QStringLiteral(" · ") + st.code;
    }
  }
  QList<QPair<QString, QString>> columns;
  for (const Recipe &r : s.recipes)
    columns << qMakePair(r.name, r.tool);
  QList<QList<MatrixCell>> cells;
  int todo = 0;
  for (const Row &row : rows_) {
    QList<MatrixCell> line;
    const Case *c = s.findCase(row.caseId);
    for (const Recipe &r : s.recipes) {
      const Cell cell = study::cellOf(s, *c, row.station, r);
      MatrixCell m;
      m.state = cell.state;
      m.why = cell.why;
      const QString k =
          key(row.caseId, study::perStation(r.tool) ? row.station : QString(),
              r.id);
      if (const Job *job = running_.value(k)) {
        m.running = true;
        m.fraction = job->progress().fraction;
      } else if (cell.state == CellState::Missing ||
                 cell.state == CellState::Stale ||
                 cell.state == CellState::Failed) {
        ++todo;
      }
      line << m;
    }
    cells << line;
  }
  matrix_->setContents(labels, columns, cells);
  run_->setText(tr("Run missing and stale (%1)").arg(runnable().size()));
  run_->setEnabled(todo > 0);
  showSelection();
}

QList<QPair<RunsPage::Row, QString>> RunsPage::runnable() const {
  QList<QPair<Row, QString>> out;
  if (!ctx_.study->isOpen())
    return out;
  const Study &s = ctx_.study->study();
  QSet<QString> seen; // a per-case tool appears on every station row once
  for (const Row &row : rows_) {
    const Case *c = s.findCase(row.caseId);
    if (!c)
      continue;
    for (const Recipe &r : s.recipes) {
      const QString k =
          key(row.caseId, study::perStation(r.tool) ? row.station : QString(),
              r.id);
      if (seen.contains(k) || running_.contains(k))
        continue;
      seen.insert(k);
      const CellState st = study::cellOf(s, *c, row.station, r).state;
      if (st == CellState::Missing || st == CellState::Stale ||
          st == CellState::Failed)
        out << qMakePair(row, r.id);
    }
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
  QString info;
  QString dir;
  bool canRun = false;
  if (r && row >= 0 && row < rows_.size()) {
    const Case *c = s.findCase(rows_[row].caseId);
    const Cell cell = study::cellOf(s, *c, rows_[row].station, *r);
    MatrixCell m;
    m.state = cell.state;
    info = RunMatrix::stateText(m).trimmed();
    if (!cell.why.isEmpty())
      info += QStringLiteral(" — ") + cell.why;
    if (cell.record && cell.record->finished.isValid())
      info +=
          QStringLiteral(" · ") + tr("last run %1 UTC")
                                      .arg(cell.record->finished.toString(
                                          QStringLiteral("yyyy-MM-dd HH:mm")));
    if (!cell.dir.isEmpty() && QDir(cell.dir).exists())
      dir = cell.dir;
    canRun =
        cell.state != CellState::Blocked &&
        cell.state != CellState::NotApplicable &&
        !running_.contains(key(
            c->id, study::perStation(r->tool) ? rows_[row].station : QString(),
            r->id));
  } else if (s.recipes.isEmpty()) {
    info = tr("No recipes yet. Use New recipe to add one.");
  } else {
    info = tr("Click a cell to see why it is waiting or where its files are.");
  }
  cellInfo_->setText(info);
  runOne_->setEnabled(canRun);
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
  for (auto it = r->settings.begin(); it != r->settings.end(); ++it)
    lines << QStringList{it.key(), valueText(it.value()),
                         fittedPerCase(*r, it.key()) ? tr("fitted per case")
                                                     : tr("fixed")};
  for (const QString &f : r->fitted)
    if (!r->settings.contains(f))
      lines << QStringList{f, QStringLiteral("–"), tr("fitted per case")};
  recipeTable_->setRowCount(int(lines.size()));
  for (int i = 0; i < int(lines.size()); ++i) {
    // Readable name in the first column; the key and what the setting does
    // on hover.
    const SettingInfo info = settingInfo(r->tool, lines[i][0]);
    for (int j = 0; j < 3; ++j) {
      auto *item = new QTableWidgetItem(j == 0 ? info.label : lines[i][j]);
      item->setData(Qt::UserRole, lines[i][0]);
      item->setToolTip(info.tooltip(lines[i][0]));
      recipeTable_->setItem(i, j, item);
    }
  }
  recipeTable_->setColumnWidth(0, 200);
  recipeTable_->setColumnWidth(1, 430);
}

void RunsPage::newRecipe(const QString &tool) {
  const Study &s = ctx_.study->study();
  Recipe r = study::defaultRecipe(tool);
  if (tool == study::kToolClassification)
    for (const Recipe &o : s.recipes)
      if (o.tool == study::kToolFeatures) {
        r.settings.insert(QStringLiteral("features_recipe"), o.id);
        break;
      }
  RecipeDialog dialog(s, r, this);
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
  RecipeDialog dialog(s, s.recipes[col], this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  const Recipe edited = dialog.recipe();
  QString error;
  if (!ctx_.study->update(
          [&edited](Study &st) {
            for (Recipe &r : st.recipes)
              if (r.id == edited.id)
                r = edited;
          },
          &error))
    QMessageBox::warning(this, tr("Recipe"), error);
}

void RunsPage::removeRecipe() {
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn();
  if (col < 0 || col >= s.recipes.size())
    return;
  const QString id = s.recipes[col].id;
  if (QMessageBox::question(
          this, tr("Remove recipe"),
          tr("Remove the recipe %1? The folders of its runs stay on disk.")
              .arg(s.recipes[col].name)) != QMessageBox::Yes)
    return;
  ctx_.study->update([&id](Study &st) {
    st.recipes.removeIf([&id](const Recipe &r) { return r.id == id; });
  });
}

bool RunsPage::start(const Row &row, const Recipe &recipe) {
  const Study study = ctx_.study->study(); // a snapshot for the worker
  const Case *found = study.findCase(row.caseId);
  if (!found)
    return false;
  const Case c = *found;
  const QString station =
      study::perStation(recipe.tool) ? row.station : QString();
  const QString k = key(c.id, station, recipe.id);
  if (running_.contains(k))
    return false;
  const QString version = QCoreApplication::applicationVersion();
  const QString name =
      station.isEmpty()
          ? QStringLiteral("%1 · %2").arg(recipe.name, c.name)
          : QStringLiteral("%1 · %2 · %3").arg(recipe.name, c.name, station);
  auto *job = new TaskJob(
      name, recipe.tool, tr("recipe %1 on %2").arg(recipe.id, c.id),
      [study, c, station, recipe, version](TaskContext &t) -> int {
        study::RunHooks hooks;
        hooks.log = [&t](const QString &l) { t.log(l); };
        hooks.shouldStop = [&t] { return t.cancelled(); };
        hooks.progress = [&t](double f, const QString &what) {
          t.progress(f, what);
        };
        try {
          return study::runCell(study, c, station, recipe, version, hooks) ? 0
                                                                           : 1;
        } catch (const std::exception &e) {
          t.log(QStringLiteral("error: %1").arg(QString::fromUtf8(e.what())));
          return 1;
        }
      });
  attempted_.insert(k);
  Job *added = ctx_.jobs->add(job);
  running_.insert(k, added);
  connect(added, &Job::stateChanged, this, [this, added] {
    if (added->state() == Job::State::Running ||
        added->state() == Job::State::Queued)
      return;
    // After the job has settled: show the new state, start what is next.
    QTimer::singleShot(0, this, [this] {
      refresh();
      pump();
    });
  });
  tick_->start();
  return true;
}

void RunsPage::runAll() {
  attempted_.clear();
  runAll_ = true;
  pump();
  refresh();
}

void RunsPage::runSelected() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn(), row = matrix_->selectedRow();
  if (col < 0 || col >= s.recipes.size() || row < 0 || row >= rows_.size())
    return;
  start(rows_[row], s.recipes[col]);
  refresh();
}

void RunsPage::pump() {
  if (!runAll_ || !ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  int active = int(running_.size());
  bool started = false;
  for (const auto &item : runnable()) {
    if (active >= kMaxAtOnce)
      break;
    const Recipe *r = s.findRecipe(item.second);
    if (!r)
      continue;
    const QString k =
        key(item.first.caseId,
            study::perStation(r->tool) ? item.first.station : QString(), r->id);
    if (attempted_.contains(k))
      continue; // tried in this batch already (it failed): do not loop
    if (start(item.first, *r)) {
      ++active;
      started = true;
    }
  }
  if (active == 0 && !started)
    runAll_ = false;
}

} // namespace skg
