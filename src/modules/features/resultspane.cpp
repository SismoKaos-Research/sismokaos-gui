#include "modules/features/resultspane.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include <stdexcept>

#include "fx/calibrate.h"
#include "modules/features/npymodel.h"

namespace skg {

ResultsPane::ResultsPane(QWidget *parent) : QWidget(parent) {
  files_ = new QListWidget(this);
  refresh_ = new QPushButton(this);
  open_ = new QPushButton(this);
  summary_ = new QLabel(this);
  summary_->setWordWrap(true);
  model_ = new NpyModel(this);
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  text_ = new QPlainTextEdit(this);
  text_->setReadOnly(true);
  text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  stack_ = new QStackedWidget(this);
  stack_->addWidget(text_);
  stack_->addWidget(table_);

  auto *buttons = new QHBoxLayout;
  buttons->addWidget(refresh_);
  buttons->addWidget(open_);
  buttons->addStretch(1);
  auto *left = new QWidget(this);
  auto *ll = new QVBoxLayout(left);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->addLayout(buttons);
  ll->addWidget(files_, 1);
  auto *right = new QWidget(this);
  auto *rl = new QVBoxLayout(right);
  rl->setContentsMargins(0, 0, 0, 0);
  rl->addWidget(summary_);
  rl->addWidget(stack_, 1);
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(left);
  split->addWidget(right);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 3);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  connect(refresh_, &QPushButton::clicked, this, &ResultsPane::refresh);
  connect(open_, &QPushButton::clicked, this, [this] {
    if (!dir_.isEmpty())
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir_));
  });
  connect(files_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *cur, QListWidgetItem *) { preview(cur); });
  retranslate();
}

void ResultsPane::setDirectory(const QString &dir) {
  dir_ = dir;
  refresh();
}

void ResultsPane::refresh() {
  const QString selected =
      files_->currentItem() ? files_->currentItem()->text() : QString();
  files_->clear();
  if (dir_.isEmpty() || !QDir(dir_).exists()) {
    summary_->setText(tr("No output directory yet."));
    showText(QString());
    return;
  }
  const QStringList patterns{QStringLiteral("*_features.npy"),
                             QStringLiteral("*_features.csv"),
                             QStringLiteral("*_calibration.json"),
                             QStringLiteral("*_run_metadata.json"),
                             QStringLiteral("*.f32"),
                             QStringLiteral("*.f32.json")};
  const QFileInfoList entries =
      QDir(dir_).entryInfoList(patterns, QDir::Files, QDir::Name);
  for (const QFileInfo &f : entries) {
    auto *item = new QListWidgetItem(f.fileName(), files_);
    item->setData(Qt::UserRole, f.filePath());
    item->setToolTip(tr("%1 bytes").arg(f.size()));
    if (f.fileName() == selected)
      files_->setCurrentItem(item);
  }
  if (entries.isEmpty()) {
    summary_->setText(tr("No results in %1.").arg(dir_));
    showText(QString());
  } else if (!files_->currentItem())
    files_->setCurrentRow(0);
}

void ResultsPane::showText(const QString &text) {
  text_->setPlainText(text);
  stack_->setCurrentWidget(text_);
}

void ResultsPane::preview(QListWidgetItem *item) {
  if (!item) {
    showText(QString());
    return;
  }
  const QString path = item->data(Qt::UserRole).toString();
  const QFileInfo info(path);
  try {
    if (path.endsWith(QStringLiteral(".npy"))) {
      model_->loadNpy(path);
      summary_->setText(
          tr("%1: %n window(s), %2 columns", nullptr, model_->totalRows())
              .arg(info.fileName())
              .arg(model_->columnCount()));
      stack_->setCurrentWidget(table_);
    } else if (path.endsWith(QStringLiteral(".csv"))) {
      model_->loadCsv(path);
      summary_->setText(
          tr("%1: %n window(s), %2 columns", nullptr, model_->totalRows())
              .arg(info.fileName())
              .arg(model_->columnCount()));
      stack_->setCurrentWidget(table_);
    } else if (path.endsWith(QStringLiteral("_calibration.json"))) {
      const fx::Calibration cal = fx::Calibration::load(path);
      summary_->setText(
          tr("Calibration of %1 at %2 Hz").arg(cal.station).arg(cal.fs));
      QFile f(path);
      const QString raw = f.open(QIODevice::ReadOnly)
                              ? QString::fromUtf8(f.readAll())
                              : QString();
      showText(cal.describe().join('\n') + QStringLiteral("\n\n") + raw);
    } else if (path.endsWith(QStringLiteral(".f32"))) {
      QFile side(path + QStringLiteral(".json"));
      summary_->setText(
          tr("%1: %2 bytes of little-endian float32, E N Z interleaved")
              .arg(info.fileName())
              .arg(info.size()));
      if (side.open(QIODevice::ReadOnly)) {
        const QJsonObject m = QJsonDocument::fromJson(side.readAll()).object();
        showText(
            tr("%n sample(s) at %1 Hz, %2 segment(s); NaN per channel E %3, N "
               "%4, Z %5\n\n"
               "numpy: np.memmap(path, dtype='<f4', mode='r').reshape(-1, 3)",
               nullptr, int(m.value("samples").toDouble()))
                .arg(m.value("fs").toDouble())
                .arg(m.value("segments").toArray().size())
                .arg(m.value("nan_counts").toObject().value("E").toDouble())
                .arg(m.value("nan_counts").toObject().value("N").toDouble())
                .arg(m.value("nan_counts").toObject().value("Z").toDouble()));
      } else
        showText(tr("No sidecar (%1) beside the stream.").arg(side.fileName()));
    } else {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error(("cannot read " + path).toStdString());
      summary_->setText(info.fileName());
      showText(QString::fromUtf8(f.read(1 << 20)));
    }
  } catch (const std::exception &e) {
    summary_->setText(info.fileName());
    showText(tr("Cannot preview: %1").arg(QString::fromUtf8(e.what())));
  }
}

void ResultsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void ResultsPane::retranslate() {
  refresh_->setText(tr("Refresh"));
  open_->setText(tr("Open folder"));
}

} // namespace skg
