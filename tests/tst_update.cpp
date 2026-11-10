// The release comparison behind "Check for updates". The network part is
// not exercised; the version logic decides whether an update is offered.
#include <QtTest>

#include "app/updatecheck.h"

using namespace skg;

class TstUpdate : public QObject {
  Q_OBJECT
private slots:
  void newerVersions() {
    QVERIFY(UpdateCheck::isNewer("v0.2.0", "0.1.0"));
    QVERIFY(UpdateCheck::isNewer("1.0.0", "0.9.9"));
    QVERIFY(UpdateCheck::isNewer("0.1.10", "0.1.9"));
    QVERIFY(!UpdateCheck::isNewer("0.1.0", "0.1.0"));
    QVERIFY(!UpdateCheck::isNewer("v0.1.0", "0.1.0"));
    // A beta is told about its own release, not about an older stable one.
    QVERIFY(UpdateCheck::isNewer("v0.4.0", "0.4.0-beta.1"));
    QVERIFY(!UpdateCheck::isNewer("v0.3.0", "0.4.0-beta.1"));
    QVERIFY(!UpdateCheck::isNewer("v0.4.0-beta.1", "0.4.0-beta.1"));
    QVERIFY(UpdateCheck::isNewer("v0.4.1", "0.4.0-beta.1"));
    QVERIFY(!UpdateCheck::isNewer("0.0.9", "0.1.0"));
    QVERIFY(!UpdateCheck::isNewer("", "0.1.0"));
    QVERIFY(!UpdateCheck::isNewer("latest", "0.1.0"));
  }

  void updaterAbsentHere() {
    // A development build has no installer beside it.
    QVERIFY(UpdateCheck::updaterPath().isEmpty() ||
            QFileInfo(UpdateCheck::updaterPath()).isFile());
  }
};

QTEST_GUILESS_MAIN(TstUpdate)
#include "tst_update.moc"
