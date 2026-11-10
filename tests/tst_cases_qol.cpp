// The Cases page and its dialogs as a first-time user meets them over a
// long study: several earthquakes added in one go, lists that sort and
// filter (with Turkish letters typed any way), a table that says what each
// case still needs, bulk changes that can be taken back, and files dropped
// on the page.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDateEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QAction>
#include <QDateTimeEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QToolButton>
#include <QUrl>
#include <QtTest>

#include <memory>

#include "core/defaults.h"
#include "core/module.h"
#include "core/theme.h"
#include "jobs/jobmodel.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
#include "study/catalogs.h"
#include "study/runs.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "text/text.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

// The station list embedded in the tdvms library.
static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

QString u(const char *utf8) { return QString::fromUtf8(utf8); }

QDateTime utc(int y, int m, int d, int h = 0, int min = 0) {
  return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::UTC);
}

void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

// One 512-byte miniSEED 2 record of 12 hours at 1 Hz (the header matters).
QByteArray halfDay(const QDateTime &start, const QByteArray &sta,
                   const QByteArray &cha) {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001D ", 8);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData("TU", 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(0) << quint8(0) << quint8(0)
    << quint16(0);
  s << quint16(43200) << qint16(1) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1) << quint8(9)
    << quint8(0);
  return r.leftJustified(512, '\0');
}

QByteArray days(const QByteArray &sta, const QDateTime &first, int count) {
  QByteArray out;
  for (int d = 0; d < count; ++d)
    for (const char *cha : {"HHE", "HHN", "HHZ"})
      for (int half = 0; half < 2; ++half)
        out += halfDay(first.addDays(d).addSecs(half * 43200), sta, cha);
  return out;
}

// A small catalog in the AFAD layout with Turkish place names.
QByteArray turkishCatalog() {
  return u("Date,Longitude,Latitude,Depth,Rms,Type,Magnitude,Location,EventID\n"
           "24/01/2020 17:55:14,39.06,38.36,8.0,0.3,Mw,6.8,Doğanyol (Elazığ),1\n"
           "30/10/2020 11:51:24,26.78,37.89,16.0,0.3,Mw,6.6,Seferihisar (İzmir),2\n"
           "12/06/2019 08:00:00,42.46,37.52,25.0,0.3,ML,5.2,Şırnak,3\n"
           "03/03/2018 03:03:03,44.05,39.92,40.0,0.3,ML,5.0,Iğdır,4\n"
           "05/05/2017 05:05:05,39.75,39.00,5.0,0.3,ML,5.4,ÇEMİŞGEZEK (TUNCELİ),5\n"
           "06/06/2016 06:06:06,38.30,38.35,12.0,0.3,ML,5.1,Malatya,6\n")
      .toUtf8();
}

template <typename T> T *child(QWidget &w, const char *name) {
  return w.findChild<T *>(QString::fromLatin1(name));
}

QStringList column(const QTableWidget *t, int col) {
  QStringList out;
  for (int row = 0; row < t->rowCount(); ++row)
    if (!t->isRowHidden(row))
      out << (t->item(row, col) ? t->item(row, col)->text() : QString());
  return out;
}

void selectRows(QTableWidget *t, const QList<int> &rows) {
  t->clearSelection();
  for (int row : rows)
    t->selectionModel()->select(
        t->model()->index(row, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

// A study of its own with its own settings, so the tests do not see each
// other.
struct Bench {
  QTemporaryDir dir;
  std::unique_ptr<QSettings> settings;
  std::unique_ptr<StudyStore> store;
  Navigator nav;
  AppContext ctx;

  Bench(JobModel *jobs, ToolRegistry *tools, Theme *theme,
        const QByteArray &catalog = {}) {
    settings = std::make_unique<QSettings>(dir.filePath("s.ini"),
                                           QSettings::IniFormat);
    store = std::make_unique<StudyStore>(settings.get());
    ctx = AppContext{jobs, tools, settings.get(), theme, store.get(), &nav};
    const QString folder = dir.filePath("study");
    if (!store->create(folder, QStringLiteral("Bench")))
      qFatal("the bench study could not be made");
    if (catalog.isEmpty()) {
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(folder).filePath("catalog.csv"));
    } else {
      QFile f(QDir(folder).filePath("catalog.csv"));
      if (f.open(QIODevice::WriteOnly))
        f.write(catalog);
    }
    store->update([](Study &s) { s.catalog = QStringLiteral("catalog.csv"); });
  }
  const Study &study() const { return store->study(); }
  QString studyDir() const { return store->study().dir; }

  // Reads the waveform folders into the index, exactly or by names only.
  void scan(bool exact = true) {
    WaveIndex index = WaveIndex::load(studyDir());
    QList<ScanFolder> folders;
    for (const WaveRoot &r : study().waveRoots)
      folders << ScanFolder{r.id, study().resolved(r.path)};
    scanRoots(index, folders, ScanPass::Identity);
    if (exact)
      scanRoots(index, folders, ScanPass::Coverage);
    QVERIFY(index.save(studyDir()));
  }
};

} // namespace

class TstCasesQol : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void foldMatrix();
  void placeSearchFolds();
  void sortFilterAndMore();
  void severalAtOnce();
  void duplicates();
  void keepOpenAndRemembered();
  void spanCoveredFilter();
  void casesTableAtAGlance();
  void bulkChangesAndWaveforms();
  void removeRestoreRenameDuplicate();
  void droppedFiles();
  void referenceDateGuards();
  void stationTableTools();

private:
  std::unique_ptr<Bench> bench(const QByteArray &catalog = {}) {
    return std::make_unique<Bench>(jobs_.get(), tools_.get(), theme_.get(),
                                   catalog);
  }
  // A station of the built-in list near a place.
  StationRef stationNear(const Study &s, double lat, double lon, int nth = 0) {
    const QList<StationRef> near =
        nearestStations(s.stations(), lat, lon, 150.0);
    return nth < near.size() ? near[nth] : StationRef();
  }
  void shot(QWidget &w, const QString &name) {
    const QString dir = qEnvironmentVariable("SKG_SHOTS");
    if (dir.isEmpty())
      return;
    QDir().mkpath(dir);
    w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
  }

  QTemporaryDir dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::function<bool(QWidget *, const QString &, const QString &)> oldConfirm_;
  QStringList asked_; // what CasesPage::confirm was asked
  bool answer_ = true;
};

void TstCasesQol::initTestCase() {
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
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  oldConfirm_ = CasesPage::confirm;
  CasesPage::confirm = [this](QWidget *, const QString &title,
                              const QString &text) {
    asked_ << title + QStringLiteral(": ") + text;
    return answer_;
  };
}

