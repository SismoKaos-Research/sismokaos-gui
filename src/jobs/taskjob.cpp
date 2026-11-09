#include "jobs/taskjob.h"

#include <QCoreApplication>
#include <QMetaObject>

namespace skg {

TaskContext::TaskContext(TaskJob *job) : job_(job) {}

void TaskContext::log(const QString &line) { emit lineLogged(line); }

void TaskContext::progress(double fraction, const QString &text) {
  emit progressReported(fraction, text, true);
}

void TaskContext::progress(const QString &text) {
  emit progressReported(-1, text, false);
}

QString TaskContext::taskName() const { return job_->name(); }

namespace {
// Lives in the worker thread; runs the work when told to.
class Worker : public QObject {
  Q_OBJECT
public:
  Worker(TaskJob::Work work, TaskContext *ctx)
      : work_(std::move(work)), ctx_(ctx) {}
public slots:
  void run() {
    int rc;
    try {
      rc = work_(*ctx_);
    } catch (const std::exception &e) {
      ctx_->log(QStringLiteral("uncaught exception: %1")
                    .arg(QString::fromUtf8(e.what())));
      rc = -1;
    } catch (...) {
      ctx_->log(QStringLiteral("uncaught exception"));
      rc = -1;
    }
    emit finished(rc);
    // Quit from here, not from the job's thread: a caller blocked in
    // TaskJob::wait() cannot service the queued finished() first.
    QThread::currentThread()->quit();
  }
signals:
  void finished(int rc);

private:
  TaskJob::Work work_;
  TaskContext *ctx_;
};
} // namespace

TaskJob::TaskJob(QString name, QString tool, QString description, Work work,
                 QObject *parent)
    : Job(std::move(name), std::move(tool), parent),
      description_(std::move(description)), work_(std::move(work)) {}

TaskJob::~TaskJob() {
  if (thread_.isRunning()) {
    cancel();
    thread_.quit();
    thread_.wait(5000);
  }
}

void TaskJob::start() {
  if (state() != State::Queued)
    return;
  context_ = new TaskContext(this);
  auto *worker = new Worker(work_, context_);
  worker_ = worker;
  // The context and the worker belong to the worker thread, so the work may
  // create thread-affine objects (QNetworkAccessManager, QEventLoop) freely.
  context_->moveToThread(&thread_);
  worker->moveToThread(&thread_);
  connect(context_, &TaskContext::lineLogged, this, &TaskJob::appendLine,
          Qt::QueuedConnection);
  connect(
      context_, &TaskContext::progressReported, this,
      [this](double fraction, const QString &text, bool withFraction) {
        ProgressState p = progress();
        if (withFraction)
          p.fraction = fraction;
        p.text = text;
        setProgress(p);
      },
      Qt::QueuedConnection);
  connect(
      worker, &Worker::finished, this, [this](int rc) { finish(rc); },
      Qt::QueuedConnection);
  connect(&thread_, &QThread::finished, worker, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, context_, &QObject::deleteLater);
  connect(&thread_, &QThread::started, worker, &Worker::run);
  begin();
  thread_.start();
}

void TaskJob::cancel() {
  if (state() != State::Running)
    return;
  setState(State::Cancelled);
  if (context_) {
    // The flag is set here, synchronously: a task busy in a plain loop never
    // services its event queue, so a queued call would never reach it. The
    // signal is queued for tasks that wait in an event loop.
    context_->cancelled_.store(true);
    QMetaObject::invokeMethod(context_, &TaskContext::cancelRequested,
                              Qt::QueuedConnection);
  }
}

bool TaskJob::wait(int ms) {
  if (!thread_.isRunning())
    return true;
  return thread_.wait(ms < 0 ? ULONG_MAX : static_cast<unsigned long>(ms));
}

} // namespace skg

#include "taskjob.moc"
#include <utility>
