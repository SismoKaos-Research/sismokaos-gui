#include "modules/classification/settingspane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

namespace skg {

using sfc::Settings;

SettingsPane::SettingsPane(std::shared_ptr<ClassificationState> state,
                           QWidget *parent)
    : QWidget(parent), state_(std::move(state)) {
  auto addRow = [this](QFormLayout *form, const char *source, QWidget *field) {
    auto *label = new QLabel(this);
    form->addRow(label, field);
    rows_.append({label, source});
  };
  // --- event and classes
  // ------------------------------------------------------
  auto *eventBox = new QGroupBox(tr("Event and labels"), this);
  auto *ef = new QFormLayout(eventBox);
  station_ = new QLineEdit(this);
  eventName_ = new QLineEdit(this);
  eqTime_ = new QDateTimeEdit(this);
  eqTime_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
  eqTime_->setTimeZone(QTimeZone::utc());
  eqTime_->setCalendarPopup(true);
  classTable_ = new QTableWidget(0, 5, this);
  classTable_->setHorizontalHeaderLabels(
      {tr("Label"), tr("First day"), tr("Last day"), tr("Step"), tr("Days")});
  classTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  classTable_->verticalHeader()->hide();
  classTable_->setMaximumHeight(150);
  addRow_ = new QPushButton(this);
  removeRow_ = new QPushButton(this);
  auto *rowButtons = new QHBoxLayout;
  rowButtons->addWidget(addRow_);
  rowButtons->addWidget(removeRow_);
  rowButtons->addStretch(1);
  labelMerge_ = new QLineEdit(this);
  classMonths_ = new QLineEdit(this);
  addRow(ef, QT_TR_NOOP("Station"), station_);
  addRow(ef, QT_TR_NOOP("Event name"), eventName_);
  addRow(ef, QT_TR_NOOP("Event time (UTC)"), eqTime_);
  addRow(ef, QT_TR_NOOP("Class table"), classTable_);
  ef->addRow(QString(), new QWidget(this));
  ef->itemAt(ef->rowCount() - 1, QFormLayout::FieldRole)
      ->widget()
      ->setLayout(rowButtons);
  addRow(ef, QT_TR_NOOP("Merge labels into classes"), labelMerge_);
  addRow(ef, QT_TR_NOOP("Months per class"), classMonths_);

  // --- features and split
  // --------------------------------------------------------
  auto *dataBox = new QGroupBox(tr("Features and split"), this);
  auto *df = new QFormLayout(dataBox);
  features_ = new QListWidget(this);
  features_->setMaximumHeight(140);
  scaler_ = new QComboBox(this);
  scaler_->addItems({"robust", "standard", "minmax"});
  clip_ = new QCheckBox(this);
  clipValue_ = new QDoubleSpinBox(this);
  clipValue_->setRange(0.1, 1000);
  clipValue_->setValue(5);
  auto *clipRow = new QWidget(this);
  {
    auto *l = new QHBoxLayout(clipRow);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(clip_);
    l->addWidget(clipValue_);
    l->addStretch(1);
  }
  trainSize_ = new QDoubleSpinBox(this);
  trainSize_->setRange(0.05, 0.9);
  trainSize_->setSingleStep(0.05);
  valSize_ = new QDoubleSpinBox(this);
  valSize_->setRange(0.05, 0.9);
  valSize_->setSingleStep(0.05);
  seed_ = new QSpinBox(this);
  seed_->setRange(0, 1000000);
  addRow(df, QT_TR_NOOP("Feature columns"), features_);
  addRow(df, QT_TR_NOOP("Scaler"), scaler_);
  addRow(df, QT_TR_NOOP("Clip after scaling"), clipRow);
  addRow(df, QT_TR_NOOP("Train share of days"), trainSize_);
  addRow(df, QT_TR_NOOP("Validation share of days"), valSize_);
  addRow(df, QT_TR_NOOP("Random seed"), seed_);

  // --- models
  // ----------------------------------------------------------------------
  auto *modelBox = new QGroupBox(tr("Models and tuning"), this);
  auto *mf = new QFormLayout(modelBox);
  runName_ = new QLineEdit(this);
  auto *modelsRow = new QWidget(this);
  {
    auto *l = new QHBoxLayout(modelsRow);
    l->setContentsMargins(0, 0, 0, 0);
    logistic_ = new QCheckBox("logistic", this);
    svmLinear_ = new QCheckBox("svm_linear", this);
    svmPoly_ = new QCheckBox("svm_poly", this);
    svmRbf_ = new QCheckBox("svm_rbf", this);
    for (auto *c : {logistic_, svmLinear_, svmPoly_, svmRbf_})
      l->addWidget(c);
    l->addStretch(1);
  }
  auto *treesRow = new QWidget(this);
  {
    auto *l = new QHBoxLayout(treesRow);
    l->setContentsMargins(0, 0, 0, 0);
    randomForest_ = new QCheckBox("random_forest", this);
    gradientBoosting_ = new QCheckBox("gradient_boosting", this);
    xgboost_ = new QCheckBox("xgboost", this);
    for (auto *c : {randomForest_, gradientBoosting_, xgboost_})
      l->addWidget(c);
    l->addStretch(1);
  }
  balanced_ = new QCheckBox(this);
  selectBy_ = new QComboBox(this);
  selectBy_->addItems({"f1_macro", "accuracy"});
  cv_ = new QCheckBox(this);
  cvFolds_ = new QSpinBox(this);
  cvFolds_->setRange(2, 20);
  cvFolds_->setValue(5);
  auto *cvRow = new QWidget(this);
  {
    auto *l = new QHBoxLayout(cvRow);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(cv_);
    l->addWidget(cvFolds_);
    l->addStretch(1);
  }
  saveModels_ = new QCheckBox(this);
  annEpochs_ = new QSpinBox(this);
  annEpochs_->setRange(1, 10000);
  annPatience_ = new QSpinBox(this);
  annPatience_->setRange(1, 1000);
  annBalance_ = new QCheckBox(this);
  addRow(mf, QT_TR_NOOP("Run name"), runName_);
  addRow(mf, QT_TR_NOOP("Baseline models"), modelsRow);
  addRow(mf, QT_TR_NOOP("Tree models"), treesRow);
  mf->addRow(QString(), balanced_);
  addRow(mf, QT_TR_NOOP("Select the best by"), selectBy_);
  addRow(mf, QT_TR_NOOP("Cross-validation"), cvRow);
  mf->addRow(QString(), saveModels_);
  addRow(mf, QT_TR_NOOP("Network: max epochs"), annEpochs_);
  addRow(mf, QT_TR_NOOP("Network: patience"), annPatience_);
  mf->addRow(QString(), annBalance_);

  auto *formWidget = new QWidget(this);
  auto *fl = new QVBoxLayout(formWidget);
  fl->setContentsMargins(0, 0, 0, 0);
  fl->addWidget(eventBox);
  fl->addWidget(dataBox);
  fl->addWidget(modelBox);
  fl->addStretch(1);
  auto *scroll = new QScrollArea(this);
  scroll->setWidget(formWidget);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);

