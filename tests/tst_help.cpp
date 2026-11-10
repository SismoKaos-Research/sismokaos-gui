// Hover help is complete: every option a user can set, on every page and in
// the dialogs, has a tooltip, and so does its label. Offenders are listed.
// With SKG_SHOTS set to a folder, a tooltip is also drawn there in the
// current theme (SKG_SHOTS_THEME=dark for the dark one).
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSettings>
#include <QAbstractItemView>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QToolTip>
#include <QtTest>
#include <memory>

#include "core/i18n.h"
#include "core/module.h"
#include "core/moduleregistry.h"
#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "modules/analysis/analysismodule.h"
#include "modules/classification/classificationmodule.h"
#include "modules/earlywarning/earlywarningmodule.h"
#include "modules/features/featuresmodule.h"
#include "modules/indicators/indicatorsmodule.h"
#include "modules/settings/settingsmodule.h"
#include "modules/study/casedialogs.h"
#include "modules/study/studysetup.h"
#include "modules/study/coveragedialog.h"
#include "modules/study/waveforms.h"
#include "modules/study/runspage.h"
#include "modules/study/studymodule.h"
#include "modules/tdvms/mailboxdialog.h"
#include "modules/tdvms/plandialog.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "study/runs.h"
#include "study/store.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

bool insideItemViewOrCompound(const QWidget *w) {
  // Inner parts of a compound widget (the line edit of a spin box or of an
  // editable combo box, the popup of a combo box) and cell editors of a
  // view are not options of their own.
  for (const QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
    if (qobject_cast<const QAbstractSpinBox *>(p) ||
        qobject_cast<const QComboBox *>(p) ||
        qobject_cast<const QAbstractItemView *>(p))
      return true;
  }
  return false;
}

bool isOption(const QWidget *w) {
  if (insideItemViewOrCompound(w))
    return false;
  if (qobject_cast<const QAbstractSpinBox *>(w) ||
      qobject_cast<const QComboBox *>(w) ||
      qobject_cast<const QCheckBox *>(w) ||
      qobject_cast<const QRadioButton *>(w))
    return true;
  if (auto *le = qobject_cast<const QLineEdit *>(w))
    return !le->isReadOnly();
  if (auto *pe = qobject_cast<const QPlainTextEdit *>(w))
    return !pe->isReadOnly();
  if (auto *te = qobject_cast<const QTextEdit *>(w))
    return !te->isReadOnly();
  if (auto *gb = qobject_cast<const QGroupBox *>(w))
    return gb->isCheckable();
  return false;
}

QString describe(const QWidget *w) {
  QStringList chain;
  for (const QWidget *p = w->parentWidget(); p && chain.size() < 4;
       p = p->parentWidget())
    chain << QString::fromLatin1(p->metaObject()->className());
  QString text;
  if (auto *b = qobject_cast<const QAbstractButton *>(w))
    text = b->text();
  else if (auto *le = qobject_cast<const QLineEdit *>(w))
    text = le->placeholderText();
  const QLabel *label = ui::helpLabel(w);
  return QStringLiteral("%1 name='%2' text='%3' label='%4' in %5")
      .arg(QString::fromLatin1(w->metaObject()->className()), w->objectName(),
           text, label ? label->text() : QStringLiteral("-"),
           chain.join(QStringLiteral(" < ")));
}

// Returns the number of options seen; appends the ones without help.
int audit(const QWidget *root, const QString &where, QStringList *missing) {
  int n = 0;
  for (const QWidget *w : root->findChildren<QWidget *>()) {
    if (!isOption(w))
      continue;
    ++n;
    if (w->toolTip().trimmed().isEmpty())
      missing->append(where + QStringLiteral(": ") + describe(w));
    else if (const QLabel *label = ui::helpLabel(w);
             label && label->toolTip().trimmed().isEmpty())
      missing->append(where + QStringLiteral(": label without help, ") +
                      describe(w));
  }
  return n;
}

} // namespace

