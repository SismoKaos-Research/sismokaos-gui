// The guided way through the Data page: opened for a case it lists the
// stations with what the study has and what is missing, and one button
// keeps a download for the study, writes the requests for every station,
// registers the folders and starts the transfers. Nothing here touches a
// network: the HTTP layer and the portal run are replaced, and the config
// and state folders are temporary.
#include <QtTest>

#include <QComboBox>
#include <QDataStream>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <memory>
#include <utility>

#include "core/module.h"
#include "core/theme.h"
#include "jobs/jobmodel.h"
#include "modules/tdvms/casedownloads.h"
#include "modules/tdvms/stationmap.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "tdvms/fdsn.h"
#include "tdvms/guide.h"
#include "tdvms/ledger.h"
#include "tdvms/plan.h"
#include "tdvms/settings.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;
using skg::tdvms::Ledger;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

// One 512-byte miniSEED 2 data record: fixed header plus blockette 1000.
QByteArray record(const QDateTime &start, const QByteArray &sta,
                  const QByteArray &net, const QByteArray &cha = "BHZ",
                  int nsamp = 3600, int rate = 1) {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001D ", 8);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData(net.constData(), 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0) << quint16(0);
  s << quint16(nsamp) << qint16(rate) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1) << quint8(9)
    << quint8(0);
  return r.leftJustified(512, '\0');
}

QDateTime utc(const QDate &d) {
  return QDateTime(d, QTime(0, 0), QTimeZone::utc());
}

// An FDSN service: a day of data for whatever is asked, except the days
// listed in `broken`, which answer 500.
class FakeHttp : public tdvms::Http {
public:
  explicit FakeHttp(std::shared_ptr<QStringList> broken,
                    std::shared_ptr<QList<QUrl>> asked)
      : broken_(std::move(broken)), asked_(std::move(asked)) {}
  tdvms::HttpResult get(const QUrl &url, int) override {
    asked_->append(url);
    const QUrlQuery q(url);
    const QString day = q.queryItemValue("starttime").left(10);
    tdvms::HttpResult r;
    if (broken_->contains(day)) {
      r.status = 500;
      return r;
    }
    r.status = 200;
    r.body = record(utc(QDate::fromString(day, Qt::ISODate)),
                    q.queryItemValue("station").toLatin1(),
                    q.queryItemValue("network").toLatin1());
    return r;
  }
  tdvms::HttpResult postJson(const QUrl &, const QByteArray &, int,
                             const QMap<QString, QString> &) override {
    return {};
  }
  tdvms::HttpResult postBody(const QUrl &, const QByteArray &, int,
                             const QMap<QString, QString> &) override {
    return {};
  }
  tdvms::HttpResult getToDevice(const QUrl &, QIODevice *, int,
                                const std::function<bool()> &) override {
    return {};
  }

private:
  std::shared_ptr<QStringList> broken_;
  std::shared_ptr<QList<QUrl>> asked_;
};

// With SKG_SHOTS set to a folder, the page is saved there as a picture. The
// mailbox in these tests is an invented one, and it is shown masked anyway.
void shot(QWidget &w, const QString &name) {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  w.resize(1360, 900);
  w.show();
  QTest::qWait(150);
  w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

template <class T> T *child(QWidget &w, const char *name) {
  return w.findChild<T *>(QLatin1String(name));
}

QString cell(QTableWidget *t, const QString &code, int column) {
  for (int i = 0; i < t->rowCount(); ++i)
    if (t->item(i, 0)->data(Qt::UserRole).toString() == code)
      return t->item(i, column) ? t->item(i, column)->text() : QString();
  return QStringLiteral("<no row>");
}

QList<tdvms::CatalogStation> portalList() {
  tdvms::CatalogStation a;
  a.code = QStringLiteral("AAA");
  a.network = QStringLiteral("TU");
  a.latitude = 38.2;
  a.longitude = 38.7;
  a.devices = QStringLiteral("H");
  tdvms::CatalogStation c = a;
  c.code = QStringLiteral("CCC");
  // Listed, but with no instrument: nothing to ask the portal for.
  tdvms::CatalogStation d = a;
  d.code = QStringLiteral("DDD");
  d.devices.clear();
  return {a, c, d};
}

} // namespace

class TstDataGuided : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void init();
  void cleanup();
  void thePiecesOfTheGuide();
  void aCaseListsStationsAndWhatIsMissing();
  void oneClickKeepsADownloadAndAsksForEverything();
  void withoutAMailboxFdsnGoesAheadAndAfadWaits();
  void theAddressIsNeverShownInFull();
  void buttonsThatAreOffSayWhy();
  void aFailedRequestIsNamedAndRetried();
  void anUnfinishedDownloadIsOfferedAgain();
  void theMapAddsStationsToTheList();
  void withoutAStudyThePageIsAsBefore();