  // --- TOML
  // -----------------------------------------------------------------------
  toml_ = new QPlainTextEdit(this);
  toml_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  applyToml_ = new QPushButton(this);
  tomlNote_ = new QLabel(this);
  tomlNote_->setWordWrap(true);
  auto *right = new QWidget(this);
  auto *rl = new QVBoxLayout(right);
  rl->setContentsMargins(0, 0, 0, 0);
  rl->addWidget(tomlNote_);
  rl->addWidget(toml_, 1);
  auto *tb = new QHBoxLayout;
  tb->addStretch(1);
  tb->addWidget(applyToml_);
  rl->addLayout(tb);
  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(scroll);
  split->addWidget(right);
  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 2);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(split);

  connect(addRow_, &QPushButton::clicked, this, [this] {
    const int r = classTable_->rowCount();
    classTable_->insertRow(r);
    for (int c = 0; c < 5; ++c)
      classTable_->setItem(
          r, c, new QTableWidgetItem(c == 0 ? QString::number(r) : QString()));
    emit edited();
  });
  connect(removeRow_, &QPushButton::clicked, this, [this] {
    if (classTable_->currentRow() >= 0)
      classTable_->removeRow(classTable_->currentRow());
    emit edited();
  });
  connect(applyToml_, &QPushButton::clicked, this, [this] {
    QString err;
    if (!applyToml(&err))
      tomlNote_->setText(tr("Not applied: %1").arg(err));
    else {
      refresh();
      emit edited();
    }
  });
  for (auto *w : {logistic_, svmLinear_, svmPoly_, svmRbf_, randomForest_,
                  gradientBoosting_, xgboost_, balanced_, cv_, clip_,
                  saveModels_, annBalance_})
    connect(w, &QCheckBox::toggled, this, [this] {
      if (!filling_)
        emit edited();
    });
  connect(cv_, &QCheckBox::toggled, cvFolds_, &QWidget::setEnabled);
  connect(clip_, &QCheckBox::toggled, clipValue_, &QWidget::setEnabled);
  for (auto *w : {runName_, labelMerge_, classMonths_, station_, eventName_})
    connect(w, &QLineEdit::editingFinished, this, [this] {
      if (!filling_)
        emit edited();
    });
  retranslate();
}