void TstCasesQol::cleanup() {
  asked_.clear();
  answer_ = true;
}

void TstCasesQol::foldMatrix() {
  // Every way of writing the four i's is the same key, on any machine.
  for (const char *izmir : {"İzmir", "IZMIR", "ızmır", "izmir", "İZMİR",
                            "Izmir", "ızmir", "İzmır"})
    QCOMPARE(text::fold(u(izmir)), QStringLiteral("izmir"));
  QCOMPARE(text::fold(u("Doğanyol")), QStringLiteral("doganyol"));
  QCOMPARE(text::fold(u("Şırnak")), QStringLiteral("sirnak"));
  QCOMPARE(text::fold(u("ŞIRNAK")), QStringLiteral("sirnak"));
  QCOMPARE(text::fold(u("Iğdır")), QStringLiteral("igdir"));
  QCOMPARE(text::fold(u("Çüngüş")), QStringLiteral("cungus"));
  QCOMPARE(text::fold(u("ÖDEMİŞ")), QStringLiteral("odemis"));
  QCOMPARE(text::fold(u("As Sulaymānīyah")), QStringLiteral("as sulaymaniyah"));
  QCOMPARE(text::fold(QStringLiteral("MDUR 0212")), QStringLiteral("mdur 0212"));
  QVERIFY(text::foldedContains(u("13 km N of Doğanyol, Turkey"),
                               QStringLiteral("dogan")));
  QVERIFY(text::foldedContains(u("Seferihisar (İzmir)"),
                               QStringLiteral("izmir")));
  QVERIFY(text::foldedContains(QStringLiteral("IZMIR"), u("ızmır")));
  QVERIFY(text::foldedContains(u("Şırnak"), QStringLiteral("SIRNAK")));
  QVERIFY(!text::foldedContains(QStringLiteral("Siirt"),
                                QStringLiteral("sirnak")));
  QVERIFY(text::foldedContains(QStringLiteral("anything"), QStringLiteral(" ")));
}

void TstCasesQol::placeSearchFolds() {
  auto b = bench(turkishCatalog());
  AddFromCatalogDialog dialog(b->study());
  auto *events = child<QTableWidget>(dialog, "eventTable");
  auto *place = child<QLineEdit>(dialog, "placeFilter");
  QVERIFY(events && place);
  QCOMPARE(events->rowCount(), 6);
  const auto found = [&](const char *typed) {
    place->setText(u(typed));
    return column(events, 4);
  };
  QCOMPARE(found("izmir"), QStringList{u("Seferihisar (İzmir)")});
  QCOMPARE(found("IZMIR"), QStringList{u("Seferihisar (İzmir)")});
  QCOMPARE(found("ızmır"), QStringList{u("Seferihisar (İzmir)")});
  QCOMPARE(found("dogan"), QStringList{u("Doğanyol (Elazığ)")});
  QCOMPARE(found("ELAZIG"), QStringList{u("Doğanyol (Elazığ)")});
  QCOMPARE(found("sirnak"), QStringList{u("Şırnak")});
  QCOMPARE(found("igdir"), QStringList{u("Iğdır")});
  QCOMPARE(found("ığdır"), QStringList{u("Iğdır")});
  QCOMPARE(found("cemisgezek"), QStringList{u("ÇEMİŞGEZEK (TUNCELİ)")});
  QCOMPARE(found("tunceli"), QStringList{u("ÇEMİŞGEZEK (TUNCELİ)")});
  QVERIFY(found("ankara").isEmpty());
  place->clear();
  QCOMPARE(events->rowCount(), 6);
  // Sorted by place, the Turkish letters fall where their plain ones do.
  events->horizontalHeader()->sectionClicked(4);
  const QStringList byPlace = column(events, 4);
  QCOMPARE(byPlace.first(), u("ÇEMİŞGEZEK (TUNCELİ)"));
  QCOMPARE(byPlace.last(), u("Şırnak"));
  QVERIFY(byPlace.indexOf(u("Iğdır")) < byPlace.indexOf(u("Malatya")));
}