private:
  bool makeStudy();
  void askFor(const QStringList &stations);
  CaseDownloads::Hooks hooks(bool mailbox = true);

  QTemporaryDir env_;
  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  std::unique_ptr<Navigator> nav_;
  AppContext ctx_;
  std::shared_ptr<QStringList> broken_;
  std::shared_ptr<QList<QUrl>> asked_;
  std::shared_ptr<QStringList> portalFails_; // chunk ids the fake run fails
  std::shared_ptr<int> portalRuns_;
};

void TstDataGuided::initTestCase() {
  initResources();
  // The mailbox file and the run locks of whoever runs the tests are not
  // this test's business: both folders are temporary.
  QVERIFY(env_.isValid());
  qputenv("XDG_CONFIG_HOME", env_.filePath("config").toLocal8Bit());
  qputenv("XDG_STATE_HOME", env_.filePath("state").toLocal8Bit());
  qputenv("APPDATA", env_.filePath("config").toLocal8Bit());
  qputenv("LOCALAPPDATA", env_.filePath("state").toLocal8Bit());
  for (const char *v : {"TDVMS_IMAP_HOST", "TDVMS_IMAP_USER", "TDVMS_IMAP_PASS",
                        "AFAD_IMAP_HOST", "AFAD_IMAP_USER", "AFAD_IMAP_PASS"})
    qunsetenv(v);
  QVERIFY(tdvms::configDir().startsWith(env_.path()));
  QVERIFY(!tdvms::credentials().complete());
}

void TstDataGuided::init() {
  dir_ = std::make_unique<QTemporaryDir>();
  settings_ = std::make_unique<QSettings>(dir_->filePath("s.ini"),
                                          QSettings::IniFormat);
  settings_->setValue(QStringLiteral("tdvms/root"), dir_->filePath("dl"));
  theme_ = std::make_unique<Theme>(settings_.get());
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  nav_ = std::make_unique<Navigator>();
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get(), nav_.get()};
  broken_ = std::make_shared<QStringList>();
  asked_ = std::make_shared<QList<QUrl>>();
  portalFails_ = std::make_shared<QStringList>();
  portalRuns_ = std::make_shared<int>(0);
  if (!qEnvironmentVariable("SKG_SHOTS").isEmpty()) {
    theme_->setMode(qEnvironmentVariable("SKG_SHOTS_THEME") ==
                            QLatin1String("dark")
                        ? Theme::Mode::Dark
                        : Theme::Mode::Light);
    theme_->apply();
  }
}

void TstDataGuided::cleanup() {
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  store_.reset();
  jobs_.reset();
}

// A study with one case, 2019-03-20 to 2019-04-02 (14 whole days), and a
// waveform folder that holds five of those days for station AAA.
bool TstDataGuided::makeStudy() {
  const QString folder = dir_->filePath("study");
  if (!store_->create(folder, QStringLiteral("Guided")))
    return false;
  const QString root = QDir(folder).filePath("mseed");
  QDir().mkpath(root + "/AAA");
  for (int d : {0, 1, 2, 7, 13}) {
    const QDate day = QDate(2019, 3, 20).addDays(d);
    QFile f(root + "/AAA/" + day.toString(Qt::ISODate) + ".mseed");
    if (!f.open(QIODevice::WriteOnly))
      return false;
    f.write(record(utc(day), "AAA", "TU", "HHZ"));
  }
  QString id;
  if (!store_->update([&](Study &s) {
        id = s.addWaveRoot(root);
        Case c;
        c.id = QStringLiteral("a");
        c.name = QStringLiteral("Case A");
        c.time = QDateTime::fromString(QStringLiteral("2019-04-02T14:00:00Z"),
                                       Qt::ISODate);
        c.lat = 38.1;
        c.lon = 38.5;
        c.magnitude = 6.0;
        c.spanDays = 13;
        s.cases << c;
      }))
    return false;
  WaveIndex index = WaveIndex::load(folder);
  scanRoot(index, id, root, ScanPass::Coverage);
  return index.save(folder) && index.hasStation(QStringLiteral("AAA"));
}

void TstDataGuided::askFor(const QStringList &stations) {
  nav_->show(QStringLiteral("tdvms"),
             {{QStringLiteral("stations"), stations},
              {QStringLiteral("start"), QStringLiteral("2019-03-20T14:00:00Z")},
              {QStringLiteral("end"), QStringLiteral("2019-04-02T14:00:00Z")},
              {QStringLiteral("case"), QStringLiteral("Case A")},
              {QStringLiteral("lat"), 38.1},
              {QStringLiteral("lon"), 38.5},
              {QStringLiteral("zone_km"), 100.0}});
}

