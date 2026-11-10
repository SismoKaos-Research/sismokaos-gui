#include "modules/study/runspage.h"

#include "modules/study/settinghelp.h"

#include <QComboBox>
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
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <iterator>

#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/study/studyswitcher.h"
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
  updateGeometry();
  update();
}

void RunMatrix::select(int row, int column) {
  if (row == row_ && column == column_)
    return;
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
        // The selected cell; a second, dotted ring while the keys act on it.
        p.setPen(QPen(col("accent"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chip.adjusted(-3, -3, 3, 3), 15, 15);
        if (hasFocus()) {
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
  const QString path = QFileDialog::getOpenFileName(this, windowTitle());
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
  stack_ = new QStackedWidget(this);

  // No study open: say so, and offer the two ways on.
  auto *none = new QWidget(this);
  noStudy_ = new QLabel(none);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  newStudy_ = new QPushButton(none);
  newStudy_->setObjectName(QStringLiteral("newStudy"));
  ui::setPrimary(newStudy_);
  openStudy_ = new QPushButton(none);
  openStudy_->setObjectName(QStringLiteral("openStudy"));
  auto *noneButtons = new QHBoxLayout;
  noneButtons->addStretch(1);
  noneButtons->addWidget(newStudy_);
  noneButtons->addWidget(openStudy_);
  noneButtons->addStretch(1);
  auto *noneLayout = new QVBoxLayout(none);
  noneLayout->addStretch(1);
  noneLayout->addWidget(noStudy_);
  noneLayout->addLayout(noneButtons);
  noneLayout->addStretch(1);
  stack_->addWidget(none);

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

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->addLayout(bar);
  mainLayout->addLayout(guideRow);
  mainLayout->addWidget(scroll, 3);
  mainLayout->addLayout(infoRow);
  mainLayout->addWidget(card, 2);
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
    if (working_.size() != before)
      reload(); // a worker has left its run: its cell can be run again
    if (running_.isEmpty() && working_.isEmpty())
      tick_->stop();
  });
  connect(newStudy_, &QPushButton::clicked, this,
          [this] { StudySwitcher::newStudy(this, ctx_.study); });
  connect(openStudy_, &QPushButton::clicked, this,
          [this] { StudySwitcher::openStudy(this, ctx_.study); });
  connect(run_, &QPushButton::clicked, this, &RunsPage::runAll);
  connect(stop_, &QPushButton::clicked, this, &RunsPage::stopBatch);
  connect(runOne_, &QPushButton::clicked, this, &RunsPage::runSelected);
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
  connect(ctx_.study, &study::StudyStore::changed, this, &RunsPage::reload);

  retranslate();
  reload();
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
                       "the cases, the recipes and every result."));
  newStudy_->setText(tr("New study…"));
  openStudy_->setText(tr("Open study…"));
  stop_->setText(tr("Stop"));
  runOne_->setText(tr("Run the selected cell"));
  new_->setText(tr("New recipe  ▾"));
  edit_->setText(tr("Edit recipe…"));
  remove_->setText(tr("Remove recipe"));
  showLog_->setText(tr("Show log"));
  openFolder_->setText(tr("Open folder"));
  hint_->setText(tr("A recipe is one tool's saved settings; the same recipe "
                    "on every case is the same analysis."));
  newStudy_->setToolTip(ui::helpText(
      tr("Starts a study in a folder you choose; its results are written "
         "there.")));
  openStudy_->setToolTip(ui::helpText(
      tr("Opens a study folder (the one that holds study.toml).")));
  run_->setToolTip(ui::helpText(
      tr("Queues every cell that is missing or stale as a job, two at a "
         "time, and goes on to the cells they unblock (a classification "
         "after its features).")));
  stop_->setToolTip(ui::helpText(
      tr("Stops the runs this page started and starts no further ones. A "
         "stopped run leaves nothing behind; what was finished before stays "
         "as it was.")));
  runOne_->setToolTip(ui::helpText(
      tr("Runs the recipe of the selected cell on its case and station. A "
         "cell that is already done is run again only after you confirm; "
         "its results are replaced when the new run has succeeded.")));
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
  if (!open)
    return;
  const Study &s = ctx_.study->study();
  for (const Case &c : s.cases) {
    if (c.stations.isEmpty())
      rows_ << Row{c.id, QString()};
    for (const study::StationRef &st : c.stations)
      rows_ << Row{c.id, st.code};
  }
  for (const Row &row : rows_) {
    QList<Cell> line;
    const Case *c = s.findCase(row.caseId);
    for (const Recipe &r : s.recipes)
      line << study::cellOf(s, *c, row.station, r);
    cells_ << line;
  }
  render();
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
  int waiting = 0, done = 0;
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
      if (const Job *job = running_.value(keyOf(i, s.recipes[j]))) {
        m.running = true;
        m.fraction = job->progress().fraction;
      }
      line << m;
    }
    cells << line;
  }
  matrix_->setContents(labels, columns, cells);
  const int todo = int(runnable().size());
  run_->setText(tr("Run missing and stale (%1)").arg(todo));
  run_->setEnabled(todo > 0);
  stop_->setEnabled(runAll_ || !running_.isEmpty());

  // The next step, in words, with the page that it is done on.
  QString guide, page, go;
  if (s.cases.isEmpty()) {
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
  if (selection)
    showSelection();
}