void TstCasesQol::sortFilterAndMore() {
  auto b = bench();
  AddFromCatalogDialog dialog(b->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = child<QTableWidget>(dialog, "eventTable");
  auto *minMag = child<QDoubleSpinBox>(dialog, "minMag");
  auto *more = child<QPushButton>(dialog, "showMore");
  auto *from = child<QDateEdit>(dialog, "dateFrom");
  auto *to = child<QDateEdit>(dialog, "dateTo");
  auto *depth = child<QDoubleSpinBox>(dialog, "maxDepth");
  QVERIFY(events && minMag && more && from && to && depth);
  // Largest first to start with, as before.
  QCOMPARE(events->rowCount(), 6);
  QCOMPARE(events->item(0, 1)->text(), QStringLiteral("6.4"));
  QVERIFY(!more->isVisible());
  // A click on the time heading: newest first, then oldest first.
  events->horizontalHeader()->sectionClicked(0);
  QStringList times = column(events, 0);
  QStringList sorted = times;
  std::sort(sorted.begin(), sorted.end(), std::greater<QString>());
  QCOMPARE(times, sorted);
  events->horizontalHeader()->sectionClicked(0);
  times = column(events, 0);
  std::sort(sorted.begin(), sorted.end());
  QCOMPARE(times, sorted);
  QCOMPARE(events->horizontalHeader()->sortIndicatorSection(), 0);
  // What is selected stays selected through a new order.
  events->selectRow(0);
  const QString chosen = events->item(0, 0)->text();
  events->horizontalHeader()->sectionClicked(1);
  QCOMPARE(events->selectionModel()->selectedRows().size(), 1);
  QCOMPARE(events->item(events->selectionModel()->selectedRows().first().row(), 0)
               ->text(),
           chosen);
  events->clearSelection();
  // The whole catalog: the list is cut, says so, and can be widened.
  minMag->setValue(0.0);
  const int all = int(study::pickingEvents(b->study()).size());
  QVERIFY(all > 2000);
  QCOMPARE(events->rowCount(), 500);
  QVERIFY(more->isVisible());
  bool says = false;
  for (QLabel *l : dialog.findChildren<QLabel *>())
    says = says || (l->text().contains(QString::number(all)) &&
                    l->text().contains(QStringLiteral("500")));
  QVERIFY(says);
  more->click();
  QCOMPARE(events->rowCount(), 2000);
  more->click();
  QCOMPARE(events->rowCount(), all);
  QVERIFY(!more->isVisible());
  // Dates and depth narrow it.
  from->setDate(QDate(2020, 1, 1));
  const int since2020 = events->rowCount();
  QVERIFY(since2020 > 0 && since2020 < all);
  for (const QString &t : column(events, 0))
    QVERIFY(t >= QStringLiteral("2020-01-01"));
  to->setDate(QDate(2020, 6, 30));
  const int half = events->rowCount();
  QVERIFY(half > 0 && half < since2020);
  for (const QString &t : column(events, 0))
    QVERIFY(t < QStringLiteral("2020-07-01"));
  depth->setValue(5);
  const int shallow = events->rowCount();
  QVERIFY(shallow > 0 && shallow < half);
  depth->setValue(0); // any depth
  QCOMPARE(events->rowCount(), half);
}

void TstCasesQol::severalAtOnce() {
  auto b = bench();
  const std::vector<analysis::Event> all = study::pickingEvents(b->study());
  AddFromCatalogDialog dialog(b->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = child<QTableWidget>(dialog, "eventTable");
  auto *plan = child<QTableWidget>(dialog, "planTable");
  auto *stations = child<QTableWidget>(dialog, "stationTable");
  auto *ok = child<QPushButton>(dialog, "addCases");
  auto *span = child<QSpinBox>(dialog, "spanDays");
  auto *nearest = child<QSpinBox>(dialog, "nearestN");
  auto *name = child<QLineEdit>(dialog, "caseName");
  QVERIFY(events && plan && stations && ok && span && nearest && name);
  QVERIFY(!ok->isEnabled());
  // One chosen: the ticks, as before.
  events->selectRow(0);
  QTRY_VERIFY(stations->isVisible());
  QVERIFY(!plan->isVisible());
  QVERIFY(name->isEnabled());
  QCOMPARE(ok->text(), QStringLiteral("Add case"));
  // Three chosen: one row each, with a name and the rule's stations.
  selectRows(events, {0, 1, 2});
  QTRY_VERIFY(plan->isVisible());
  QVERIFY(!stations->isVisible());
  QVERIFY(!name->isEnabled());
  QCOMPARE(plan->rowCount(), 3);
  QCOMPARE(ok->text(), QStringLiteral("Add 3 case(s)"));
  QVERIFY(ok->isEnabled());
  span->setValue(120);
  nearest->setValue(2);
  for (int row = 0; row < 3; ++row) {
    QVERIFY(!plan->item(row, 1)->text().isEmpty());
    // Nothing has data yet: the two nearest, to be got.
    QCOMPARE(plan->item(row, 2)->text().split(QStringLiteral(", ")).size(), 2);
  }
  shot(dialog, QStringLiteral("add_several"));
  // A row is edited: its name, and its stations typed.
  plan->item(1, 1)->setText(QStringLiteral("The second one"));
  const QString firstCode = plan->item(0, 2)->text().section(',', 0, 0).trimmed();
  plan->item(2, 2)->setText(firstCode.toLower() + QStringLiteral(", NOPE"));
  QCOMPARE(plan->item(1, 1)->text(), QStringLiteral("The second one"));
  QVERIFY(plan->item(2, 3)->text().contains(QStringLiteral("NOPE")));
  // The rule changes: the edited row keeps what was typed.
  nearest->setValue(1);
  QCOMPARE(plan->item(0, 2)->text().split(QStringLiteral(", ")).size(), 1);
  QVERIFY(plan->item(2, 2)->text().startsWith(firstCode));
  dialog.accept();
  const QList<Case> added = dialog.results();
  QCOMPARE(added.size(), 3);
  QCOMPARE(added[1].name, QStringLiteral("The second one"));
  for (const Case &c : added) {
    QCOMPARE(c.spanDays, 120);
    QVERIFY(c.id.isEmpty());
    QCOMPARE(c.stations.size(), 1);
    QVERIFY(c.stations[0].distanceKm >= 0);
  }
  QCOMPARE(added[2].stations[0].code, firstCode);
  QVERIFY(added[0].magnitude >= added[1].magnitude);
}

void TstCasesQol::duplicates() {
  auto b = bench();
  // The largest earthquake of the catalog is already a case.
  std::vector<analysis::Event> all = study::pickingEvents(b->study());
  std::sort(all.begin(), all.end(),
            [](const analysis::Event &x, const analysis::Event &y) {
              return x.mag > y.mag;
            });
  QString existing;
  QVERIFY(b->store->update([&](Study &s) {
    Case c = caseFromEvent(all.front());
    c.id = s.freeCaseId(c.name);
    c.spanDays = 300;
    existing = c.id;
    s.cases << c;
  }));
  AddFromCatalogDialog dialog(b->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = child<QTableWidget>(dialog, "eventTable");
  auto *dup = child<QComboBox>(dialog, "duplicates");
  auto *ok = child<QPushButton>(dialog, "addCases");
  auto *plan = child<QTableWidget>(dialog, "planTable");
  auto *span = child<QSpinBox>(dialog, "spanDays");
  QVERIFY(events && dup && ok && plan && span);
  QVERIFY(!dup->isVisible());
  // Alone, and left out: there is nothing to add, and the button says why.
  events->selectRow(0);
  QTRY_VERIFY(dup->isVisible());
  QVERIFY(!ok->isEnabled());
  QVERIFY(ok->toolTip().contains(QStringLiteral("already a case")));
  // With another one: one is added, the other left out.
  selectRows(events, {0, 1});
  QTRY_COMPARE(plan->rowCount(), 2);
  QVERIFY(plan->item(0, 3)->text().contains(QStringLiteral("left out")));
  QVERIFY(ok->isEnabled());
  QCOMPARE(ok->text(), QStringLiteral("Add case"));
  // Replaced: both, the first under the id it has.
  dup->setCurrentIndex(dup->findData(int(Duplicates::Replace)));
  QVERIFY(plan->item(0, 3)->text().contains(QStringLiteral("replaced")));
  QCOMPARE(ok->text(), QStringLiteral("Add 2 case(s)"));
  span->setValue(100);
  dialog.accept();
  QList<Case> out = dialog.results();
  QCOMPARE(out.size(), 2);
  QCOMPARE(out[0].id, existing);
  QVERIFY(out[1].id.isEmpty());
  // The page replaces the one and adds the other.
  CasesPage page(b->ctx);
  QVERIFY(page.addCases(out));
  QCOMPARE(b->study().cases.size(), 2);
  QCOMPARE(b->study().findCase(existing)->spanDays, 100);
  QCOMPARE(page.selectedIds().size(), 2);
  // Added again: a second case of the same earthquake.
  AddFromCatalogDialog again(b->study());
  auto *events2 = child<QTableWidget>(again, "eventTable");
  auto *dup2 = child<QComboBox>(again, "duplicates");
  events2->selectRow(0);
  dup2->setCurrentIndex(dup2->findData(int(Duplicates::AddAgain)));
  again.accept();
  QCOMPARE(again.results().size(), 1);
  QVERIFY(again.results().first().id.isEmpty());
  QVERIFY(page.addCases(again.results()));
  QCOMPARE(b->study().cases.size(), 3);
  QVERIFY(b->study().cases.last().id != existing);
}

void TstCasesQol::keepOpenAndRemembered() {
  auto b = bench();
  QList<Case> handed;
  {
    AddFromCatalogDialog dialog(b->study(), nullptr, {}, b->settings.get());
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *events = child<QTableWidget>(dialog, "eventTable");
    auto *keep = child<QPushButton>(dialog, "addAndStay");
    auto *minMag = child<QDoubleSpinBox>(dialog, "minMag");
    auto *span = child<QSpinBox>(dialog, "spanDays");
    auto *km = child<QDoubleSpinBox>(dialog, "stationKm");
    auto *added = child<QLabel>(dialog, "addedNote");
    QVERIFY(events && keep && minMag && span && km && added);
    // Nobody to hand the cases to: the button is not offered.
    QVERIFY(!keep->isVisible());
    dialog.setAdder([&](const QList<Case> &cases) {
      handed << cases;
      return b->store->update([&cases](Study &s) {
        for (Case c : cases) {
          c.id = s.freeCaseId(c.name);
          s.cases << c;
        }
      });
    });
    QVERIFY(keep->isVisible());
    QVERIFY(!keep->isEnabled());
    minMag->setValue(5.2);
    span->setValue(200);
    km->setValue(90);
    const int listed = events->rowCount();
    QVERIFY(listed >= 2);
    events->selectRow(0);
    QVERIFY(keep->isEnabled());
    keep->click();
    // Added, and the dialog is still there with its filters, ready for the
    // next one; the one just added is now known as a case.
    QCOMPARE(handed.size(), 1);
    QVERIFY(dialog.isVisible());
    QVERIFY(added->isVisible());
    QVERIFY(added->text().contains(handed.first().name));
    QCOMPARE(events->rowCount(), listed);
    QVERIFY(events->selectionModel()->selectedRows().isEmpty());
    QCOMPARE(b->study().cases.size(), 1);
    events->selectRow(0);
    QTRY_VERIFY(child<QComboBox>(dialog, "duplicates")->isVisible());
    events->selectRow(1);
    keep->click();
    QCOMPARE(handed.size(), 2);
    QCOMPARE(b->study().cases.size(), 2);
    events->horizontalHeader()->sectionClicked(0);
    dialog.resize(1300, 820);
    dialog.reject();
  }
  // The next time in this study: as it was left.
  {
    AddFromCatalogDialog dialog(b->study(), nullptr, {}, b->settings.get());
    QCOMPARE(child<QDoubleSpinBox>(dialog, "minMag")->value(), 5.2);
    QCOMPARE(child<QSpinBox>(dialog, "spanDays")->value(), 200);
    QCOMPARE(child<QDoubleSpinBox>(dialog, "stationKm")->value(), 90.0);
    QCOMPARE(child<QTableWidget>(dialog, "eventTable")
                 ->horizontalHeader()
                 ->sortIndicatorSection(),
             0);
    QCOMPARE(dialog.size(), QSize(1300, 820));
  }
  // Another study starts from the defaults, with the same settings file.
  auto other = bench();
  AddFromCatalogDialog fresh(other->study(), nullptr, {}, b->settings.get());
  QCOMPARE(child<QDoubleSpinBox>(fresh, "minMag")->value(), 5.0);
  QCOMPARE(child<QSpinBox>(fresh, "spanDays")->value(), 365);
}

void TstCasesQol::spanCoveredFilter() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  std::sort(big.begin(), big.end(),
            [](const analysis::Event &x, const analysis::Event &y) {
              return x.epoch < y.epoch;
            });
  QVERIFY(big.size() >= 3);
  // A station of the list near the earliest large earthquake, with data in
  // the weeks before that earthquake only.
  const analysis::Event &first = big.front();
  const StationRef st = stationNear(b->study(), first.lat, first.lon);
  QVERIFY(!st.code.isEmpty());
  const QDateTime t =
      QDateTime::fromSecsSinceEpoch(qint64(first.epoch), QTimeZone::UTC);
  const QDateTime from = QDateTime(t.date().addDays(-20), QTime(0, 0),
                                   QTimeZone::UTC);
  // Named as a downloaded day file is, so its period can be estimated.
  const QString root = b->dir.filePath("waves");
  const QString file =
      QStringLiteral("%1/%2/TU_%2_%3_000000_%4_000000_HH.mseed")
          .arg(root, st.code, from.toString(QStringLiteral("ddMMyyyy")),
               from.addDays(10).toString(QStringLiteral("ddMMyyyy")));
  write(file, days(st.code.toLatin1(), from, 10));
  QVERIFY(b->store->update([&root](Study &s) { s.addWaveformRoot(root); }));

  int nearCount = 0, coveredCount = 0;
  for (const analysis::Event &e : big) {
    const bool near = !analysis::stationsWithin(
                           {analysis::Station{st.code, QString(), st.lat,
                                              st.lon, 0, QString(), QString()}},
                           e.lat, e.lon, defaults::kStationRadiusKm)
                           .empty();
    nearCount += near ? 1 : 0;
    const QDateTime te =
        QDateTime::fromSecsSinceEpoch(qint64(e.epoch), QTimeZone::UTC);
    // In its 365-day span, the ten days of data.
    coveredCount +=
        near && from < te && from.addDays(10) > te.addDays(-365) ? 1 : 0;
  }
  QVERIFY(coveredCount >= 1);
  QVERIFY(coveredCount < int(big.size()));

  for (const bool exact : {false, true}) {
    b->scan(exact);
    AddFromCatalogDialog dialog(b->study());
    auto *events = child<QTableWidget>(dialog, "eventTable");
    auto *covered = child<QCheckBox>(dialog, "spanCovered");
    auto *span = child<QSpinBox>(dialog, "spanDays");
    QVERIFY(events && covered && span);
    QVERIFY(covered->isEnabled());
    // Before the exact scan it goes by the file names, and says so.
    QCOMPARE(covered->text().contains(QStringLiteral("estimated")), !exact);
    QCOMPARE(events->rowCount(), int(big.size()));
    covered->setChecked(true);
    QCOMPARE(events->rowCount(), coveredCount);
    // A span too short to reach the data leaves none of them.
    span->setValue(3);
    QCOMPARE(events->rowCount(), 0);
    span->setValue(365);
    QCOMPARE(events->rowCount(), coveredCount);
    // The covered earthquake starts with the station that covers it.
    events->horizontalHeader()->sectionClicked(0);
    events->horizontalHeader()->sectionClicked(0); // oldest first
    events->selectRow(0);
    auto *stations = child<QTableWidget>(dialog, "stationTable");
    bool ticked = false;
    for (int i = 0; i < stations->rowCount(); ++i)
      ticked = ticked || (stations->item(i, 1)->text() == st.code &&
                          stations->item(i, 0)->checkState() == Qt::Checked);
    QVERIFY(ticked);
  }
  // Without waveforms the filter is not offered.
  auto empty = bench();
  AddFromCatalogDialog none(empty->study());
  QVERIFY(!child<QCheckBox>(none, "spanCovered")->isEnabled());
}

void TstCasesQol::casesTableAtAGlance() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  QVERIFY(big.size() >= 3);
  const StationRef st = stationNear(b->study(), big[0].lat, big[0].lon);
  const StationRef st2 = stationNear(b->study(), big[0].lat, big[0].lon, 1);
  QVERIFY(!st.code.isEmpty() && !st2.code.isEmpty());
  const QDateTime t0 =
      QDateTime::fromSecsSinceEpoch(qint64(big[0].epoch), QTimeZone::UTC);
  const QString root = b->dir.filePath("waves");
  write(root + "/" + st.code + "/a.mseed",
        days(st.code.toLatin1(),
             QDateTime(t0.date().addDays(-30), QTime(0, 0), QTimeZone::UTC),
             6));
  QVERIFY(b->store->update([&](Study &s) {
    s.addWaveformRoot(root);
    Case a = caseFromEvent(big[0]);
    a.id = s.freeCaseId(a.name);
    a.stations << st << st2; // one with data in the span, one without
    s.cases << a;
    Case c = caseFromEvent(big[1]);
    c.id = s.freeCaseId(c.name);
    s.cases << c; // no station
    Case r = referenceCase(QStringLiteral("Quiet time"), utc(2019, 6, 1),
                           big[2].lat, big[2].lon);
    r.id = s.freeCaseId(r.name);
    s.cases << r;
    Recipe ind = defaultRecipe(kToolIndicators);
    ind.name = QStringLiteral("Window");
    ind.id = s.freeRecipeId(ind.name);
    s.recipes << ind;
  }));
  b->scan();
  const QString firstId = b->study().cases[0].id;
  {
    CasesPage page(b->ctx);
    page.resize(1360, 900);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    auto *cases = child<QTableWidget>(page, "caseTable");
    auto *filter = child<QLineEdit>(page, "caseFilter");
    QVERIFY(cases && filter);
    QCOMPARE(cases->columnCount(), 9);
    QCOMPARE(cases->rowCount(), 3);
    // In the study's order until a heading is clicked.
    QCOMPARE(cases->item(0, 0)->data(Qt::UserRole).toString(), firstId);
    // What each case still needs: stations with data in the span of those
    // ticked, and runs done of those the recipes make.
    QCOMPARE(cases->item(0, 7)->text(), u("⚠ 1 of 2"));
    QCOMPARE(cases->item(1, 7)->text(), u("–"));
    QCOMPARE(cases->item(0, 8)->text(), QStringLiteral("0 of 2"));
    QCOMPARE(cases->item(1, 8)->text(), QStringLiteral("0 of 1"));
    QVERIFY(!cases->item(0, 7)->toolTip().isEmpty());
    shot(page, QStringLiteral("cases_at_a_glance"));
    // Sorted by magnitude, largest first; the reference (no magnitude) last.
    cases->horizontalHeader()->setSortIndicator(3, Qt::DescendingOrder);
    QStringList mags = column(cases, 3);
    QCOMPARE(mags.last(), u("–"));
    QVERIFY(mags[0].toDouble() >= mags[1].toDouble());
    // By the number of days, not by the text of the cell.
    QVERIFY(b->store->update([](Study &s) {
      s.cases[0].spanDays = 90;
      s.cases[1].spanDays = 1000;
      s.cases[2].spanDays = 200;
    }));
    cases->horizontalHeader()->setSortIndicator(6, Qt::AscendingOrder);
    QCOMPARE(column(cases, 6),
             QStringList({"90 day(s)", "200 day(s)", "1000 day(s)"}));
    cases->setColumnWidth(0, 333);
    // The selection follows its case through a new order.
    QCOMPARE(page.selectedIds().size(), 1);
    // The filter: the type, and a name typed without its Turkish letters.
    filter->setText(QStringLiteral("REFERENCE"));
    QCOMPARE(column(cases, 0), QStringList{QStringLiteral("Quiet time")});
    QVERIFY(b->store->update(
        [](Study &s) { s.cases[1].name = u("Şırnak depremi"); }));
    filter->setText(QStringLiteral("sirnak"));
    QCOMPARE(column(cases, 0), QStringList{u("Şırnak depremi")});
    filter->clear();
    QCOMPARE(column(cases, 0).size(), 3);
  }
  // The same study later: the order and the widths as they were left.
  {
    CasesPage page(b->ctx);
    auto *cases = child<QTableWidget>(page, "caseTable");
    QCOMPARE(cases->horizontalHeader()->sortIndicatorSection(), 6);
    QCOMPARE(cases->horizontalHeader()->sortIndicatorOrder(),
             Qt::AscendingOrder);
    QCOMPARE(cases->columnWidth(0), 333);
    QCOMPARE(column(cases, 6),
             QStringList({"90 day(s)", "200 day(s)", "1000 day(s)"}));
  }
}

void TstCasesQol::bulkChangesAndWaveforms() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  const StationRef s0 = stationNear(b->study(), big[0].lat, big[0].lon);
  const StationRef s1 = stationNear(b->study(), big[1].lat, big[1].lon, 1);
  QVERIFY(b->store->update([&](Study &s) {
    for (int i = 0; i < 3; ++i) {
      Case c = caseFromEvent(big[size_t(i)]);
      c.id = s.freeCaseId(c.name);
      c.spanDays = 100 + i;
      if (i == 0)
        c.stations << s0;
      if (i == 1)
        c.stations << s0 << s1;
      s.cases << c;
    }
  }));
  CasesPage page(b->ctx);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *cases = child<QTableWidget>(page, "caseTable");
  auto *name = child<QLineEdit>(page, "caseTitle");
  auto *more = child<QToolButton>(page, "caseMore");
  QVERIFY(cases && name && more && more->menu());
  QCOMPARE(page.selectedIds().size(), 1);
  QVERIFY(name->isEnabled());
  // Two selected: no single case to edit, and the menu has what applies.
  selectRows(cases, {0, 1});
  const QStringList two = page.selectedIds();
  QCOMPARE(two.size(), 2);
  QVERIFY(!name->isEnabled());
  QVERIFY(name->placeholderText().contains(QStringLiteral("2")));
  emit more->menu()->aboutToShow();
  QStringList actions;
  bool duplicateEnabled = true;
  for (QAction *a : more->menu()->actions()) {
    actions << a->text();
    if (a->text() == QLatin1String("Duplicate"))
      duplicateEnabled = a->isEnabled();
  }
  QVERIFY(actions.contains(QStringLiteral("Change the span…")));
  QVERIFY(!duplicateEnabled); // one case at a time
  // The span of both at once.
  QVERIFY(page.setSpanOf(two, 250));
  QCOMPARE(b->study().cases[0].spanDays, 250);
  QCOMPARE(b->study().cases[1].spanDays, 250);
  QCOMPARE(b->study().cases[2].spanDays, 102);
  QCOMPARE(page.selectedIds(), two); // still the two
  // The stations of all three by one rule.
  QStringList three = two;
  three << b->study().cases[2].id;
  QVERIFY(page.applyStationRule(three, 2, 200.0));
  for (const Case &c : b->study().cases) {
    QCOMPARE(c.stations.size(), 2);
    QVERIFY(c.stations[0].distanceKm <= c.stations[1].distanceKm);
    QVERIFY(c.stations[1].distanceKm <= 200.0);
  }
  QVERIFY(page.applyStationRule({three.last()}, 0, 200.0));
  QVERIFY(b->study().cases[2].stations.isEmpty());
  // Their waveforms together: every station once, over all their spans.
  QSignalSpy went(&b->nav, &Navigator::requested);
  page.getWaveformsFor(two);
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("tdvms"));
  const QVariantMap args = went[0][1].toMap();
  QStringList want;
  for (int i = 0; i < 2; ++i)
    for (const StationRef &st : b->study().cases[i].stations)
      if (!want.contains(st.code))
        want << st.code;
  QCOMPARE(args.value("stations").toStringList(), want);
  QCOMPARE(args.value("cases").toStringList(),
           QStringList({b->study().cases[0].name, b->study().cases[1].name}));
  const QDateTime start =
      QDateTime::fromString(args.value("start").toString(), Qt::ISODate);
  const QDateTime end =
      QDateTime::fromString(args.value("end").toString(), Qt::ISODate);
  QCOMPARE(start, std::min(b->study().cases[0].spanStart(),
                           b->study().cases[1].spanStart()));
  QCOMPARE(end, std::max(b->study().cases[0].time, b->study().cases[1].time));
  QVERIFY(!args.contains("lat")); // no single place
  // A selection with no station says so instead of going anywhere.
  went.clear();
  page.getWaveformsFor({three.last()});
  QCOMPARE(went.size(), 0);
  // One case from the menu's point of view is what its own button sends.
  cases->selectRow(0);
  page.getWaveformsFor(page.selectedIds());
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][1].toMap().value("cases").toStringList().size(), 1);
  QVERIFY(went[0][1].toMap().contains("lat"));
}