// What the panel would reach outside with, replaced: a mailbox that is (or
// is not) set up, an FDSN service in memory, and a portal run that answers
// every open request at once: fetched, or failed when its id is listed.
CaseDownloads::Hooks TstDataGuided::hooks(bool mailbox) {
  CaseDownloads::Hooks h;
  const auto broken = broken_;
  const auto asked = asked_;
  h.http = [broken, asked] { return std::make_unique<FakeHttp>(broken, asked); };
  h.credentials = [mailbox] {
    tdvms::Credentials c;
    if (mailbox) {
      c.host = QStringLiteral("imap.example.org");
      c.user = QStringLiteral("someone@example.org");
      c.password = QStringLiteral("x");
    }
    return c;
  };
  h.address = [mailbox] {
    return mailbox ? QStringLiteral("someone@example.org") : QString();
  };
  h.addressBusy = [](const QString &) { return false; };
  const auto fails = portalFails_;
  const auto runs = portalRuns_;
  h.portalRun = [fails, runs](const tdvms::Campaign &c,
                              const tdvms::RunOptions &,
                              const tdvms::Notify &) -> TaskJob::Work {
    const QString ledgerPath = c.ledgerPath;
    return [ledgerPath, fails, runs](TaskContext &ctx) -> int {
      ++*runs;
      Ledger ledger(ledgerPath);
      for (const tdvms::Row &r : ledger.inState({QStringLiteral("pending")})) {
        const QString cid = tdvms::chunkId(r);
        if (fails->contains(cid))
          ledger.update(cid, {{"state", "failed"},
                              {"note", "portal said no"}});
        else
          ledger.update(cid, {{"state", "fetched"}, {"bytes", 1000.0}});
      }
      ctx.log(QStringLiteral("answered"));
      return 0;
    };
  };
  h.fdsn.retries = 1;
  h.fdsn.pauseMs = 0;
  return h;
}

void TstDataGuided::thePiecesOfTheGuide() {
  using namespace tdvms;
  // Which source: listed with an instrument, the portal; anything else FDSN.
  const auto list = portalList();
  QCOMPARE(int(defaultSource("AAA", list)), int(Source::Portal));
  QCOMPARE(int(defaultSource("aaa", list)), int(Source::Portal));
  QCOMPARE(int(defaultSource("DDD", list)), int(Source::Fdsn));
  QCOMPARE(int(defaultSource("NOPE", list)), int(Source::Fdsn));
  QCOMPARE(portalNetwork("AAA", list), QStringLiteral("TU"));
  QVERIFY(portalNetwork("NOPE", list).isEmpty());

  // An address fit for a screenshot.
  QCOMPARE(maskAddress("someone@example.org"),
           QStringLiteral("s•••@example.org"));
  QVERIFY(!maskAddress("someone@example.org").contains("someone"));
  QCOMPARE(maskAddress(""), QString());
  // The slot's tag says which request, not who: it stays.
  QCOMPARE(maskAddress("someone+a3@example.org"),
           QStringLiteral("s\u2022\u2022\u2022+a3@example.org"));
  QCOMPARE(maskAddresses("slot someone+a3@example.org holds X; "
                         "other.person@mail.example.com idle"),
           QStringLiteral("slot s\u2022\u2022\u2022+a3@example.org holds X; "
                          "o\u2022\u2022\u2022@mail.example.com idle"));
  QCOMPARE(maskAddresses("no address here"), QStringLiteral("no address here"));
  QVERIFY(!maskAddress("notanaddress").contains("notanaddress"));

  // The study's own download: made once, found again.
  const QString study = dir_->filePath("s1");
  QDir().mkpath(study);
  QVERIFY(studyDownload(study, false).ledgerPath.isEmpty());
  const Campaign c = studyDownload(study, true);
  QVERIFY(QFileInfo::exists(c.ledgerPath));
  QVERIFY(c.ledgerPath.startsWith(study + "/downloads/"));
  QCOMPARE(c.name, QStringLiteral("waveforms"));
  QVERIFY(QFileInfo(c.raw).isDir());
  QCOMPARE(studyDownload(study, false).ledgerPath, c.ledgerPath);
  // The expert view finds it as it finds any campaign.
  const QList<Campaign> found = discoverCampaigns(studyDownloadsRoot(study));
  QCOMPARE(found.size(), 1);
  QCOMPARE(found[0].ledgerPath, c.ledgerPath);

  // Requests on a grid from the span's first day, only where days are
  // missing, and never twice.
  const QDate first(2019, 1, 1), end(2019, 3, 1); // 59 days: 21 + 21 + 17
  QList<QDate> missing = {QDate(2019, 1, 5), QDate(2019, 2, 20)};
  auto windows = windowsForDays(first, end, missing, 21);
  QCOMPARE(windows.size(), 2);
  QCOMPARE(windows[0].first, QStringLiteral("2019-01-01T00:00:00"));
  QCOMPARE(windows[0].second, QStringLiteral("2019-01-22T00:00:00"));
  QCOMPARE(windows[1].first, QStringLiteral("2019-02-12T00:00:00"));
  QCOMPARE(windows[1].second, QStringLiteral("2019-03-01T00:00:00"));
  Ledger ledger(c.ledgerPath);
  QCOMPARE(addChunks(ledger, "AAA", windows), 2);
  QCOMPARE(addChunks(ledger, "AAA", windows), 0);
  // What is asked for already is not asked for again; a failed request
  // does not count as asked.
  QVERIFY(daysNotRequested(missing, ledger.rows(), "AAA").isEmpty());
  QCOMPARE(daysNotRequested(missing, ledger.rows(), "BBB"), missing);
  ledger.update(chunkId("AAA", windows[0].first),
                {{"state", "failed"}, {"note", "why"}});
  QCOMPARE(daysNotRequested(missing, ledger.rows(), "AAA"),
           QList<QDate>{QDate(2019, 1, 5)});
  RequestProgress p = progressOf(ledger.rows(), "AAA");
  QCOMPARE(p.total, 2);
  QCOMPARE(p.failed, 1);
  QCOMPARE(p.waiting, 1);
  QCOMPARE(p.failures, QStringList{QStringLiteral("2019-01-01: why")});
  QCOMPARE(stationsOf(ledger.rows()), QStringList{QStringLiteral("AAA")});
  QCOMPARE(retryFailed(ledger, "AAA"), 1);
  QCOMPARE(progressOf(ledger.rows(), "AAA").waiting, 2);
  // A rough size: the fallback before anything was fetched, the average
  // of what was fetched after.
  QCOMPARE(estimateBytes(ledger.rows(), 10, 5), qint64(50));
  ledger.update(chunkId("AAA", windows[0].first),
                {{"state", "fetched"}, {"bytes", 2100.0}});
  QCOMPARE(estimateBytes(ledger.rows(), 10, 5), qint64(1000));
}