QList<QPair<RunsPage::Row, QString>> RunsPage::runnable() const {
  QList<QPair<Row, QString>> out;
  if (!ctx_.study->isOpen())
    return out;
  const Study &s = ctx_.study->study();
  QSet<QString> seen; // a per-case tool appears on every station row once
  for (int i = 0; i < int(rows_.size()); ++i)
    for (int j = 0; j < int(s.recipes.size()); ++j) {
      const Cell *cell = cellAt(i, j);
      if (!cell)
        continue;
      const QString k = keyOf(i, s.recipes[j]);
      if (seen.contains(k) || busy(k))
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
  bool canRun = false;
  const Cell *cell = r ? cellAt(row, col) : nullptr;
  if (cell) {
    const QString k = keyOf(row, *r);
    MatrixCell m;
    m.state = cell->state;
    m.running = running_.contains(k);
    info = RunMatrix::stateText(m).trimmed();
    if (!m.running && !cell->why.isEmpty())
      info += QStringLiteral(" — ") + cell->why;
    if (cell->record && cell->record->finished.isValid())
      info +=
          QStringLiteral(" · ") + tr("last run %1 UTC")
                                      .arg(cell->record->finished.toString(
                                          QStringLiteral("yyyy-MM-dd HH:mm")));
    if (!cell->note.isEmpty())
      info += QStringLiteral(" · ⚠ ") + cell->note;
    if (!cell->dir.isEmpty() && QDir(cell->dir).exists())
      dir = cell->dir;
    else if (!cell->dir.isEmpty() &&
             QDir(study::runFailedDir(cell->dir)).exists())
      dir = study::runFailedDir(cell->dir); // what the failed run left
    log = cell->log;
    if (!m.running && cell->state == CellState::Blocked)
      fix = cell->fix;
    canRun = cell->state != CellState::Blocked &&
             cell->state != CellState::NotApplicable && !busy(k);
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
  cellInfo_->setText(info);
  runOne_->setEnabled(canRun);
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
  const auto apply = [&edited](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == edited.id)
        r = edited;
  };
  // A changed setting makes finished runs stale: say how many, and ask.
  if (edited.revision != s.recipes[col].revision) {
    Study after = s;
    apply(after);
    const int stale = study::runsMadeStale(s, after);
    if (stale > 0 &&
        !confirm(tr("Edit recipe"),
                 tr("Changing a setting of \"%1\" makes %n finished run(s) "
                    "stale (its own, and those that build on them). They "
                    "stay on disk until they are run again.\n\nSave the "
                    "change?",
                    nullptr, stale)
                     .arg(edited.name)))
      return;
  }
  QString error;
  if (!ctx_.study->update(apply, &error))
    QMessageBox::warning(this, tr("Recipe"), error);
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

bool RunsPage::start(const Row &row, const Recipe &recipe) {
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
      name, recipe.tool, tr("recipe %1 on %2").arg(recipe.id, c.id),
      [study, c, station, recipe, version, working](TaskContext &t) -> int {
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
  connect(added, &Job::stateChanged, this, [this, added] {
    if (added->state() == Job::State::Running ||
        added->state() == Job::State::Queued)
      return;
    // Cancelling a run, here or in the Jobs panel, ends the batch: the next
    // cell is not started behind the user's back.
    if (added->state() == Job::State::Cancelled)
      runAll_ = false;
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
  attempted_.clear();
  runAll_ = true;
  pump();
  render();
}

void RunsPage::runSelected() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const int col = matrix_->selectedColumn(), row = matrix_->selectedRow();
  const Cell *cell = cellAt(row, col);
  if (!cell || col >= s.recipes.size())
    return;
  const Recipe &r = s.recipes[col];
  if (cell->state == CellState::Done) {
    const Case *c = s.findCase(rows_[row].caseId);
    if (!confirm(tr("Run again"),
                 tr("\"%1\" already has a finished run on %2. Run it "
                    "again?\n\nThe finished results stay as they are until "
                    "the new run has succeeded; a run that is stopped or "
                    "fails leaves them untouched.")
                     .arg(r.name, c ? c->name : rows_[row].caseId)))
      return;
  }
  start(rows_[row], r);
  render();
}

void RunsPage::stopBatch() {
  runAll_ = false;
  const auto jobs = running_.values();
  for (const QPointer<Job> &job : jobs)
    if (job)
      job->cancel();
  render();
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
  const QString path = showLog_->property("log").toString();
  QFile f(path);
  if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Run log"),
                         tr("Cannot read %1.").arg(path));
    return;
  }
  // The end of a very long log: that is where a failure is.
  constexpr qint64 kMost = 2 * 1024 * 1024;
  const bool cut = f.size() > kMost;
  if (cut)
    f.seek(f.size() - kMost);
  QString text = QString::fromUtf8(f.readAll());
  if (cut)
    text = tr("(the beginning is left out; the whole log is in the file)") +
           QLatin1Char('\n') + text.mid(text.indexOf(QLatin1Char('\n')) + 1);
  auto *dialog = new QDialog(this);
  dialog->setObjectName(QStringLiteral("runLog"));
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Run log"));
  auto *where = new QLabel(QDir::toNativeSeparators(path), dialog);
  where->setObjectName(QStringLiteral("fieldLabel"));
  where->setTextInteractionFlags(Qt::TextSelectableByMouse);
  where->setWordWrap(true);
  auto *view = new QPlainTextEdit(dialog);
  view->setReadOnly(true);
  view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  view->setPlainText(text);
  view->moveCursor(QTextCursor::End);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  auto *layout = new QVBoxLayout(dialog);
  layout->addWidget(where);
  layout->addWidget(view, 1);
  layout->addWidget(buttons);
  dialog->resize(860, 560);
  dialog->show();
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
  render();
}

} // namespace skg