void TstCasesQol::removeRestoreRenameDuplicate() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  // The latest ones: the catalog has to reach back a span and a window.
  std::sort(big.begin(), big.end(),
            [](const analysis::Event &x, const analysis::Event &y) {
              return x.epoch > y.epoch;
            });
  QVERIFY(b->store->update([&](Study &s) {
    for (int i = 0; i < 3; ++i) {
      Case c = caseFromEvent(big[size_t(i)]);
      c.id = s.freeCaseId(c.name);
      c.spanDays = 270;
      s.cases << c;
    }
    Recipe ind = defaultRecipe(kToolIndicators);
    ind.name = QStringLiteral("Window");
    ind.id = s.freeRecipeId(ind.name);
    ind.settings.insert(QStringLiteral("figures"), false);
    ind.settings.insert(QStringLiteral("window_months"), 6);
    s.recipes << ind;
  }));
  // A finished run of the second case, made for real.
  const Recipe recipe = b->study().recipes.first();
  const Case second = b->study().cases[1];
  try {
    QVERIFY(runCell(b->study(), second, QString(), recipe,
                    QStringLiteral("test"), {}));
  } catch (const std::exception &e) {
    QFAIL(e.what());
  }
  QCOMPARE(cellOf(b->study(), second, QString(), recipe).state,
           CellState::Done);
  const QString runDir = cellOf(b->study(), second, QString(), recipe).dir;
  QVERIFY(QDir(runDir).exists());

  CasesPage page(b->ctx);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *cases = child<QTableWidget>(page, "caseTable");
  auto *name = child<QLineEdit>(page, "caseTitle");
  QVERIFY(cases && name);
  QCOMPARE(cases->item(1, 8)->text(), QStringLiteral("1 of 1"));

  // Renamed: the same case, the same folder, the run still its own.
  cases->selectRow(1);
  name->setText(QStringLiteral("Renamed"));
  emit name->editingFinished();
  QCOMPARE(b->study().cases[1].name, QStringLiteral("Renamed"));
  QCOMPARE(b->study().cases[1].id, second.id);
  QCOMPARE(cellOf(b->study(), b->study().cases[1], QString(), recipe).state,
           CellState::Done);

  // Removed, with the other one: asked first, with what it costs.
  selectRows(cases, {1, 2});
  const QString third = b->study().cases[2].id;
  answer_ = false;
  page.removeCase();
  QCOMPARE(b->study().cases.size(), 3); // declined
  QCOMPARE(asked_.size(), 1);
  QVERIFY(asked_.first().contains(QStringLiteral("Renamed")));
  QVERIFY(asked_.first().contains(QStringLiteral("1 finished run")));
  answer_ = true;
  page.removeCase();
  QCOMPARE(b->study().cases.size(), 1);
  QVERIFY(QDir(runDir).exists()); // nothing on disk is touched
  QCOMPARE(page.removedCases().size(), 2);
  QVERIFY(page.removedCases().contains(QStringLiteral("Renamed")));
  // Put back: where it was, under its id, with its run.
  QVERIFY(page.restoreRemoved(page.removedCases().indexOf("Renamed")));
  QCOMPARE(b->study().cases.size(), 2);
  QCOMPARE(b->study().cases[1].id, second.id);
  QCOMPARE(cellOf(b->study(), b->study().cases[1], QString(), recipe).state,
           CellState::Done);
  QCOMPARE(page.selectedIds(), QStringList{second.id});
  QCOMPARE(page.removedCases().size(), 1);
  QVERIFY(page.restoreRemoved(0));
  QCOMPARE(b->study().cases[2].id, third);
  QVERIFY(page.removedCases().isEmpty());
  QVERIFY(!page.restoreRemoved(0));

  // Duplicated: a case of its own beside the original, to change.
  const QString copy = page.duplicateCase(second.id);
  QVERIFY(!copy.isEmpty() && copy != second.id);
  QCOMPARE(b->study().cases.size(), 4);
  QCOMPARE(b->study().cases[2].id, copy);
  QCOMPARE(b->study().cases[2].time, b->study().cases[1].time);
  QVERIFY(b->study().cases[2].name.contains(QStringLiteral("Renamed")));
  QCOMPARE(page.selectedIds(), QStringList{copy});
  // Its runs are its own: none yet.
  QCOMPARE(cellOf(b->study(), b->study().cases[2], QString(), recipe).state,
           CellState::Missing);

  // The Delete key asks the same question.
  cases->setFocus();
  asked_.clear();
  answer_ = false;
  QTest::keyClick(cases, Qt::Key_Delete);
  QCOMPARE(asked_.size(), 1);
  QCOMPARE(b->study().cases.size(), 4);
  // A removed case belongs to its study: another study has none to put back.
  answer_ = true;
  page.removeCase();
  QCOMPARE(page.removedCases().size(), 1);
}