void TstDataGuided::aCaseListsStationsAndWhatIsMissing() {
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  QVERIFY(guide);
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  // A study, nothing asked for: one line that says where to start, and the
  // expert view as it always was.
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Idle));
  QVERIFY(!guide->isHidden());
  QVERIFY(child<QLabel>(page, "guideLead")->text().contains("Cases"));
  auto *advanced = child<QPushButton>(page, "dataAdvanced");
  QVERIFY(advanced && advanced->isHidden());

  askFor({QStringLiteral("AAA"), QStringLiteral("BBB")});
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Case));
  QVERIFY(child<QLabel>(page, "guideTitle")->text().contains("Case A"));
  auto *table = child<QTableWidget>(page, "guideStations");
  QVERIFY(table);
  QCOMPARE(table->rowCount(), 2);
  // AAA: five of the fourteen days are in the study; BBB: none.
  QVERIFY2(cell(table, "AAA", 1).startsWith("5 of 14"),
           qPrintable(cell(table, "AAA", 1)));
  QVERIFY2(cell(table, "BBB", 1).startsWith("none of 14"),
           qPrintable(cell(table, "BBB", 1)));
  // The source follows the rule: AAA is on the portal's list, BBB is not.
  auto *fromA = child<QComboBox>(page, "guideFrom_AAA");
  auto *fromB = child<QComboBox>(page, "guideFrom_BBB");
  QVERIFY(fromA && fromB);
  QCOMPARE(fromA->currentData().toInt(), int(tdvms::Source::Portal));
  QCOMPARE(fromB->currentData().toInt(), int(tdvms::Source::Fdsn));
  QVERIFY(!fromA->toolTip().isEmpty());
  // Nine days missing for AAA fit one request of three weeks; BBB needs
  // fourteen days from the FDSN service.
  QVERIFY2(cell(table, "AAA", 3).contains("9 day") &&
               cell(table, "AAA", 3).contains("1 new request"),
           qPrintable(cell(table, "AAA", 3)));
  QVERIFY2(cell(table, "BBB", 3).contains("14"),
           qPrintable(cell(table, "BBB", 3)));
  const QString summary = child<QLabel>(page, "guideSummary")->text();
  QVERIFY2(summary.contains("1 request") && summary.contains("14 day") &&
               summary.contains("Free"),
           qPrintable(summary));
  QVERIFY(child<QPushButton>(page, "guideGo")->isEnabled());
  QVERIFY(guide->blockedReason().isEmpty());
  shot(page, QStringLiteral("guided_case"));
  // Nothing was written by looking.
  QVERIFY(guide->download().ledgerPath.isEmpty());
  QVERIFY(!QFileInfo::exists(store_->study().dir + "/downloads"));

  // The page is the panel and the map; the expert view is one click away.
  auto *tabs = page.findChild<QTabWidget *>();
  auto *bar = child<QWidget>(page, "wantedBar");
  QVERIFY(tabs && bar);
  QVERIFY(bar->isHidden());
  QVERIFY(!advanced->isHidden());
  int shown = 0;
  for (int i = 0; i < tabs->count(); ++i)
    shown += tabs->isTabVisible(i) ? 1 : 0;
  QCOMPARE(shown, 1);
  QCOMPARE(tabs->currentWidget()->objectName(), QStringLiteral("stationMap"));
  QCOMPARE(child<StationPickMap>(page, "stationMap")->selected().size(), 2);
  advanced->setChecked(true);
  shown = 0;
  for (int i = 0; i < tabs->count(); ++i)
    shown += tabs->isTabVisible(i) ? 1 : 0;
  QCOMPARE(shown, tabs->count());
  advanced->setChecked(false);

  // The user knows better: BBB from the portal, AAA left out.
  fromB->setCurrentIndex(fromB->findData(int(tdvms::Source::Portal)));
  emit fromB->activated(fromB->currentIndex());
  fromA = child<QComboBox>(page, "guideFrom_AAA");
  fromA->setCurrentIndex(fromA->findData(int(tdvms::Source::None)));
  emit fromA->activated(fromA->currentIndex());
  QVERIFY2(cell(table, "BBB", 3).contains("1 new request"),
           qPrintable(cell(table, "BBB", 3)));
  QCOMPARE(cell(table, "AAA", 4), QStringLiteral("left out"));

  // Dismissed: the page is as it was, and nothing is chosen on the map.
  child<QPushButton>(page, "guideDismiss")->click();
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Idle));
  QVERIFY(advanced->isHidden());
  QVERIFY(child<StationPickMap>(page, "stationMap")->selected().isEmpty());
}