void SettingsPane::refresh() {
  filling_ = true;
  const Settings &s = state_->settings;
  station_->setText(s.station);
  eventName_->setText(s.earthquakeName);
  eqTime_->setDateTime(
      QDateTime(s.eqDate, QTime(s.eqCutoffHour, 0), QTimeZone::utc()));
  classTable_->setRowCount(0);
  for (const sfc::ClassRow &r : s.classTable) {
    const int row = classTable_->rowCount();
    classTable_->insertRow(row);
    classTable_->setItem(row, 0,
                         new QTableWidgetItem(QString::number(r.label)));
    classTable_->setItem(row, 1,
                         new QTableWidgetItem(QString::number(r.firstDay)));
    classTable_->setItem(row, 2,
                         new QTableWidgetItem(QString::number(r.lastDay)));
    classTable_->setItem(row, 3,
                         new QTableWidgetItem(r.step ? QString::number(*r.step)
                                                     : QStringLiteral("none")));
    classTable_->setItem(row, 4,
                         new QTableWidgetItem(QString::number(r.count)));
  }
  QStringList merge;
  if (s.labelMerge)
    for (const sfc::ClassRow &r : s.classTable)
      merge << QString::number(s.labelMerge->value(r.label));
  labelMerge_->setText(merge.join(','));
  QStringList months;
  for (auto it = s.classMonths.begin(); it != s.classMonths.end(); ++it) {
    QStringList m;
    for (int mo : it.value())
      m << QString::number(mo);
    months << QStringLiteral("%1: %2").arg(it.key()).arg(m.join(' '));
  }
  classMonths_->setText(months.join("; "));
  // feature columns: the source's numeric columns, the settings' ones checked
  features_->clear();
  QStringList candidates = state_->tableColumns;
  if (state_->dataset)
    for (const QString &c :
         state_->dataset->featureNames + state_->dataset->extraColumns)
      if (!candidates.contains(c))
        candidates << c;
  for (const QString &f : s.featureColumns)
    if (!candidates.contains(f))
      candidates << f;
  for (const QString &c : candidates) {
    auto *item = new QListWidgetItem(c, features_);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(s.featureColumns.contains(c) ? Qt::Checked
                                                     : Qt::Unchecked);
  }
  scaler_->setCurrentText(s.scaler);
  clip_->setChecked(s.clipAfterScaling.has_value());
  clipValue_->setEnabled(s.clipAfterScaling.has_value());
  if (s.clipAfterScaling)
    clipValue_->setValue(*s.clipAfterScaling);
  trainSize_->setValue(s.trainSize);
  valSize_->setValue(s.valSize);
  seed_->setValue(s.randomSeed);
  runName_->setText(s.runName);
  logistic_->setChecked(s.modelsToRun.contains("logistic"));
  svmLinear_->setChecked(s.modelsToRun.contains("svm_linear"));
  svmPoly_->setChecked(s.modelsToRun.contains("svm_poly"));
  svmRbf_->setChecked(s.modelsToRun.contains("svm_rbf"));
  randomForest_->setChecked(s.treeModelsToRun.contains("random_forest"));
  gradientBoosting_->setChecked(
      s.treeModelsToRun.contains("gradient_boosting"));
  xgboost_->setChecked(s.treeModelsToRun.contains("xgboost"));
  balanced_->setChecked(s.classWeight.has_value());
  selectBy_->setCurrentText(s.selectBy);
  cv_->setChecked(s.cvFolds.has_value());
  cvFolds_->setEnabled(s.cvFolds.has_value());
  if (s.cvFolds)
    cvFolds_->setValue(*s.cvFolds);
  saveModels_->setChecked(s.saveModels);
  annEpochs_->setValue(s.annMaxEpochs);
  annPatience_->setValue(s.annPatience);
  annBalance_->setChecked(s.annBalanceClasses);
  showToml();
  filling_ = false;
}

