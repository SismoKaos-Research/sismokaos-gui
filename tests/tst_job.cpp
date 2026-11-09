#include <QtTest>

#include "jobs/job.h"
#include "jobs/jobmodel.h"

using namespace skg;

namespace {
JobSpec shell(const QString &script, const QString &tool = {}) {
  JobSpec s;
  s.name = "test";
  s.tool = tool;
  s.program = "/bin/sh";
  s.args = {"-c", script};
  return s;
}
} // namespace

class TstJob : public QObject {
  Q_OBJECT
private slots:
  void exitCodeAndLog() {
    Job job(shell("echo hello; echo oops 1>&2; exit 3"));
    QSignalSpy done(&job, &Job::stateChanged);
    job.start();
    QCOMPARE(job.state(), Job::State::Running);
    QVERIFY(done.wait(5000) || job.state() != Job::State::Running);
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Failed, 5000);
    QCOMPARE(job.exitCode(), 3);
    QVERIFY(job.log().contains("hello"));
    QVERIFY(job.log().contains("oops")); // stderr merged
    QVERIFY(job.log().first().startsWith("$ /bin/sh"));
    QVERIFY(job.log().last().startsWith("= exit 3"));
    QVERIFY(job.elapsedMs() >= 0);
  }

  void progressFromOutput() {
    Job job(shell("printf '[1/2] a\\n@sfc "
                  "{\"event\":\"day\",\"done\":2,\"total\":2}\\nlast line "
                  "without newline'"));
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Done, 5000);
    QCOMPARE(job.exitCode(), 0);
    QCOMPARE(job.progress().fraction, 1.0);
    QVERIFY(!job.log().contains(QStringLiteral(
        "@sfc {\"event\":\"day\",\"done\":2,\"total\":2}"))); // hidden
    QVERIFY(job.log().contains("last line without newline"));
  }

  void cancelKillsProcess() {
    Job job(shell("sleep 30"));
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
    Job job(s);
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Failed, 5000);
    QVERIFY(job.log().last().contains("could not start"));
  }

  void environment() {
    JobSpec s = shell("echo \"$PYTHONUNBUFFERED $MYVAR\"");
    s.env.insert("MYVAR", "set");
    Job job(s);
    job.start();
    QTRY_COMPARE_WITH_TIMEOUT(job.state(), Job::State::Done, 5000);
    QVERIFY(job.log().contains("1 set"));
  }

  void model() {
    JobModel model;
    QSignalSpy added(&model, &JobModel::jobAdded);
    QSignalSpy finished(&model, &JobModel::jobFinished);
    Job *a = model.add(shell("exit 0"));
    Job *b = model.add(shell("exit 2"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowOf(b), 1);
    QCOMPARE(added.count(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
    QCOMPARE(model.runningCount(), 0);
    QCOMPARE(
        model.data(model.index(0, JobModel::State), Qt::DisplayRole).toString(),
        stateName(Job::State::Done));
    QVERIFY(model.data(model.index(1, JobModel::State), Qt::DisplayRole)
                .toString()
                .contains("2"));
    QCOMPARE(a->state(), Job::State::Done);
  }
};

QTEST_GUILESS_MAIN(TstJob)
#include "tst_job.moc"