void TstDataGuided::oneClickKeepsADownloadAndAsksForEverything() {
  QVERIFY(makeStudy());
  const QStringList before = store_->study().waveformRoots();
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  askFor({QStringLiteral("AAA"), QStringLiteral("BBB"), QStringLiteral("CCC")});
  auto *table = child<QTableWidget>(page, "guideStations");
  auto *go = child<QPushButton>(page, "guideGo");
  QCOMPARE(table->rowCount(), 3);
  QVERIFY(go->isEnabled());
  QSignalSpy studySaved(store_.get(), &StudyStore::changed);
  go->click();

  // The download lives in the study, in the layout of any other campaign.
  const tdvms::Campaign c = guide->download();
  QVERIFY(!c.ledgerPath.isEmpty());
  QVERIFY(c.ledgerPath.startsWith(store_->study().dir + "/downloads/"));
  // Its folders are the study's waveform folders now.
  const QStringList roots = store_->study().waveformRoots();
  QCOMPARE(roots.size(), before.size() + 2);
  QVERIFY(roots.contains(c.raw));
  QVERIFY(roots.contains(tdvms::fdsnDefaultOut(c)));
  QVERIFY(studySaved.size() >= 1);

  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  // Every station was asked for in the one step: AAA and CCC at the portal
  // (one request each: the span is under three weeks), BBB day by day.
  const QList<tdvms::Row> portal = Ledger(c.ledgerPath).rows();
  QCOMPARE(tdvms::stationsOf(portal),
           (QStringList{QStringLiteral("AAA"), QStringLiteral("CCC")}));
  QCOMPARE(portal.size(), 2);
  QCOMPARE(tdvms::str(portal[0], "start"),
           QStringLiteral("2019-03-20T00:00:00"));
  QCOMPARE(tdvms::str(portal[0], "end"), QStringLiteral("2019-04-03T00:00:00"));
  const QList<tdvms::Row> fdsn = Ledger(tdvms::fdsnStatePath(c)).rows();
  QCOMPARE(fdsn.size(), 14);
  QCOMPARE(asked_->size(), 14);
  QCOMPARE(QUrlQuery(asked_->first()).queryItemValue("network"),
           QStringLiteral("KO"));
  for (const tdvms::Row &r : fdsn)
    QCOMPARE(tdvms::str(r, "state"), QStringLiteral("fetched"));
  QVERIFY(QDir(tdvms::fdsnDefaultOut(c) + "/BBB").entryList(QDir::Files).size() ==
          14);
  QCOMPARE(*portalRuns_, 1);
  QCOMPARE(tdvms::progressOf(portal, "AAA").fetched, 1);

  // The page says how it ended, per station and as a whole, and offers the
  // way on.
  QTRY_VERIFY(cell(table, "BBB", 4).contains("14 day"));
  QVERIFY2(cell(table, "AAA", 4).contains("1 request"),
           qPrintable(cell(table, "AAA", 4)));
  auto *done = child<QLabel>(page, "guideDone");
  QVERIFY(!done->isHidden());
  QVERIFY2(done->text().startsWith("Finished"), qPrintable(done->text()));
  shot(page, QStringLiteral("guided_finished"));
  QVERIFY(!child<QPushButton>(page, "guideStop")->isEnabled());
  // Nothing more to ask for: the button says so instead of asking twice.
  QVERIFY(!go->isEnabled());
  QVERIFY(!child<QLabel>(page, "guideWhy")->text().isEmpty());
  QCOMPARE(guide->plan().portalRequests, 0);
  QCOMPARE(guide->plan().fdsnDays, 0);

  // The expert view shows the same download.
  auto *campaign = page.findChild<QComboBox *>(QStringLiteral("dataCampaign"));
  QVERIFY(campaign);
  QCOMPARE(campaign->currentData().toString(), c.ledgerPath);
  // The root the user keeps campaigns under is not forgotten for it.
  QCOMPARE(settings_->value("tdvms/root").toString(), dir_->filePath("dl"));

  // The way on: back to the case, or on to the recipes.
  QSignalSpy went(nav_.get(), &Navigator::requested);
  child<QPushButton>(page, "guideCases")->click();
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("cases"));
  QCOMPARE(went[0][1].toMap().value("case").toString(), QStringLiteral("a"));
  child<QPushButton>(page, "guideRecipes")->click();
  QCOMPARE(went[1][0].toString(), QStringLiteral("runs"));
}