void SettingsPane::showToml() {
  toml_->setPlainText(state_->settings.toToml());
}

bool SettingsPane::applyToml(QString *error) {
  try {
    state_->settings = Settings::fromToml(toml_->toPlainText());
    return true;
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
}

bool SettingsPane::apply(QString *error) {
  Settings s = state_->settings;
  s.station = station_->text().trimmed().toUpper();
  s.earthquakeName = eventName_->text().trimmed();
  const QDateTime t = eqTime_->dateTime().toUTC();
  s.eqDate = t.date();
  s.eqCutoffHour = t.time().hour();
  s.classTable.clear();
  for (int r = 0; r < classTable_->rowCount(); ++r) {
    auto cell = [&](int c) {
      return classTable_->item(r, c) ? classTable_->item(r, c)->text().trimmed()
                                     : QString();
    };
    bool ok1, ok2, ok3, ok5;
    sfc::ClassRow row{cell(0).toInt(&ok1), cell(1).toInt(&ok2),
                      cell(2).toInt(&ok3), std::nullopt, cell(4).toInt(&ok5)};
    if (!(ok1 && ok2 && ok3 && ok5)) {
      if (error)
        *error = tr("class table row %1: label, first day, last day and days "
                    "must be numbers")
                     .arg(r + 1);
      return false;
    }
    if (cell(3) != QLatin1String("none") && !cell(3).isEmpty())
      row.step = cell(3).toInt();
    s.classTable << row;
  }
  const QString merge = labelMerge_->text().trimmed();
  if (merge.isEmpty())
    s.labelMerge.reset();
  else {
    const QStringList parts = merge.split(',', Qt::SkipEmptyParts);
    if (parts.size() != s.classTable.size()) {
      if (error)
        *error =
            tr("merge: one class per label of the class table (%1 numbers)")
                .arg(s.classTable.size());
      return false;
    }
    QMap<int, int> m;
    for (int i = 0; i < parts.size(); ++i)
      m.insert(s.classTable[i].label, parts[i].trimmed().toInt());
    s.labelMerge = m;
  }
  s.classMonths.clear();
  for (const QString &part :
       classMonths_->text().split(';', Qt::SkipEmptyParts)) {
    const QStringList kv = part.split(':');
    if (kv.size() != 2)
      continue;
    QList<int> months;
    for (const QString &m :
         kv[1].split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts))
      months << m.toInt();
    s.classMonths.insert(kv[0].trimmed().toInt(), months);
  }
  QStringList features;
  for (int i = 0; i < features_->count(); ++i)
    if (features_->item(i)->checkState() == Qt::Checked)
      features << features_->item(i)->text();
  if (features.isEmpty()) {
    if (error)
      *error = tr("choose at least one feature column");
    return false;
  }
  s.featureColumns = features;
  s.scaler = scaler_->currentText();
  s.clipAfterScaling = clip_->isChecked()
                           ? std::optional<double>(clipValue_->value())
                           : std::nullopt;
  s.trainSize = trainSize_->value();
  s.valSize = valSize_->value();
  s.randomSeed = seed_->value();
  s.runName = runName_->text().trimmed().isEmpty() ? QStringLiteral("run_01")
                                                   : runName_->text().trimmed();
  s.modelsToRun.clear();
  for (auto *c : {logistic_, svmLinear_, svmPoly_, svmRbf_})
    if (c->isChecked())
      s.modelsToRun << c->text();
  s.treeModelsToRun.clear();
  for (auto *c : {randomForest_, gradientBoosting_, xgboost_})
    if (c->isChecked())
      s.treeModelsToRun << c->text();
  s.classWeight = balanced_->isChecked()
                      ? std::optional<QString>(QStringLiteral("balanced"))
                      : std::nullopt;
  s.selectBy = selectBy_->currentText();
  s.cvFolds =
      cv_->isChecked() ? std::optional<int>(cvFolds_->value()) : std::nullopt;
  s.saveModels = saveModels_->isChecked();
  s.annMaxEpochs = annEpochs_->value();
  s.annPatience = annPatience_->value();
  s.annBalanceClasses = annBalance_->isChecked();
  try {
    s.labelToClass();
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
  state_->settings = s;
  showToml();
  return true;
}

void SettingsPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void SettingsPane::retranslate() {
  for (const auto &r : rows_)
    r.first->setText(tr(r.second));
  addRow_->setText(tr("Add label"));
  removeRow_->setText(tr("Remove label"));
  labelMerge_->setPlaceholderText(
      tr("class per label, e.g. 0,1,1,2; empty = no merging"));
  classMonths_->setPlaceholderText(
      tr("e.g. 1: 10 11 12 1 2 3; empty = all months"));
  balanced_->setText(tr("balanced class weights"));
  cv_->setText(tr("k-fold over train + validation days"));
  clip_->setText(tr("limit scaled values to ±"));
  saveModels_->setText(tr("save the trained models"));
  annBalance_->setText(tr("network: oversample the rarer classes"));
  applyToml_->setText(tr("Apply TOML"));
  tomlNote_->setText(tr("Every setting as TOML, the layout of sfc's "
                        "project.toml. Grids and the scatter lists are edited "
                        "here; the form covers the rest."));

  ui::setHelp(station_, tr("The station code. It names the results folder "
                           "and the labelled table."));
  ui::setHelp(eventName_, tr("A short name for the earthquake. It names "
                             "the results folder."));
  ui::setHelp(eqTime_,
              tr("The earthquake's origin time in UTC. Days are counted "
                 "back from it, and on the event day only the hours before "
                 "it are used."));
  classTable_->setToolTip(ui::helpText(
      tr("How the days before the event become classes. Each row is one "
         "label: the range of days before the event (first to last), the "
         "step (1 = every day, 2 = every second day) and how many days "
         "are taken. A planned day without data is replaced by the "
         "nearest day that has some.")));
  if (QLabel *l = ui::helpLabel(classTable_))
    l->setToolTip(classTable_->toolTip());
  addRow_->setToolTip(
      ui::helpText(tr("Adds a label row to the class table.")));
  removeRow_->setToolTip(
      ui::helpText(tr("Removes the selected label row.")));
  ui::setHelp(labelMerge_,
              tr("Joins labels into fewer classes: one class number per "
                 "label, in label order. 0,1,1,2 puts labels 1 and 2 into "
                 "one class. Empty keeps every label a class of its own."));
  ui::setHelp(classMonths_,
              tr("A seasonal control: restricts a class to windows "
                 "recorded in the listed calendar months, written as "
                 "class: months, e.g. 1: 10 11 12 1 2 3. Empty uses all "
                 "months."));
  features_->setToolTip(ui::helpText(
      tr("The columns of the table given to the models. Tick the ones to "
         "use; the list is the source's numeric columns.")));
  if (QLabel *l = ui::helpLabel(features_))
    l->setToolTip(features_->toolTip());
  ui::setHelp(scaler_,
              tr("How feature values are brought to a common scale, fitted "
                 "on the training days only: robust (median and "
                 "quartiles, least affected by outliers), standard (mean "
                 "and standard deviation) or minmax (smallest to "
                 "largest)."));
  ui::setHelp({clip_, clipValue_},
              tr("After scaling, cuts every value off at plus and minus "
                 "this limit, so that a few extreme windows do not "
                 "dominate."));
  ui::setHelp(trainSize_,
              tr("Share of each label's days used to train the models "
                 "(0 to 1). Whole days go to one set, never single "
                 "windows."));
  ui::setHelp(valSize_,
              tr("Share of each label's days used to choose parameters and "
                 "compare models. What remains after training and "
                 "validation is the test set."));
  ui::setHelp(seed_, tr("Starting value of the random numbers (the split "
                        "of days, the models). The same seed repeats the "
                        "same run."));
  ui::setHelp(runName_,
              tr("Names this experiment; its files go to models/<run name>. "
                 "Running a name again adds rows to experiments.csv and "
                 "the latest per model counts."));
  const QString baseline =
      tr("Baseline models trained in the baseline step: logistic "
         "regression and support vector machines with a linear, "
         "polynomial or RBF kernel. Tick the ones to train.");
  ui::setHelp({logistic_, svmLinear_, svmPoly_, svmRbf_}, baseline);
  const QString trees =
      tr("Tree models trained in the trees step: random forest, gradient "
         "boosting and XGBoost. Tick the ones to train.");
  ui::setHelp({randomForest_, gradientBoosting_, xgboost_}, trees);
  ui::setHelp(balanced_,
              tr("Weights each class inversely to its number of windows, "
                 "so that a class with few windows counts as much as a "
                 "large one."));
  ui::setHelp(selectBy_,
              tr("The score that picks the best parameters of each model: "
                 "f1_macro (F1 averaged over the classes, each class "
                 "counting the same) or accuracy (share of windows "
                 "classified correctly)."));
  ui::setHelp({cv_, cvFolds_},
              tr("Tunes parameters by k-fold cross-validation over the "
                 "training and validation days together (the number is k, "
                 "the number of folds) instead of the fixed validation "
                 "days. Slower, steadier."));
  ui::setHelp(saveModels_, tr("Also writes the trained models into the "
                              "run's folder. Unticked, only the scores, "
                              "reports and figures are kept."));
  ui::setHelp(annEpochs_, tr("The neural network: the most passes over the "
                             "training data in one training."));
  ui::setHelp(annPatience_,
              tr("The neural network: training stops early when the "
                 "result on the validation days has not improved for this many "
                 "epochs."));
  ui::setHelp(annBalance_,
              tr("The neural network: repeats windows of the rarer classes "
                 "in the training data until the classes are the same "
                 "size."));
  ui::setHelp(toml_,
              tr("Every setting as text in the layout of sfc's "
                 "project.toml. Edit the parameter grids and the scatter "
                 "lists here, then Apply TOML."));
  applyToml_->setToolTip(ui::helpText(
      tr("Reads the text on the left into the form. An error in the text "
         "is reported and nothing changes.")));
}

} // namespace skg