void TstCasesQol::droppedFiles() {
  auto b = bench();
  CasesPage page(b->ctx);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QVERIFY(page.acceptDrops());
  auto *banner = child<QLabel>(page, "banner");
  QVERIFY(banner);
  const QString before = b->study().catalog;
  // A catalog: what it holds is said before it is taken.
  const QString csv = b->dir.filePath(u("katalog İzmir.csv"));
  write(csv, turkishCatalog());
  const auto drop = [&](const QStringList &paths) {
    QMimeData mime;
    QList<QUrl> urls;
    for (const QString &p : paths)
      urls << QUrl::fromLocalFile(p);
    mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime, Qt::LeftButton,
                          Qt::NoModifier);
    QCoreApplication::sendEvent(&page, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent event(QPointF(40, 40), Qt::CopyAction, &mime, Qt::LeftButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(&page, &event);
  };
  answer_ = false;
  drop({csv});
  QCOMPARE(asked_.size(), 1);
  QVERIFY(asked_.first().contains(QStringLiteral("6 events")));
  QVERIFY(asked_.first().contains(QStringLiteral("2016-06-06")));
  QVERIFY(asked_.first().contains(QStringLiteral("2020-10-30")));
  QCOMPARE(b->study().catalog, before); // declined
  answer_ = true;
  drop({csv});
  QCOMPARE(b->study().catalogPath(), csv);
  QVERIFY(banner->isVisible());
  // Something that is no catalog: said, nothing changed, nothing asked.
  asked_.clear();
  const QString junk = b->dir.filePath("notes.csv");
  write(junk, "just some words\nand more\n");
  drop({junk});
  QVERIFY(asked_.isEmpty());
  QCOMPARE(b->study().catalogPath(), csv);
  QVERIFY(banner->text().contains(QStringLiteral("could not be read")));
  // A folder: a waveform folder, once.
  const QString waves = b->dir.filePath("my waves");
  write(waves + "/ABC/a.mseed", days("ABC", utc(2020, 1, 1), 2));
  drop({waves});
  QCOMPARE(b->study().waveRoots.size(), 1);
  QCOMPARE(b->study().waveformRoots().first(), waves);
  drop({waves});
  QCOMPARE(b->study().waveRoots.size(), 1);
  QVERIFY(banner->text().contains(QStringLiteral("already")));
  // StationXML: the coordinates of its stations.
  const QString xml = b->dir.filePath("stations.xml");
  write(xml, "<?xml version=\"1.0\"?>\n<FDSNStationXML><Network code=\"KO\">"
             "<Station code=\"ABC\"><Latitude>38.5</Latitude>"
             "<Longitude>39.5</Longitude></Station>"
             "</Network></FDSNStationXML>");
  drop({xml});
  QCOMPARE(b->study().extraStations.size(), 1);
  QCOMPARE(b->study().extraStations.first().code, QStringLiteral("ABC"));
  // Anything else is named as not used.
  const QString other = b->dir.filePath("picture.png");
  write(other, "x");
  drop({other});
  QVERIFY(banner->text().contains(QStringLiteral("picture.png")));
  // With no study open nothing is taken.
  b->store->close();
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(csv)});
  QDragEnterEvent enter(QPoint(40, 40), Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
  enter.ignore();
  QCoreApplication::sendEvent(&page, &enter);
  QVERIFY(!enter.isAccepted());
}