void TstDataGuided::withoutAMailboxFdsnGoesAheadAndAfadWaits() {
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks(false));
  guide->setPortalStations(portalList());
  askFor({QStringLiteral("AAA"), QStringLiteral("BBB")});
  auto *note = child<QLabel>(page, "guideMailbox");
  auto *setup = child<QPushButton>(page, "guideMailboxSetup");
  auto *go = child<QPushButton>(page, "guideGo");
  auto *table = child<QTableWidget>(page, "guideStations");
  QVERIFY(!note->isHidden());
  // In plain words: why a mailbox, and what waits without one.
  QVERIFY2(note->text().contains("e-mail") && note->text().contains("wait"),
           qPrintable(note->text()));
  QVERIFY(setup->text().contains("Set up"));
  shot(page, QStringLiteral("guided_no_mailbox"));
  // (The page answers this with its mailbox dialog, which would wait for a
  // person here.)
  QObject::disconnect(guide, &CaseDownloads::mailboxRequested, &page, nullptr);
  QSignalSpy asked(guide, &CaseDownloads::mailboxRequested);
  setup->click();
  QCOMPARE(asked.size(), 1);

  // BBB comes from the FDSN service and needs no mailbox.
  QVERIFY(go->isEnabled());
  go->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  const tdvms::Campaign c = guide->download();
  QCOMPARE(*portalRuns_, 0);
  QCOMPARE(asked_->size(), 14);
  // AAA's request is written and waits.
  QCOMPARE(tdvms::progressOf(Ledger(c.ledgerPath).rows(), "AAA").waiting, 1);
  QTRY_VERIFY2(cell(table, "AAA", 4).contains("waiting for the mailbox"),
               qPrintable(cell(table, "AAA", 4)));
  QVERIFY2(child<QLabel>(page, "guideDone")->text().contains("still open"),
           qPrintable(child<QLabel>(page, "guideDone")->text()));

  // Only AFAD stations left to ask: the button is off and says why.
  QVERIFY(!go->isEnabled());
  QVERIFY2(child<QLabel>(page, "guideWhy")->text().contains("mailbox"),
           qPrintable(child<QLabel>(page, "guideWhy")->text()));
  // With the mailbox set up the same button resumes, and the request that
  // waited is answered; nothing is asked for twice.
  guide->setHooks(hooks(true));
  QVERIFY(go->isEnabled());
  QCOMPARE(go->text(), QStringLiteral("Resume the download"));
  go->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  QCOMPARE(*portalRuns_, 1);
  QCOMPARE(asked_->size(), 14);
  QCOMPARE(Ledger(c.ledgerPath).rows().size(), 1);
  QCOMPARE(tdvms::progressOf(Ledger(c.ledgerPath).rows(), "AAA").fetched, 1);
}