class TstHelp : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void helperSetsFieldAndLabel();
  void everyOptionHasHelp();
  void tooltipPicture();

private:
  QTemporaryDir dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<I18n> i18n_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  AppContext ctx_;
};

void TstHelp::initTestCase() {
  initResources();
  settings_ =
      std::make_unique<QSettings>(dir_.filePath("s.ini"), QSettings::IniFormat);
  settings_->setValue(Theme::kSettingMode,
                      qEnvironmentVariable("SKG_SHOTS_THEME") ==
                              QLatin1String("dark")
                          ? QStringLiteral("dark")
                          : QStringLiteral("light"));
  theme_ = std::make_unique<Theme>(settings_.get());
  theme_->apply();
  i18n_ = std::make_unique<I18n>(settings_.get());
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get()};
  const QString folder = dir_.filePath("study");
  QVERIFY(store_->create(folder, QStringLiteral("Help")));
  QVERIFY(
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(folder).filePath("catalog.csv")));
  QVERIFY(store_->update([](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    Case c;
    c.id = QStringLiteral("a");
    c.name = QStringLiteral("A");
    c.time = QDateTime::fromString(QStringLiteral("2019-04-02T14:00:00Z"),
                                   Qt::ISODate);
    c.lat = 38.1228;
    c.lon = 38.5626;
    c.magnitude = 6.1;
    s.cases << c;
    for (const char *tool : {"indicators", "features", "classification"}) {
      Recipe r = defaultRecipe(QLatin1String(tool));
      r.id = QLatin1String(tool);
      r.name = QLatin1String(tool);
      s.recipes << r;
    }
  }));
}

void TstHelp::helperSetsFieldAndLabel() {
  QWidget page;
  auto *form = new QFormLayout(&page);
  auto *edit = new QLineEdit(&page);
  auto *spin = new QSpinBox(&page);
  auto *row = new QHBoxLayout;
  row->addWidget(spin);
  form->addRow(QStringLiteral("Name"), edit);
  form->addRow(QStringLiteral("Count"), row);
  ui::setHelp(edit, QStringLiteral("What it is <called>."));
  ui::setHelp(spin, QStringLiteral("How many."));
  QVERIFY(edit->toolTip().contains(QStringLiteral("&lt;called&gt;")));
  QCOMPARE(edit->property("help").toString(),
           QStringLiteral("What it is <called>."));
  QCOMPARE(edit->whatsThis(), edit->toolTip());
  QVERIFY(ui::helpLabel(edit));
  QCOMPARE(ui::helpLabel(edit)->toolTip(), edit->toolTip());
  // A field in a nested row layout still finds the row's label.
  QVERIFY(ui::helpLabel(spin));
  QCOMPARE(ui::helpLabel(spin)->text(), QStringLiteral("Count"));
  QCOMPARE(ui::helpLabel(spin)->toolTip(), spin->toolTip());
}

