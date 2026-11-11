#include "modules/classification/experimentspane.h"

#include "core/ui.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include "modules/analysis/figureview.h"
#include "sfc/run.h"

namespace skg {

// --- runs model
// ---------------------------------------------------------------------

class RunsModel : public QAbstractTableModel {
public:
  enum Column {
    Run,
    Models,
    Best,
    ValF1,
    TestF1,
    TestAcc,
    TestAuc,
    Changes,
    ColumnCount
  };
  explicit RunsModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
  void setRuns(std::vector<sfc::RunSummary> runs) {
    beginResetModel();
    runs_ = std::move(runs);
    endResetModel();
  }
  const sfc::RunSummary *at(int row) const {
    return row >= 0 && row < int(runs_.size()) ? &runs_[size_t(row)] : nullptr;
  }
  int rowCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : int(runs_.size());
  }
  int columnCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : ColumnCount;
  }
  QVariant data(const QModelIndex &i, int role) const override {
    if (!i.isValid() || role != Qt::DisplayRole)
      return {};
    const sfc::RunSummary &r = runs_[size_t(i.row())];
    auto m = [&r](double v) {
      return r.models ? QString::number(v, 'f', 3) : QString();
    };
    switch (i.column()) {
    case Run:
      return r.run;
    case Models:
      return r.models;
    case Best:
      return r.bestModel;
    case ValF1:
      return m(r.valF1);
    case TestF1:
      return m(r.testF1);
    case TestAcc:
      return m(r.testAcc);
    case TestAuc:
      return m(r.testAuc);
    case Changes:
      return r.changes ? (r.changes->isEmpty() ? QStringLiteral("-")
                                               : r.changes->join(", "))
                       : QCoreApplication::translate("skg::ExperimentsPane",
                                                     "(no settings.toml)");
    }
    return {};
  }
  QVariant headerData(int s, Qt::Orientation o, int role) const override {
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
      return {};
    // No Q_OBJECT here, so tr() would look the texts up under the base
    // class and never find them; they are filed under the pane. Not cached:
    // the language can change while the application runs.
    const auto t = [](const char *text) {
      return QCoreApplication::translate("skg::ExperimentsPane", text);
    };
    const QStringList h{t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "Run")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "Models")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "Best model")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "val F1")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "test F1")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "test acc")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "test AUC")),
                        t(QT_TRANSLATE_NOOP("skg::ExperimentsPane", "Settings different from the project"))};
    return h.value(s);
  }

private:
  std::vector<sfc::RunSummary> runs_;
};

// --- trials model
// -------------------------------------------------------------------

TrialsModel::TrialsModel(QObject *parent) : QAbstractTableModel(parent) {}

void TrialsModel::setTrials(std::vector<sfc::Trial> trials) {
  beginResetModel();
  trials_ = std::move(trials);
  endResetModel();
}

const sfc::Trial *TrialsModel::trialAt(int row) const {
  return row >= 0 && row < int(trials_.size()) ? &trials_[size_t(row)]
                                               : nullptr;
}

int TrialsModel::rowCount(const QModelIndex &p) const {
  return p.isValid() ? 0 : int(trials_.size());
}
int TrialsModel::columnCount(const QModelIndex &p) const {
  return p.isValid() ? 0 : 7;
}

QVariant TrialsModel::data(const QModelIndex &i, int role) const {
  if (!i.isValid() || role != Qt::DisplayRole)
    return {};
  const sfc::Trial &t = trials_[size_t(i.row())];
  auto m = [](double v) {
    return std::isnan(v) ? QString() : QString::number(v, 'f', 3);
  };
  switch (i.column()) {
  case 0:
    return t.model;
  case 1:
    return m(t.trainF1);
  case 2:
    return m(t.valF1);
  case 3:
    return m(t.testF1);
  case 4:
    return m(t.testAcc);
  case 5:
    return m(t.testAuc);
  case 6:
    return t.params;
  }
  return {};
}

QVariant TrialsModel::headerData(int s, Qt::Orientation o, int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  static const QStringList h{tr("Model"),     tr("train F1"), tr("val F1"),
                             tr("test F1"),   tr("test acc"), tr("test AUC"),
                             tr("Parameters")};
  return h.value(s);
}