void TstDataGuided::theAddressIsNeverShownInFull() {
  // A mailbox as the user would have saved it, in the temporary config.
  tdvms::Credentials saved;
  saved.host = QStringLiteral("imap.example.org");
  saved.user = QStringLiteral("someone@example.org");
  saved.password = QStringLiteral("x");
  QVERIFY(tdvms::saveCredentials(saved));
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  askFor({QStringLiteral("AAA")});
  page.resize(1300, 860);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  child<QPushButton>(page, "dataAdvanced")->setChecked(true);
  const auto shown = [&page] {
    QStringList all;
    for (const QLabel *l : page.findChildren<QLabel *>())
      all << l->text();
    for (const QLineEdit *e : page.findChildren<QLineEdit *>())
      if (!e->hasFocus())
        all << e->text();
    return all.join('\n');
  };
  QVERIFY(shown().contains("s•••@example.org"));
  QVERIFY2(!shown().contains("someone"), qPrintable(shown()));
  // The address itself is what a run would use.
  QCOMPARE(page.address(), QStringLiteral("someone@example.org"));
  QVERIFY(QFile::remove(tdvms::credentialsPath()));
}

void TstDataGuided::buttonsThatAreOffSayWhy() {
  // The expert view with no campaign: Plan, Ingest, Reset and Fetch are off
  // and one line says why and what to do.
  TdvmsWidget bare(ctx_);
  bare.resize(1200, 800);
  bare.show();
  QVERIFY(QTest::qWaitForWindowExposed(&bare));
  auto *why = child<QLabel>(bare, "dataWhy");
  QVERIFY(why);
  QTRY_VERIFY(!why->isHidden());
  QVERIFY2(why->text().contains("campaign"), qPrintable(why->text()));
  QVERIFY(!child<QPushButton>(bare, "dataPlan")->isEnabled());
  QVERIFY(!child<QPushButton>(bare, "dataFetch")->isEnabled());

  // The guided panel when the study has everything already.
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  nav_->show(QStringLiteral("tdvms"),
             {{QStringLiteral("stations"), QStringList{QStringLiteral("AAA")}},
              {QStringLiteral("start"), QStringLiteral("2019-03-20T00:00:00Z")},
              {QStringLiteral("end"), QStringLiteral("2019-03-22T10:00:00Z")},
              {QStringLiteral("case"), QStringLiteral("Case A")}});
  auto *table = child<QTableWidget>(page, "guideStations");
  QVERIFY2(cell(table, "AAA", 1).startsWith("3 of 3"),
           qPrintable(cell(table, "AAA", 1)));
  QVERIFY(!child<QPushButton>(page, "guideGo")->isEnabled());
  QVERIFY2(child<QLabel>(page, "guideWhy")->text().contains("Nothing to download"),
           qPrintable(child<QLabel>(page, "guideWhy")->text()));
  // Every station left out: the same button, the same kind of answer.
  askFor({QStringLiteral("BBB")});
  auto *from = child<QComboBox>(page, "guideFrom_BBB");
  from->setCurrentIndex(from->findData(int(tdvms::Source::None)));
  emit from->activated(from->currentIndex());
  QVERIFY(!child<QPushButton>(page, "guideGo")->isEnabled());
  QVERIFY(!child<QLabel>(page, "guideWhy")->text().isEmpty());
}

void TstDataGuided::aFailedRequestIsNamedAndRetried() {
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  askFor({QStringLiteral("CCC"), QStringLiteral("BBB")});
  // The portal refuses CCC's request; the FDSN service fails on one day.
  portalFails_->append(tdvms::chunkId("CCC", "2019-03-20T00:00:00"));
  broken_->append(QStringLiteral("2019-03-25"));
  auto *table = child<QTableWidget>(page, "guideStations");
  auto *retry = child<QPushButton>(page, "guideRetry");
  QVERIFY(retry->isHidden());
  child<QPushButton>(page, "guideGo")->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  // Which, and why.
  QTRY_VERIFY2(cell(table, "CCC", 4).contains("1 failed") &&
                   cell(table, "CCC", 4).contains("portal said no"),
               qPrintable(cell(table, "CCC", 4)));
  QVERIFY2(cell(table, "BBB", 4).contains("1 failed") &&
               cell(table, "BBB", 4).contains("2019-03-25") &&
               cell(table, "BBB", 4).contains("HTTP 500"),
           qPrintable(cell(table, "BBB", 4)));
  QVERIFY2(child<QLabel>(page, "guideDone")->text().contains("2 request"),
           qPrintable(child<QLabel>(page, "guideDone")->text()));
  QVERIFY(!retry->isHidden());
  QVERIFY(retry->isEnabled());
  shot(page, QStringLiteral("guided_failed"));
  // A failed day is not asked for again by the main button on its own...
  QCOMPARE(asked_->size(), 14);

  // ...Retry failed does, and this time both answer.
  portalFails_->clear();
  broken_->clear();
  retry->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  QCOMPARE(asked_->size(), 15);
  QCOMPARE(*portalRuns_, 2);
  const tdvms::Campaign c = guide->download();
  QCOMPARE(tdvms::progressOf(Ledger(c.ledgerPath).rows(), "CCC").failed, 0);
  QCOMPARE(tdvms::progressOf(Ledger(tdvms::fdsnStatePath(c)).rows(), "BBB")
               .fetched,
           14);
  QTRY_VERIFY(retry->isHidden());
  QVERIFY(child<QLabel>(page, "guideDone")->text().startsWith("Finished."));
}