void TstHelp::everyOptionHasHelp() {
  ModuleRegistry modules;
  modules.emplace<CasesModule>();
  modules.emplace<RunsModule>();
  modules.emplace<CompareModule>();
  modules.emplace<TdvmsModule>();
  modules.emplace<FeaturesModule>();
  modules.emplace<ClassificationModule>();
  modules.emplace<AnalysisModule>();
  modules.emplace<IndicatorsModule>();
  modules.emplace<EarlyWarningModule>();
  modules.emplace<SettingsModule>(i18n_.get(), theme_.get());

  QStringList missing;
  QWidget holder;
  for (const auto &m : modules.modules()) {
    QWidget *page = m->createWidget(ctx_, &holder);
    QVERIFY(page);
    ui::polishPage(page);
    const int n = audit(page, m->id(), &missing);
    qInfo().noquote() << m->id() << "options:" << n;
  }
  const Study &s = store_->study();
  {
    AddFromCatalogDialog d(s);
    qInfo() << "add-from-catalog options:"
            << audit(&d, QStringLiteral("add-from-catalog"), &missing);
  }
  {
    ReferenceDateDialog d(s);
    qInfo() << "reference-date options:"
            << audit(&d, QStringLiteral("reference-date"), &missing);
  }
  for (const Recipe &r : s.recipes) {
    RecipeDialog d(s, r);
    qInfo().noquote() << "recipe" << r.tool << "options:"
                      << audit(&d, QStringLiteral("recipe ") + r.tool,
                               &missing);
    // Every setting row explains itself too.
    auto *table = d.findChild<QTableWidget *>(QStringLiteral("settingsTable"));
    QVERIFY(table);
    for (int i = 0; i < table->rowCount(); ++i)
      if (table->item(i, 0)->toolTip().trimmed().isEmpty())
        missing << QStringLiteral("recipe %1: setting '%2' has no help")
                       .arg(r.tool, table->item(i, 0)->text());
  }
  {
    WaveScan scan(ctx_);
    WaveformsDialog d(ctx_, &scan);
    qInfo() << "waveforms options:"
            << audit(&d, QStringLiteral("waveforms"), &missing);
  }
  {
    CoverageDialog d(ctx_);
    const int n = audit(&d, QStringLiteral("coverage"), &missing);
    qInfo() << "coverage options:" << n;
    QVERIFY(n >= 3); // which stations, the radius, the magnitude
  }
  {
    MailboxDialog d(jobs_.get());
    qInfo() << "mailbox options:"
            << audit(&d, QStringLiteral("mailbox"), &missing);
  }
  {
    PlanDialog d(dir_.filePath("ledger.tsv"));
    qInfo() << "plan options:" << audit(&d, QStringLiteral("plan"), &missing);
  }
  {
    StudySetupDialog d(ctx_.study);
    qInfo() << "study setup options:"
            << audit(&d, QStringLiteral("study-setup"), &missing);
  }
  {
    CatalogFetchDialog d(s);
    qInfo() << "catalog download options:"
            << audit(&d, QStringLiteral("catalog-download"), &missing);
  }
  for (const QString &line : missing)
    qWarning().noquote() << line;
  QVERIFY2(missing.isEmpty(),
           qPrintable(QStringLiteral("%1 option(s) without hover help")
                          .arg(missing.size())));
}

void TstHelp::tooltipPicture() {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    QSKIP("SKG_SHOTS not set");
  IndicatorsModule module;
  QWidget holder;
  auto *hl = new QVBoxLayout(&holder);
  QWidget *page = module.createWidget(ctx_, &holder);
  hl->addWidget(page);
  ui::polishPage(page);
  holder.resize(1100, 760);
  holder.show();
  QVERIFY(QTest::qWaitForWindowExposed(&holder));
  QWidget *target = nullptr;
  for (QAbstractSpinBox *sb : page->findChildren<QAbstractSpinBox *>())
    if (sb->isVisible() && !sb->toolTip().isEmpty()) {
      target = sb;
      break;
    }
  QVERIFY(target);
  QToolTip::showText(target->mapToGlobal(QPoint(10, target->height())),
                     target->toolTip(), target);
  QWidget *tip = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT(
      [&] {
        for (QWidget *w : QApplication::topLevelWidgets())
          if (w->inherits("QTipLabel") && w->isVisible())
            tip = w;
        return tip != nullptr;
      }(),
      2000);
  QDir().mkpath(dir);
  // The tip is its own window; draw it onto the page where it would appear.
  QPixmap shot = holder.grab();
  QPainter p(&shot);
  const QPoint at = target->mapTo(&holder, QPoint(10, target->height() + 4));
  p.drawPixmap(at, tip->grab());
  p.end();
  QVERIFY(shot.save(QDir(dir).filePath(QStringLiteral("tooltip.png"))));
  QVERIFY(tip->grab().save(QDir(dir).filePath(QStringLiteral("tip_only.png"))));
  QToolTip::hideText();
}

QTEST_MAIN(TstHelp)
#include "tst_help.moc"
