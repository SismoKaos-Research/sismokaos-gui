#include "app/diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSysInfo>

#include <exception>

#include "core/keepawake.h"
#include "app/updatecheck.h"
#include "cbi/zones.h"
#include "core/i18n.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "study/store.h"

#ifndef SKG_BUILD_TYPE
#define SKG_BUILD_TYPE "unknown"
#endif

namespace skg {

QString scrubbed(QString text, QString home, QString user) {
  if (home.isEmpty())
    home = QDir::homePath();
  if (user.isEmpty())
    user = QFileInfo(home).fileName();
  // The home folder, in either spelling of the separators.
  if (home.size() > 1) {
    text.replace(QDir::toNativeSeparators(home), QStringLiteral("~"),
                 Qt::CaseInsensitive);
    text.replace(QDir::fromNativeSeparators(home), QStringLiteral("~"),
                 Qt::CaseInsensitive);
  }
  static const QRegularExpression mail(
      QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"));
  text.replace(mail, QStringLiteral("<address>"));
  // The login name as a part of some other path (/media/<user>/disk).
  if (user.size() > 2) {
    const QRegularExpression part(
        QStringLiteral("(?<=[/\\\\])%1(?=[/\\\\]|$)")
            .arg(QRegularExpression::escape(user)),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::MultilineOption);
    text.replace(part, QStringLiteral("<user>"));
  }
  return text;
}

QString manualUrl() {
  return QStringLiteral("https://github.com/%1/blob/v%2/README.md"
                        "#first-study-in-ten-minutes")
      .arg(UpdateCheck::kRepo, QCoreApplication::applicationVersion());
}

QString reportUrl() {
  return QStringLiteral("https://github.com/%1/issues/new?template="
                        "beta-report.md")
      .arg(UpdateCheck::kRepo);
}

namespace {

QString dataFileState(const QString &name) {
  try {
    cbi::dataFile(name);
    return QStringLiteral("found");
  } catch (const std::exception &) {
    return QStringLiteral("MISSING");
  }
}

// The run records under a study folder: <case>/<station>/<tool>/<recipe>/.
int countRuns(const QString &dir, int *failed, int *unfinished) {
  int n = 0;
  QDirIterator it(dir, QDir::Dirs | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  int seen = 0;
  while (it.hasNext() && ++seen < 20000) {
    const QString d = it.next();
    const int depth =
        QDir(dir).relativeFilePath(d).count(QLatin1Char('/')) + 1;
    if (depth != 4)
      continue;
    const QString name = QFileInfo(d).fileName();
    if (name.endsWith(QLatin1String(".failed")))
      ++*failed;
    else if (name.endsWith(QLatin1String(".tmp")))
      ++*unfinished;
    else if (QFileInfo::exists(QDir(d).filePath(QStringLiteral("run.json"))))
      ++n;
  }
  return n;
}

} // namespace

QString diagnosticsText(const DiagnosticsInput &in) {
  QStringList out;
  out << QStringLiteral("sismokaos-gui %1 (%2 build)")
             .arg(QCoreApplication::applicationVersion(),
                  QStringLiteral(SKG_BUILD_TYPE).isEmpty()
                      ? QStringLiteral("unknown")
                      : QStringLiteral(SKG_BUILD_TYPE));
  out << QStringLiteral("System: %1, %2, kernel %3")
             .arg(QSysInfo::prettyProductName(),
                  QSysInfo::currentCpuArchitecture(), QSysInfo::kernelVersion());
  out << QStringLiteral("Qt %1, platform %2")
             .arg(QLatin1String(qVersion()), QGuiApplication::platformName());
  out << QStringLiteral("Language: %1 (Qt's own translation: %2), theme: %3")
             .arg(in.language,
                  in.language == QLatin1String("en")
                      ? QStringLiteral("not needed")
                      : QFileInfo::exists(
                            I18n::qtTranslationResource(in.language))
                            ? QStringLiteral("embedded")
                            : QStringLiteral("not embedded"),
                  in.theme);
  out << QStringLiteral("Data files: eafz.geojson %1, faults_tr.geojson %2")
             .arg(dataFileState(QStringLiteral("eafz.geojson")),
                  dataFileState(QStringLiteral("faults_tr.geojson")));
  const QString awake = KeepAwake::mechanism();
  out << QStringLiteral("Keep awake: %1")
             .arg(awake.isEmpty() ? QStringLiteral("not available") : awake);

  if (in.study && in.study->isOpen()) {
    const study::Study &s = in.study->study();
    int stations = 0;
    for (const study::Case &c : s.cases)
      stations += int(c.stations.size());
    int failed = 0, unfinished = 0;
    const int runs = countRuns(s.dir, &failed, &unfinished);
    out << QStringLiteral("Study: \"%1\" (folder %2): %3 case(s), %4 station "
                          "row(s), %5 recipe(s), %6 waveform folder(s)")
               .arg(s.name, QFileInfo(s.dir).fileName())
               .arg(s.cases.size())
               .arg(stations)
               .arg(s.recipes.size())
               .arg(s.waveRoots.size());
    out << QStringLiteral("Runs on disk: %1 finished, %2 failed, %3 "
                          "unfinished; catalog: %4; lock held: %5")
               .arg(runs)
               .arg(failed)
               .arg(unfinished)
               .arg(s.catalog.isEmpty() ? QStringLiteral("built-in list")
                                        : QStringLiteral("own file"),
                    in.study->holdsLock() ? QStringLiteral("yes")
                                          : QStringLiteral("no"));
  } else {
    out << QStringLiteral("Study: none open");
  }

  if (in.jobs) {
    int running = 0, done = 0, failed = 0, cancelled = 0;
    QStringList failures;
    for (int i = 0; i < in.jobs->rowCount(); ++i) {
      const Job *j = in.jobs->jobAt(i);
      switch (j->state()) {
      case Job::State::Running:
      case Job::State::Queued:
        ++running;
        break;
      case Job::State::Done:
        ++done;
        break;
      case Job::State::Cancelled:
        ++cancelled;
        break;
      case Job::State::Failed: {
        ++failed;
        // What it said last, before the "= exit" trailer.
        QString why;
        const QStringList &log = j->log();
        for (qsizetype k = log.size() - 1; k >= 0 && why.isEmpty(); --k)
          if (!log[k].startsWith(QLatin1String("= exit")) &&
              !log[k].trimmed().isEmpty())
            why = log[k].trimmed();
        failures << QStringLiteral("  - %1 [%2], after %3: %4")
                        .arg(j->name(), j->tool(),
                             JobModel::elapsedText(j->elapsedMs()),
                             why.left(300));
        break;
      }
      }
    }
    out << QStringLiteral("Jobs this session: %1 running, %2 finished, %3 "
                          "failed, %4 stopped")
               .arg(running)
               .arg(done)
               .arg(failed)
               .arg(cancelled);
    if (!failures.isEmpty()) {
      out << QStringLiteral("Failures (the last five):");
      out << failures.mid(qMax(qsizetype(0), failures.size() - 5));
    }
  }
  QString text = out.join(QLatin1Char('\n'));
  // Of the study, its folder's name only: a log line may hold its place.
  if (in.study && in.study->isOpen()) {
    const QString dir = in.study->study().dir;
    text.replace(QDir::toNativeSeparators(dir), QStringLiteral("<study>"));
    text.replace(dir, QStringLiteral("<study>"));
  }
  return scrubbed(text);
}

} // namespace skg