void TstCasesQol::referenceDateGuards() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  const analysis::Event &e = big.front();
  const StationRef withData = stationNear(b->study(), e.lat, e.lon);
  const StationRef noData = stationNear(b->study(), e.lat, e.lon, 1);
  const QDateTime t =
      QDateTime::fromSecsSinceEpoch(qint64(e.epoch), QTimeZone::UTC);
  // The first station has data in the month before the earthquake only.
  const QString root = b->dir.filePath("waves");
  write(root + "/" + withData.code + "/a.mseed",
        days(withData.code.toLatin1(),
             QDateTime(t.date().addDays(-30), QTime(0, 0), QTimeZone::UTC), 8));
  QVERIFY(b->store->update([&](Study &s) {
    s.addWaveformRoot(root);
    Case c = caseFromEvent(e);
    c.id = s.freeCaseId(c.name);
    c.spanDays = 60;
    c.stations << withData << noData;
    s.cases << c;
  }));
  b->scan();
  ReferenceDateDialog dialog(b->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *stations = child<QTableWidget>(dialog, "stationTable");
  auto *when = child<QDateTimeEdit>(dialog, "referenceTime");
  auto *warning = child<QLabel>(dialog, "caseWarning");
  auto *place = child<QComboBox>(dialog, "referencePlace");
  QVERIFY(stations && when && warning && place);
  const auto tickedCodes = [&] {
    QStringList out;
    for (int i = 0; i < stations->rowCount(); ++i)
      if (stations->item(i, 0)->checkState() == Qt::Checked)
        out << stations->item(i, 1)->text();
    return out;
  };
  // Two years later: the station with data has none in that span, so it is
  // not ticked to start with; the one nothing is known of is.
  when->setDateTime(t.addYears(2));
  QTRY_COMPARE(tickedCodes(), QStringList{noData.code});
  QCOMPARE(stations->columnCount(), 5); // with what the folders hold
  QVERIFY(warning->isVisible());
  QVERIFY(warning->text().contains(QStringLiteral("not ticked here")));
  // Ticked all the same: said plainly.
  for (int i = 0; i < stations->rowCount(); ++i)
    if (stations->item(i, 1)->text() == withData.code)
      stations->item(i, 0)->setCheckState(Qt::Checked);
  QVERIFY(warning->text().contains(withData.code));
  QVERIFY(warning->text().contains(QStringLiteral("none in the span")));
  // Ten days before the earthquake: its data is in the span, both ticked;
  // and the date lies inside the earthquake's own span, which is said.
  when->setDateTime(t.addDays(-10));
  QTRY_COMPARE(tickedCodes(), QStringList({withData.code, noData.code}));
  QVERIFY(warning->text().contains(QStringLiteral("overlaps")));
  shot(dialog, QStringLiteral("reference_date_guards"));
  dialog.accept();
  QVERIFY(dialog.result().has_value());
  QVERIFY(dialog.result()->reference);
  QCOMPARE(dialog.result()->stations.size(), 2);
  QCOMPARE(dialog.result()->lat, e.lat);
  // Coordinates of its own: the nearest of the list, by the same rule.
  ReferenceDateDialog own(b->study());
  auto *place2 = child<QComboBox>(own, "referencePlace");
  place2->setCurrentIndex(place2->count() - 1);
  auto *stations2 = child<QTableWidget>(own, "stationTable");
  QVERIFY(stations2->rowCount() > 0);
  int ticked = 0;
  for (int i = 0; i < stations2->rowCount(); ++i)
    ticked += stations2->item(i, 0)->checkState() == Qt::Checked ? 1 : 0;
  QCOMPARE(ticked, 3);
  own.accept();
  QCOMPARE(own.result()->stations.size(), 3);
  QCOMPARE(own.result()->lat, defaults::kCentreLat);
}

