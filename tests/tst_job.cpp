#include <QtTest>

#include "jobs/jobmodel.h"
#include "jobs/processjob.h"
#include "jobs/taskjob.h"

using namespace skg;

namespace {
// tests/helper.cpp stands in for external programs on every platform.
const QString kHelper = QStringLiteral(SKG_TEST_HELPER);
JobSpec helper(const QStringList &args, const QString &tool = {}) {
  JobSpec s;
  s.name = "test";
  s.tool = tool;
  s.program = kHelper;
  s.args = args;
  return s;
}
} // namespace

class TstJob : public QObject {
  Q_OBJECT
private slots:
  void exitCodeAndLog() {
    ProcessJob job(helper({"--out", "hello", "--err", "oops", "--exit", "3"}));
    job.start();
    QCOMPARE(job.state(), Job::State::Running);
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Failed, 5000);
    QCOMPARE(job.exitCode(), 3);
    QVERIFY(job.log().contains("hello"));
    QVERIFY(job.log().contains("oops")); // stderr merged
    QVERIFY(job.log().first().startsWith("$ " + kHelper));
    QVERIFY(job.log().last().startsWith("= exit 3"));
    QVERIFY(job.elapsedMs() >= 0);
  }

  void progressFromOutput() {
    ProcessJob job(helper(
        {"--raw",
         "[1/2] a\n@sfc {\"event\":\"day\",\"done\":2,\"total\":2}\nlast line "
         "without newline"}));
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Done, 5000);
    QCOMPARE(job.exitCode(), 0);
    QCOMPARE(job.progress().fraction, 1.0);
    QVERIFY(!job.log().contains(QStringLiteral(
        "@sfc {\"event\":\"day\",\"done\":2,\"total\":2}"))); // hidden
    QVERIFY(job.log().contains("last line without newline"));
  }

  void cancelKillsProcess() {
    ProcessJob job(helper({"--sleep", "30000"}));
    job.start();
    QTest::qWait(200);
    job.cancel();
    QCOMPARE(job.state(), Job::State::Cancelled);
    QVERIFY(job.log().last().startsWith("= exit"));
  }

  void failedToStart() {
    JobSpec s;
    s.name = "missing";
    s.program = "/nonexistent/binary";
    ProcessJob job(s);
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Failed, 5000);
    QVERIFY(job.log().at(job.log().size() - 2).contains("could not start"));
  }

  void environment() {
    JobSpec s = helper({"--env", "PYTHONUNBUFFERED", "--env", "MYVAR"});
    s.env.insert("MYVAR", "set");
    ProcessJob job(s);
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Done, 5000);
    QVERIFY(job.log().contains("1"));
    QVERIFY(job.log().contains("set"));
  }

  void taskRunsInThreadAndReports() {
    const Qt::HANDLE main = QThread::currentThreadId();
    Qt::HANDLE seen = nullptr;
    TaskJob job("task", "tdvms", "count to three", [&](TaskContext &ctx) {
      seen = QThread::currentThreadId();
      for (int i = 1; i <= 3; ++i) {
        ctx.log(QStringLiteral("step %1").arg(i));
        ctx.progress(i / 3.0, QStringLiteral("%1/3").arg(i));
      }
      return 0;
    });
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Done, 5000);
    QVERIFY(seen != nullptr && seen != main);
    QCOMPARE(job.exitCode(), 0);
    QVERIFY(job.log().contains("step 2"));
    QCOMPARE(job.log().first(), QString("$ count to three"));
    QCOMPARE(job.progress().fraction, 1.0);
    QCOMPARE(job.progress().text, QString("3/3"));
  }

  void taskFailureAndException() {
    TaskJob bad("bad", "", "fails", [](TaskContext &) { return 7; });
    bad.start();
    QTRY_COMPARE_WITH_TIMEOUT(bad.state(), Job::State::Failed, 5000);
    QCOMPARE(bad.exitCode(), 7);

    TaskJob thrower("throw", "", "throws", [](TaskContext &) -> int {
      throw std::runtime_error("kaboom");
    });
    thrower.start();
    QTRY_COMPARE_WITH_TIMEOUT(thrower.state(), Job::State::Failed, 5000);
    QCOMPARE(thrower.exitCode(), -1);
    QVERIFY(thrower.log().join('\n').contains("kaboom"));
  }

  void taskCancelIsCooperative() {
    TaskJob job("loop", "", "loops until cancelled", [](TaskContext &ctx) {
      while (!ctx.cancelled())
        QThread::msleep(10);
      ctx.log("saw the cancel");
      return 130;
    });
    job.start();
    QTest::qWait(100);
    job.cancel();
    QCOMPARE(job.state(), Job::State::Cancelled);
    QVERIFY(job.wait(5000));
    QTRY_VERIFY_WITH_TIMEOUT(job.log().contains("saw the cancel"), 5000);
    QCOMPARE(job.state(), Job::State::Cancelled); // finish() keeps Cancelled
    QCOMPARE(job.exitCode(), 130);
  }

  void model() {
    JobModel model;
    QSignalSpy added(&model, &JobModel::jobAdded);
    QSignalSpy finished(&model, &JobModel::jobFinished);
    Job *a = model.addProcess(helper({"--exit", "0"}));
    Job *b = model.addProcess(helper({"--exit", "2"}));
    Job *c = model.add(
        new TaskJob("t", "tdvms", "task", [](TaskContext &) { return 0; }));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.rowOf(b), 1);
    QCOMPARE(added.count(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, 5000);
    QCOMPARE(model.runningCount(), 0);
    QCOMPARE(
        model.data(model.index(0, JobModel::State), Qt::DisplayRole).toString(),
        stateName(Job::State::Done));
    QVERIFY(model.data(model.index(1, JobModel::State), Qt::DisplayRole)
                .toString()
                .contains("2"));
    QCOMPARE(
        model.data(model.index(2, JobModel::Tool), Qt::DisplayRole).toString(),
        QString("tdvms"));
    QCOMPARE(a->state(), Job::State::Done);
    QCOMPARE(c->state(), Job::State::Done);
  }
};

QTEST_GUILESS_MAIN(TstJob)
#include "tst_job.moc"
#include <stdexcept>
