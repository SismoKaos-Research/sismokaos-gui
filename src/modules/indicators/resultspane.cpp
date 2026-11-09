#include "modules/indicators/resultspane.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

#include "modules/analysis/figureview.h"

namespace skg {

IndicatorTableModel::IndicatorTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

void IndicatorTableModel::setTable(cbi::IndicatorTable table) {
  beginResetModel();
  table_ = std::move(table);
  endResetModel();
}

int IndicatorTableModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : table_.rows();
}

int IndicatorTableModel::columnCount(const QModelIndex &parent) const {
  if (parent.isValid() || table_.empty())
    return 0;
  return 3 + int(table_.columns.size()) + (table_.risk.isEmpty() ? 0 : 1);
}

QVariant IndicatorTableModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole))
    return {};
  const int r = index.row(), c = index.column();
  const size_t ur = static_cast<size_t>(r);
  if (c == 0)
    return table_.xLabel.value(r);
  if (c == 1)
    return table_.start[ur].toString(QStringLiteral("yyyy-MM-dd HH:mm"));
  if (c == 2)
    return table_.end[ur].toString(QStringLiteral("yyyy-MM-dd HH:mm"));
  const int k = c - 3;
  if (k < table_.columns.size()) {
    const double v = table_.values[static_cast<size_t>(k)][ur];
    if (std::isnan(v))
      return QStringLiteral("—");
    if (table_.columns[k] == QLatin1String("Event_Count"))
      return qint64(std::llround(v));
    return QString::number(v, 'g', 6);
  }
  return table_.risk.value(r);
}

QVariant IndicatorTableModel::headerData(int section, Qt::Orientation o,
                                         int role) const {
  if (role != Qt::DisplayRole)
    return {};
  if (o == Qt::Vertical)
    return section;
  switch (section) {
  case 0:
    return tr("Window");
  case 1:
    return tr("Start");
  case 2:
    return tr("End");
  default:
    break;
  }
  const int k = section - 3;
  if (k < table_.columns.size())
    return table_.columns[k];
  return QStringLiteral("Risk_Durumu");
}

IndicatorsResultsPane::IndicatorsResultsPane(
    std::shared_ptr<IndicatorsState> state, QWidget *parent)
    : QWidget(parent), state_(std::move(state)) {
  model_ = new IndicatorTableModel(this);
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->horizontalHeader()->setDefaultSectionSize(110);
  table_->setAlternatingRowColors(true);
  figures_ = new QListWidget(this);
  view_ = new FigureView(this);
  info_ = new QLabel(this);
  info_->setWordWrap(true);
  open_ = new QPushButton(this);
  choose_ = new QPushButton(this);

  auto *top = new QHBoxLayout;
  top->addWidget(info_, 1);
  top->addWidget(choose_);
  top->addWidget(open_);
  auto *figSplit = new QSplitter(Qt::Horizontal, this);
  figSplit->addWidget(figures_);
  figSplit->addWidget(view_);
  figSplit->setStretchFactor(0, 1);
  figSplit->setStretchFactor(1, 4);
  auto *split = new QSplitter(Qt::Vertical, this);
  split->addWidget(table_);
  split->addWidget(figSplit);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 2);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(split, 1);

  connect(figures_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *cur, QListWidgetItem *) { showFigure(cur); });
  connect(open_, &QPushButton::clicked, this,
          &IndicatorsResultsPane::openFolder);
  connect(choose_, &QPushButton::clicked, this,
          &IndicatorsResultsPane::chooseFolder);
  retranslate();
}

void IndicatorsResultsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void IndicatorsResultsPane::retranslate() {
  open_->setText(tr("Open folder"));
  choose_->setText(tr("Open an earlier run…"));
  if (dir_.isEmpty())
    info_->setText(tr("No run yet. Run the indicators or open an earlier "
                      "output folder."));
}

void IndicatorsResultsPane::refresh() {
  if (!state_->hasResult)
    return;
  showFolder(state_->result.outDir);
}

void IndicatorsResultsPane::showFolder(const QString &dir) {
  dir_ = dir;
  const QDir d(dir);
  const QStringList csvs = d.entryList({QStringLiteral("*_pencereler.csv")},
                                       QDir::Files, QDir::Name);
  cbi::IndicatorTable table;
  QString note;
  if (!csvs.isEmpty()) {
    try {
      table = cbi::readTableCsv(d.filePath(csvs.first()));
    } catch (const std::exception &e) {
      note = QString::fromUtf8(e.what());
    }
  }
  model_->setTable(table);
  figures_->clear();
  for (const QString &png :
       d.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name)) {
    auto *item = new QListWidgetItem(png, figures_);
    item->setData(Qt::UserRole, d.filePath(png));
  }
  view_->clearFigure();
  info_->setText(
      tr("%1: %n window(s), %2 figure(s)%3", nullptr, table.rows())
          .arg(d.dirName())
          .arg(figures_->count())
          .arg(note.isEmpty() ? QString() : QStringLiteral(" — ") + note));
  if (figures_->count() > 0)
    figures_->setCurrentRow(0);
}

void IndicatorsResultsPane::showFigure(QListWidgetItem *item) {
  if (!item) {
    view_->clearFigure();
    return;
  }
  const QImage img(item->data(Qt::UserRole).toString());
  if (img.isNull()) {
    view_->clearFigure();
    return;
  }
  view_->setFigure(
      [img](QPainter &p, const QRectF &r) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(r, img);
      },
      img.size());
}

void IndicatorsResultsPane::openFolder() {
  if (!dir_.isEmpty())
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir_));
}

void IndicatorsResultsPane::chooseFolder() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Output folder of an indicators run"),
      dir_.isEmpty() ? QDir::homePath() : QFileInfo(dir_).path());
  if (!dir.isEmpty())
    showFolder(dir);
}

} // namespace skg