void TstCasesQol::stationTableTools() {
  auto b = bench();
  std::vector<analysis::Event> big;
  for (const analysis::Event &e : study::pickingEvents(b->study()))
    if (e.mag >= 5.0)
      big.push_back(e);
  const analysis::Event &e = big.front();
  const QDateTime t =
      QDateTime::fromSecsSinceEpoch(qint64(e.epoch), QTimeZone::UTC);
  // The third and fifth nearest stations have data in the span: a week and
  // three weeks of it.
  const StationRef third = stationNear(b->study(), e.lat, e.lon, 2);
  const StationRef fifth = stationNear(b->study(), e.lat, e.lon, 4);
  QVERIFY(!third.code.isEmpty() && !fifth.code.isEmpty());
  const QString root = b->dir.filePath("waves");
  const QDateTime from(t.date().addDays(-40), QTime(0, 0), QTimeZone::UTC);
  write(root + "/" + third.code + "/a.mseed",
        days(third.code.toLatin1(), from, 7));
  write(root + "/" + fifth.code + "/a.mseed",
        days(fifth.code.toLatin1(), from, 21));
  QVERIFY(b->store->update([&](Study &s) {
    s.addWaveformRoot(root);
    Case c = caseFromEvent(e);
    c.id = s.freeCaseId(c.name);
    c.spanDays = 70;
    s.cases << c;
  }));
  b->scan();
  CasesPage page(b->ctx);
  page.resize(1360, 900);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *stations = child<QTableWidget>(page, "stationTable");
  auto *tick = child<QPushButton>(page, "tickNearest");
  auto *untick = child<QPushButton>(page, "untickAll");
  QVERIFY(stations && tick && untick);
  QVERIFY(tick->isEnabled());
  QVERIFY(!untick->isEnabled());
  // The nearest with data in the span, whatever lies nearer without.
  tick->click();
  QStringList used;
  for (const StationRef &st : b->study().cases[0].stations)
    used << st.code;
  QCOMPARE(used, QStringList({third.code, fifth.code}));
  QVERIFY(untick->isEnabled());
  // The bar's share is that of the days: 7 and 21 of 70.
  const auto rowOf = [&](const QString &code) {
    for (int i = 0; i < stations->rowCount(); ++i)
      if (stations->item(i, 1)->text() == code)
        return i;
    return -1;
  };
  QCOMPARE(stations->item(rowOf(third.code), 4)->data(Qt::UserRole + 2).toDouble(),
           0.1);
  QCOMPARE(stations->item(rowOf(fifth.code), 4)->data(Qt::UserRole + 2).toDouble(),
           0.3);
  QVERIFY(stations->item(rowOf(third.code), 4)->text().contains(
      QStringLiteral("7 of 70")));
  // A station nothing is known of has no bar.
  const StationRef nearest = stationNear(b->study(), e.lat, e.lon, 0);
  QVERIFY(!stations->item(rowOf(nearest.code), 4)
               ->data(Qt::UserRole + 2)
               .isValid());
  // By coverage: the fuller one first. By distance: the nearest first.
  stations->horizontalHeader()->sectionClicked(4);
  QCOMPARE(stations->item(0, 1)->text(), fifth.code);
  QCOMPARE(stations->item(1, 1)->text(), third.code);
  stations->horizontalHeader()->sectionClicked(2);
  QCOMPARE(stations->item(0, 1)->text(), nearest.code);
  stations->horizontalHeader()->sectionClicked(2); // back: the ticked first
  QCOMPARE(stations->item(0, 1)->text(), third.code);
  // Nothing more to tick: said, not done silently.
  tick->click();
  QCOMPARE(b->study().cases[0].stations.size(), 2);
  QVERIFY(child<QLabel>(page, "banner")->text().contains(
      QStringLiteral("No further station")));
  untick->click();
  QVERIFY(b->study().cases[0].stations.isEmpty());
  shot(page, QStringLiteral("cases_station_tools"));
}

QTEST_MAIN(TstCasesQol)
#include "tst_cases_qol.moc"