void TstDataGuided::anUnfinishedDownloadIsOfferedAgain() {
  QVERIFY(makeStudy());
  // A download of this study that the application was closed on: two
  // requests at the portal, three days at the FDSN service, all waiting.
  const tdvms::Campaign c =
      tdvms::studyDownload(store_->study().dir, true);
  Ledger portal(c.ledgerPath);
  QCOMPARE(tdvms::enumerateChunks(portal, "AAA", "2019-01-01", "2019-02-12", 21),
           2);
  Ledger fdsn(tdvms::fdsnStatePath(c));
  QCOMPARE(tdvms::fdsnPlan(fdsn, "BBB",
                           tdvms::daysBetween(QDate(2019, 1, 1),
                                              QDate(2019, 1, 4)),
                           "KO", "BH?", "http://fdsn.invalid"),
           3);

  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Resume));
  QVERIFY(child<QLabel>(page, "guideTitle")->text().contains("unfinished"));
  auto *table = child<QTableWidget>(page, "guideStations");
  QCOMPARE(table->rowCount(), 2);
  QVERIFY2(cell(table, "AAA", 3).contains("2 request"),
           qPrintable(cell(table, "AAA", 3)));
  QVERIFY2(cell(table, "BBB", 3).contains("3 day"),
           qPrintable(cell(table, "BBB", 3)));
  auto *go = child<QPushButton>(page, "guideGo");
  QCOMPARE(go->text(), QStringLiteral("Resume the download"));
  QVERIFY(go->isEnabled());
  shot(page, QStringLiteral("guided_resume"));
  go->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 30000);
  QTRY_VERIFY_WITH_TIMEOUT(!guide->running(), 10000);
  // Nothing new was asked for: the same two requests and three days.
  QCOMPARE(portal.rows().size(), 2);
  QCOMPARE(fdsn.rows().size(), 3);
  QCOMPARE(tdvms::progressOf(portal.rows(), "AAA").fetched, 2);
  QCOMPARE(tdvms::progressOf(fdsn.rows(), "BBB").fetched, 3);
  // All answered: there is nothing left to offer.
  QTRY_COMPARE(int(guide->mode()), int(CaseDownloads::Mode::Idle));
}

void TstDataGuided::theMapAddsStationsToTheList() {
  QVERIFY(makeStudy());
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  guide->setHooks(hooks());
  guide->setPortalStations(portalList());
  askFor({QStringLiteral("AAA")});
  auto *table = child<QTableWidget>(page, "guideStations");
  auto *map = child<StationPickMap>(page, "stationMap");
  QCOMPARE(table->rowCount(), 1);
  // A click on a station of the map: it joins the list, with its own row.
  emit map->toggled(QStringLiteral("CCC"));
  QCOMPARE(guide->stations(),
           (QStringList{QStringLiteral("AAA"), QStringLiteral("CCC")}));
  QCOMPARE(table->rowCount(), 2);
  QVERIFY(cell(table, "CCC", 1).startsWith("none of 14"));
  QCOMPARE(map->selected().size(), 2);
  // A second click takes it out again.
  emit map->toggled(QStringLiteral("CCC"));
  QCOMPARE(table->rowCount(), 1);
  QCOMPARE(guide->stations(), QStringList{QStringLiteral("AAA")});
}

void TstDataGuided::withoutAStudyThePageIsAsBefore() {
  TdvmsWidget page(ctx_);
  CaseDownloads *guide = page.guide();
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Off));
  QVERIFY(guide->isHidden());
  askFor({QStringLiteral("AAA"), QStringLiteral("BBB")});
  QCOMPARE(int(guide->mode()), int(CaseDownloads::Mode::Off));
  auto *bar = child<QWidget>(page, "wantedBar");
  QVERIFY(!bar->isHidden());
  QCOMPARE(child<QComboBox>(page, "wantedStation")->count(), 2);
  // "Plan from AFAD…" is off without a campaign, and the bar says so.
  QVERIFY(!child<QPushButton>(page, "wantedPlan")->isEnabled());
  QVERIFY2(child<QLabel>(page, "wantedText")->text().contains("campaign"),
           qPrintable(child<QLabel>(page, "wantedText")->text()));
}

QTEST_MAIN(TstDataGuided)
#include "tst_data_guided.moc"
