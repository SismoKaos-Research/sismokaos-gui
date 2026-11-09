#include <QtTest>

#include "jobs/progress.h"

using namespace skg;

class TstProgress : public QObject {
  Q_OBJECT
private slots:
  void counter() {
    CounterParser p;
    ProgressState st;
    bool hide = true;
    QVERIFY(p.parse("[3/17] Tarih: 2020-01-10  (24 dosya)", st, hide));
    QVERIFY(!hide);
    QCOMPARE(st.fraction, 3.0 / 17);
    QCOMPARE(st.text, QString("3/17"));
    QVERIFY(p.parse("  [2/24] file.npy... ", st, hide)); // indented counter
    QCOMPARE(st.text, QString("2/24"));
    QVERIFY(!p.parse("[INFO] Dosya okunuyor", st, hide));
    QVERIFY(!p.parse("[0/0] nothing", st, hide));
  }

  void sfcJson() {
    SfcJsonParser p;
    ProgressState st;
    bool hide = false;
    QVERIFY(p.parse(
        R"(@sfc {"event": "day", "stage": "features", "done": 3, "total": 17})",
        st, hide));
    QVERIFY(hide);
    QCOMPARE(st.fraction, 3.0 / 17);
    QCOMPARE(st.text, QString("features 3/17"));
    QCOMPARE(st.stage, QString("features"));

    QVERIFY(p.parse(R"(@sfc {"event": "step_start", "step": "baseline"})", st,
                    hide));
    QCOMPARE(st.text, QString("baseline"));
    QCOMPARE(st.fraction, 3.0 / 17); // unchanged

    QVERIFY(p.parse(R"(@sfc {"event": "run_done"})", st, hide));
    QCOMPARE(st.fraction, 1.0);

    // Machine line we do not understand: hidden, no progress.
    QVERIFY(!p.parse("@sfc not json", st, hide));
    QVERIFY(hide);
    // Ordinary line: not ours.
    QVERIFY(!p.parse("plain text", st, hide));
    QVERIFY(!hide);
  }

  void chainAndFactory() {
    auto p = makeProgressParser(QString());
    ProgressState st;
    bool hide = false;
    QVERIFY(p->parse("[1/4] x", st, hide));
    QVERIFY(!hide);
    QVERIFY(
        p->parse(R"(@sfc {"event": "day", "done": 2, "total": 4})", st, hide));
    QVERIFY(hide);
    QCOMPARE(st.fraction, 0.5);
    QVERIFY(!p->parse("@sfc garbage", st, hide));
    QVERIFY(hide); // still hidden by the chain

    auto onlyCounter = makeProgressParser("counter");
    QVERIFY(!onlyCounter->parse(
        R"(@sfc {"event": "day", "done": 2, "total": 4})", st, hide));
    auto onlySfc = makeProgressParser("sfc-json");
    QVERIFY(!onlySfc->parse("[1/4] x", st, hide));
  }
};

QTEST_GUILESS_MAIN(TstProgress)
#include "tst_progress.moc"