// --- pane
// -----------------------------------------------------------------------------

ExperimentsPane::ExperimentsPane(std::shared_ptr<ClassificationState> state,
                                 QWidget *parent)
    : QWidget(parent), state_(std::move(state)) {
  runsModel_ = new RunsModel(this);
  runs_ = new QTableView(this);
  runs_->setModel(runsModel_);
  runs_->setSelectionBehavior(QAbstractItemView::SelectRows);
  runs_->setSelectionMode(QAbstractItemView::SingleSelection);
  runs_->verticalHeader()->hide();
  runs_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  runs_->horizontalHeader()->setSectionResizeMode(RunsModel::Changes,
                                                  QHeaderView::Stretch);
  trialsModel_ = new TrialsModel(this);
  trials_ = new QTableView(this);
  trials_->setModel(trialsModel_);
  trials_->setSelectionBehavior(QAbstractItemView::SelectRows);
  trials_->verticalHeader()->hide();
  trials_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  trials_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
  runInfo_ = new QLabel(this);
  runInfo_->setWordWrap(true);
  files_ = new QListWidget(this);
  figure_ = new FigureView(this);
  text_ = new QPlainTextEdit(this);
  text_->setReadOnly(true);
  text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  stack_ = new QStackedWidget(this);
  stack_->addWidget(text_);
  stack_->addWidget(figure_);
  refresh_ = new QPushButton(this);
  report_ = new QPushButton(this);
  remove_ = new QPushButton(this);
  openFolder_ = new QPushButton(this);
  auto *bar = new QHBoxLayout;
  bar->addWidget(refresh_);
  bar->addWidget(report_);
  bar->addWidget(openFolder_);
  bar->addStretch(1);
  bar->addWidget(remove_);
  auto *top = new QWidget(this);
  auto *tl = new QVBoxLayout(top);
  tl->setContentsMargins(0, 0, 0, 0);
  tl->addLayout(bar);
  tl->addWidget(runs_, 1);
  auto *left = new QWidget(this);
  auto *ll = new QVBoxLayout(left);
  ll->setContentsMargins(0, 0, 0, 0);
  ll->addWidget(runInfo_);
  ll->addWidget(trials_, 2);
  ll->addWidget(files_, 3);
  auto *detail = new QSplitter(Qt::Horizontal, this);
  detail->addWidget(left);
  detail->addWidget(stack_);
  detail->setStretchFactor(0, 2);
  detail->setStretchFactor(1, 3);
  auto *split = new QSplitter(Qt::Vertical, this);
  split->addWidget(top);
  split->addWidget(detail);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 3);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  connect(runs_->selectionModel(), &QItemSelectionModel::currentRowChanged,
          this, [this](const QModelIndex &cur, const QModelIndex &) {
            selectRun(cur.row());
          });
  connect(files_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *cur, QListWidgetItem *) { showFile(cur); });
  connect(refresh_, &QPushButton::clicked, this, &ExperimentsPane::refresh);
  connect(report_, &QPushButton::clicked, this, &ExperimentsPane::writeReport);
  connect(remove_, &QPushButton::clicked, this, &ExperimentsPane::removeRun);
  connect(openFolder_, &QPushButton::clicked, this, [this] {
    const sfc::RunSummary *r = runsModel_->at(runs_->currentIndex().row());
    const QString dir = r ? modelsDir_ + '/' + r->run : modelsDir_;
    if (!dir.isEmpty())
      QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
  });
  retranslate();
}

void ExperimentsPane::refresh() {
  const QString keep = runsModel_->at(runs_->currentIndex().row())
                           ? runsModel_->at(runs_->currentIndex().row())->run
                           : QString();
  modelsDir_ = state_->projectDir.isEmpty()
                   ? QString()
                   : sfc::modelsDir(state_->projectDir, state_->settings);
  summaries_ = modelsDir_.isEmpty()
                   ? std::vector<sfc::RunSummary>()
                   : sfc::summarize(modelsDir_, state_->settings.asMap());
  runsModel_->setRuns(summaries_);
  int row = 0;
  for (size_t i = 0; i < summaries_.size(); ++i)
    if (summaries_[i].run == keep)
      row = int(i);
  if (!summaries_.empty())
    runs_->selectRow(row);
  else
    selectRun(-1);
  report_->setEnabled(!summaries_.empty());
}

