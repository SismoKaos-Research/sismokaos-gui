#include "studyui/studyinputs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>

#include "core/ui.h"
#include "study/runs.h"
#include "study/store.h"

namespace skg {

StudyInputsBar::StudyInputsBar(study::StudyStore *store, QSettings *settings,
                               const QString &settingsKey, Needs needs,
                               const QString &recipeTool, QWidget *parent)
    : QFrame(parent), store_(store), settings_(settings), key_(settingsKey),
      needs_(needs), tool_(recipeTool) {
  setObjectName(QStringLiteral("studyInputs"));
  use_ = new QCheckBox(this);
  use_->setObjectName(QStringLiteral("useStudy"));
  use_->setChecked(!settings_ || settings_->value(key_, true).toBool());
  case_ = new QComboBox(this);
  case_->setObjectName(QStringLiteral("studyCase"));
  case_->setMinimumWidth(170);
  caseLabel_ = ui::buddyLabel(QString(), case_, this);
  station_ = new QComboBox(this);
  station_->setObjectName(QStringLiteral("studyStation"));
  station_->setMinimumWidth(90);
  stationLabel_ = ui::buddyLabel(QString(), station_, this);
  recipe_ = new QPushButton(this);
  recipe_->setObjectName(QStringLiteral("saveRecipe"));
  auto *l = new QHBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(use_);
  l->addSpacing(8);
  l->addWidget(caseLabel_);
  l->addWidget(case_);
  l->addWidget(stationLabel_);
  l->addWidget(station_);
  l->addStretch(1);
  l->addWidget(recipe_);
  const bool withStation = needs_ == Needs::CaseAndStation;
  stationLabel_->setVisible(withStation);
  station_->setVisible(withStation);
  recipe_->setVisible(!tool_.isEmpty());

  connect(use_, &QCheckBox::toggled, this, [this](bool on) {
    if (settings_)
      settings_->setValue(key_, on);
    refill();
    emit inputsChanged();
  });
  connect(case_, &QComboBox::currentIndexChanged, this, [this] {
    refill();
    emit inputsChanged();
  });
  connect(station_, &QComboBox::currentIndexChanged, this,
          &StudyInputsBar::inputsChanged);
  connect(recipe_, &QPushButton::clicked, this,
          &StudyInputsBar::recipeRequested);
  if (store_)
    connect(store_, &study::StudyStore::changed, this, [this] {
      refill();
      emit inputsChanged();
    });
  retranslate();
  refill();
}

bool StudyInputsBar::active() const {
  return store_ && store_->isOpen() && use_->isChecked();
}

const study::Study *StudyInputsBar::study() const {
  return active() ? &store_->study() : nullptr;
}

const study::Case *StudyInputsBar::currentCase() const {
  const study::Study *s = study();
  return s ? s->findCase(case_->currentData().toString()) : nullptr;
}

const study::StationRef *StudyInputsBar::currentStation() const {
  const study::Case *c = currentCase();
  if (!c)
    return nullptr;
  const QString code = station_->currentData().toString();
  for (const study::StationRef &st : c->stations)
    if (st.code == code)
      return &st;
  return nullptr;
}

QString StudyInputsBar::featureTable() const {
  const study::Study *s = study();
  const study::Case *c = currentCase();
  const study::StationRef *st = currentStation();
  if (!s || !c || !st)
    return {};
  for (const study::Recipe &r : s->recipes) {
    if (r.tool != study::kToolFeatures)
      continue;
    const study::Cell cell = study::cellOf(*s, *c, st->code, r);
    if (cell.state != study::CellState::Done || !cell.record)
      continue;
    const QString rel =
        cell.record->outputs.value(QStringLiteral("features")).toString();
    if (!rel.isEmpty())
      return QDir(cell.dir).filePath(rel);
  }
  return {};
}

void StudyInputsBar::refill() {
  const bool open = store_ && store_->isOpen();
  setVisible(open);
  if (!open)
    return;
  const study::Study &s = store_->study();
  const QString keepCase = case_->currentData().toString();
  {
    const QSignalBlocker block(case_);
    case_->clear();
    for (const study::Case &c : s.cases)
      case_->addItem(c.name, c.id);
    const int i = case_->findData(keepCase);
    case_->setCurrentIndex(i >= 0 ? i : (s.cases.isEmpty() ? -1 : 0));
  }
  const QString keepStation = station_->currentData().toString();
  {
    const QSignalBlocker block(station_);
    station_->clear();
    if (const study::Case *c = s.findCase(case_->currentData().toString()))
      for (const study::StationRef &st : c->stations)
        station_->addItem(st.code, st.code);
    const int i = station_->findData(keepStation);
    station_->setCurrentIndex(i >= 0 ? i : (station_->count() ? 0 : -1));
  }
  const bool on = use_->isChecked();
  case_->setEnabled(on && case_->count() > 0);
  station_->setEnabled(on && station_->count() > 0);
  use_->setText(tr("Use the inputs of the study \"%1\"").arg(s.name));
}

QString StudyInputsBar::addRecipe(study::StudyStore *store, const QString &tool,
                                  const QString &name,
                                  const QVariantMap &settings, QString *error) {
  if (!store || !store->isOpen()) {
    if (error)
      *error = tr("No study is open.");
    return {};
  }
  study::Recipe r = study::defaultRecipe(tool);
  if (!name.trimmed().isEmpty())
    r.name = name.trimmed();
  for (auto it = settings.begin(); it != settings.end(); ++it)
    r.settings.insert(it.key(), it.value());
  QString id;
  if (!store->update(
          [&](study::Study &s) {
            r.id = s.freeRecipeId(r.name);
            id = r.id;
            s.recipes << r;
          },
          error))
    return {};
  return id;
}

void StudyInputsBar::saveRecipe(const QVariantMap &settings) {
  if (!store_ || !store_->isOpen() || tool_.isEmpty())
    return;
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, tr("Save as study recipe"),
      tr("Name of the recipe (it will be run on every case of \"%1\"):")
          .arg(store_->study().name),
      QLineEdit::Normal, study::defaultRecipe(tool_).name, &ok);
  if (!ok || name.trimmed().isEmpty())
    return;
  QString error;
  if (addRecipe(store_, tool_, name, settings, &error).isEmpty()) {
    QMessageBox::warning(this, tr("Save as study recipe"), error);
    return;
  }
  QMessageBox::information(
      this, tr("Save as study recipe"),
      tr("\"%1\" is now a recipe of the study. Run it on the cases from the "
         "Runs page.")
          .arg(name.trimmed()));
}

void StudyInputsBar::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refill();
  }
  QFrame::changeEvent(e);
}

void StudyInputsBar::retranslate() {
  caseLabel_->setText(tr("Case"));
  stationLabel_->setText(tr("Station"));
  recipe_->setText(tr("Save as study recipe…"));
  ui::setHelp(use_,
              tr("Ticked, this page takes the earthquake catalog, its time "
                 "offset and the chosen case's place and station from the "
                 "open study, and follows the study when it changes. "
                 "Unticked, the page keeps its own inputs."));
  ui::setHelp(case_, tr("The case of the study whose place and dates fill "
                        "this page."));
  ui::setHelp(station_, tr("The station of that case whose code, position "
                           "and waveform folder fill this page."));
  recipe_->setToolTip(ui::helpText(
      tr("Saves this page's current settings under a name in the study, so "
         "the same analysis can be run on every case from the Runs page.")));
}

} // namespace skg