void ExperimentsPane::selectRun(int row) {
  const sfc::RunSummary *r = runsModel_->at(row);
  files_->clear();
  trialsModel_->setTrials({});
  remove_->setEnabled(r != nullptr);
  if (!r) {
    runInfo_->setText(tr("No experiments yet: set the project and run."));
    text_->clear();
    stack_->setCurrentWidget(text_);
    return;
  }
  std::vector<sfc::Trial> mine;
  for (const sfc::Trial &t :
       sfc::loadTrials(modelsDir_ + QStringLiteral("/experiments.csv")))
    if (t.run == r->run)
      mine.push_back(t);
  std::sort(mine.begin(), mine.end(),
            [](const sfc::Trial &a, const sfc::Trial &b) {
              return a.valF1 > b.valF1;
            });
  trialsModel_->setTrials(mine);
  QString info = tr("%1: %n model(s)", nullptr, r->models).arg(r->run);
  if (r->changes)
    info += r->changes->isEmpty()
                ? tr("; the project's settings")
                : tr("; changed: %1").arg(r->changes->join(", "));
  runInfo_->setText(info);
  const QString folder = modelsDir_ + '/' + r->run;
  QStringList paths;
  QDirIterator it(folder, {"*.png", "*.txt", "*.csv", "*.toml"}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext())
    paths << it.next();
  std::sort(paths.begin(), paths.end());
  for (const QString &p : paths) {
    auto *item = new QListWidgetItem(QDir(folder).relativeFilePath(p), files_);
    item->setData(Qt::UserRole, p);
  }
  if (files_->count())
    files_->setCurrentRow(0);
}

void ExperimentsPane::showFile(QListWidgetItem *item) {
  if (!item) {
    text_->clear();
    stack_->setCurrentWidget(text_);
    return;
  }
  const QString path = item->data(Qt::UserRole).toString();
  if (path.endsWith(".png")) {
    const QImage img(path);
    figure_->setFigure(
        [img](QPainter &p, const QRectF &r) { p.drawImage(r, img); },
        img.size());
    stack_->setCurrentWidget(figure_);
    return;
  }
  QFile f(path);
  text_->setPlainText(f.open(QIODevice::ReadOnly)
                          ? QString::fromUtf8(f.read(2 << 20))
                          : tr("Cannot read %1").arg(path));
  stack_->setCurrentWidget(text_);
}

void ExperimentsPane::removeRun() {
  const sfc::RunSummary *r = runsModel_->at(runs_->currentIndex().row());
  if (!r)
    return;
  const auto answer = QMessageBox::question(
      this, tr("Delete run"),
      tr("Delete the run '%1': its folder and its rows in experiments.csv?")
          .arg(r->run));
  if (answer != QMessageBox::Yes)
    return;
  sfc::removeRun(modelsDir_, r->run);
  refresh();
}

void ExperimentsPane::writeReport() {
  if (modelsDir_.isEmpty())
    return;
  const QString path =
      sfc::writeHtmlReport(state_->projectDir, state_->settings);
  if (QFileInfo::exists(path))
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  else
    QMessageBox::warning(this, tr("Report"), tr("Could not write the report."));
}

void ExperimentsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void ExperimentsPane::retranslate() {
  ui::setEmptyHint(
      runs_,
      tr("No experiments yet.\nChoose a project folder and run the steps."));
  refresh_->setText(tr("Refresh"));
  report_->setText(tr("HTML report"));
  remove_->setText(tr("Delete run…"));
  openFolder_->setText(tr("Open folder"));
  refresh_->setToolTip(
      ui::helpText(tr("Reads the project's experiments from disk again.")));
  report_->setToolTip(ui::helpText(
      tr("Writes one self-contained HTML page with every run's scores, "
         "confusion matrices and figures, and opens it.")));
  remove_->setToolTip(ui::helpText(
      tr("Deletes the selected run's folder and its rows in "
         "experiments.csv. This cannot be undone.")));
  openFolder_->setToolTip(ui::helpText(
      tr("Opens the selected run's folder in the file manager.")));
}

} // namespace skg
