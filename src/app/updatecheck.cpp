#include "app/updatecheck.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>

namespace skg {

const QString UpdateCheck::kRepo =
    QStringLiteral("SismoKaos-Research/sismokaos-gui");
const QString UpdateCheck::kSettingOnStart =
    QStringLiteral("updates/checkOnStart");

UpdateCheck::UpdateCheck(QSettings *settings, QWidget *dialogParent,
                         QObject *parent)
    : QObject(parent), settings_(settings), parent_(dialogParent) {}

namespace {

QString bare(QString s) {
  if (s.startsWith('v', Qt::CaseInsensitive))
    s.remove(0, 1);
  return s;
}

// "beta.2" against "rc.1": part by part, numbers as numbers.
int compareSuffix(const QString &a, const QString &b) {
  const QStringList pa = a.split(QLatin1Char('.'));
  const QStringList pb = b.split(QLatin1Char('.'));
  for (qsizetype i = 0; i < qMax(pa.size(), pb.size()); ++i) {
    if (i >= pa.size())
      return -1;
    if (i >= pb.size())
      return 1;
    bool na = false, nb = false;
    const int ia = pa[i].toInt(&na), ib = pb[i].toInt(&nb);
    if (na && nb) {
      if (ia != ib)
        return ia < ib ? -1 : 1;
    } else if (const int c = pa[i].compare(pb[i], Qt::CaseInsensitive)) {
      return c < 0 ? -1 : 1;
    }
  }
  return 0;
}

} // namespace

bool UpdateCheck::isPreRelease(const QString &version) {
  return version.contains(QLatin1Char('-'));
}

bool UpdateCheck::isNewer(const QString &latest, const QString &current) {
  const QString l = bare(latest), c = bare(current);
  const QVersionNumber a = QVersionNumber::fromString(l);
  const QVersionNumber b = QVersionNumber::fromString(c);
  if (a.isNull() || b.isNull())
    return false;
  const int order = QVersionNumber::compare(a, b);
  if (order != 0)
    return order > 0;
  // The same number: a pre-release (0.4.0-beta.1) is older than the release
  // it leads to, and than a later pre-release of it.
  const bool lp = isPreRelease(l), cp = isPreRelease(c);
  if (!cp)
    return false;
  if (!lp)
    return true;
  return compareSuffix(l.section(QLatin1Char('-'), 1),
                       c.section(QLatin1Char('-'), 1)) > 0;
}

QString UpdateCheck::bestOffer(const QStringList &tags,
                               const QString &current) {
  const bool pre = isPreRelease(current);
  QString best;
  for (const QString &tag : tags) {
    if (!pre && isPreRelease(tag))
      continue;
    if (!isNewer(tag, current))
      continue;
    if (best.isEmpty() || isNewer(tag, best))
      best = tag;
  }
  return best;
}

QString UpdateCheck::updaterPath() {
  QStringList candidates;
#ifdef Q_OS_WIN
  candidates << QCoreApplication::applicationDirPath() +
                    QStringLiteral("/update.ps1");
#elif defined(Q_OS_MACOS)
  // <dir>/sismokaos-gui.app/Contents/MacOS/sismokaos-gui -> <dir>/update
  candidates << QDir(QCoreApplication::applicationDirPath())
                    .filePath(QStringLiteral("../../../update"));
  candidates << QDir::homePath() + QStringLiteral("/Applications/update");
#else
  const QString appImage = qEnvironmentVariable("APPIMAGE");
  if (!appImage.isEmpty())
    candidates << QFileInfo(appImage).absolutePath() +
                      QStringLiteral("/update");
  candidates << QCoreApplication::applicationDirPath() +
                    QStringLiteral("/update");
  candidates << QDir::homePath() +
                    QStringLiteral("/.local/opt/sismokaos-gui/update");
#endif
  for (const QString &c : candidates)
    if (QFileInfo(c).isFile())
      return QDir::cleanPath(c);
  return {};
}

bool UpdateCheck::runUpdater(const QString &version) {
  const QString script = updaterPath();
  if (script.isEmpty())
    return false;
  const QString pid = QString::number(QCoreApplication::applicationPid());
  const QString want = bare(version);
#ifdef Q_OS_WIN
  QStringList args{QStringLiteral("-NoProfile"),
                   QStringLiteral("-ExecutionPolicy"),
                   QStringLiteral("Bypass"),
                   QStringLiteral("-File"),
                   script,
                   QStringLiteral("-WaitPid"),
                   pid,
                   QStringLiteral("-Relaunch")};
  if (!want.isEmpty())
    args << QStringLiteral("-Version") << want;
  return QProcess::startDetached(QStringLiteral("powershell"), args);
#else
  // Output goes to a log beside the script; the script restarts the app.
  QProcess p;
  p.setProgram(QStringLiteral("/bin/sh"));
  QStringList args{script, QStringLiteral("--wait-pid"), pid,
                   QStringLiteral("--relaunch")};
  if (!want.isEmpty())
    args << QStringLiteral("--version") << want;
  p.setArguments(args);
  p.setStandardOutputFile(QFileInfo(script).absolutePath() +
                          QStringLiteral("/update.log"));
  p.setStandardErrorFile(QFileInfo(script).absolutePath() +
                         QStringLiteral("/update.log"));
  return p.startDetached();
#endif
}

void UpdateCheck::check(bool quiet) {
  if (busy_)
    return;
  busy_ = true;
  const QString current = QCoreApplication::applicationVersion();
  // A released build asks for the latest release; a pre-release build reads
  // the list, where the pre-releases are too.
  const bool pre = isPreRelease(current);
  QNetworkRequest req(
      QUrl(QStringLiteral("https://api.github.com/repos/%1/releases%2")
               .arg(kRepo, pre ? QStringLiteral("?per_page=20")
                               : QStringLiteral("/latest"))));
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setRawHeader("User-Agent", ("sismokaos-gui/" + current).toUtf8());
  req.setTransferTimeout(15000);
  QNetworkReply *reply = net_.get(req);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, quiet, pre, current] {
    busy_ = false;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      if (!quiet)
        QMessageBox::warning(
            parent_, tr("Check for updates"),
            tr("Could not reach GitHub: %1").arg(reply->errorString()));
      return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject o = doc.object();
    bool any = !o.isEmpty();
    if (pre) {
      // The newest this build should take, among releases and pre-releases.
      QStringList tags;
      const QJsonArray list = doc.array();
      for (const QJsonValue &v : list)
        if (!v.toObject().value(QStringLiteral("draft")).toBool())
          tags << v.toObject().value(QStringLiteral("tag_name")).toString();
      any = !tags.isEmpty();
      const QString best = bestOffer(tags, current);
      o = {};
      for (const QJsonValue &v : list)
        if (!best.isEmpty() &&
            v.toObject().value(QStringLiteral("tag_name")).toString() == best)
          o = v.toObject();
    }
    const QString tag = o.value(QStringLiteral("tag_name")).toString();
    const QString url = o.value(QStringLiteral("html_url")).toString();
    const QString notes = o.value(QStringLiteral("body")).toString();
    if (!any) {
      if (!quiet)
        QMessageBox::information(parent_, tr("Check for updates"),
                                 tr("No release has been published yet."));
      return;
    }
    if (tag.isEmpty() || !isNewer(tag, current)) {
      if (!quiet)
        QMessageBox::information(
            parent_, tr("Check for updates"),
            tr("sismokaos-gui %1 is the latest version.").arg(current));
      return;
    }
    emit updateAvailable(tag, url);
    offer(tag, url, notes);
  });
}

bool UpdateCheck::startUpdate(const QString &version, QString *why) {
  const int jobs = runningJobs ? runningJobs() : 0;
  if (jobs > 0) {
    const bool stop =
        confirmStopJobs
            ? confirmStopJobs(jobs)
            : QMessageBox::question(
                  parent_, tr("Update"),
                  tr("%n job(s) are still running. Updating closes the "
                     "application, which stops them; what they have "
                     "finished is kept.",
                     nullptr, jobs),
                  QMessageBox::StandardButtons(QMessageBox::Yes |
                                               QMessageBox::No),
                  QMessageBox::No) == QMessageBox::Yes;
    if (!stop)
      return false; // "not now": nothing to explain
  }
  // The application must really be on its way out before the updater, which
  // replaces it, is started.
  if (closeForUpdate && !closeForUpdate()) {
    if (why)
      *why = tr("The application did not close, so the update was not "
                "started.");
    return false;
  }
  const bool started = launch ? launch(version) : runUpdater(version);
  if (!started) {
    if (why)
      *why = tr("The updater could not be started: %1").arg(updaterPath());
    return false;
  }
  if (quit)
    quit();
  else
    QCoreApplication::quit();
  return true;
}

void UpdateCheck::offer(const QString &version, const QString &url,
                        const QString &notes) {
  QMessageBox box(parent_);
  box.setIcon(QMessageBox::Information);
  box.setWindowTitle(tr("Update available"));
  box.setText(tr("sismokaos-gui %1 is available (this is %2).")
                  .arg(version, QCoreApplication::applicationVersion()));
  const QString updater = updaterPath();
  box.setInformativeText(
      updater.isEmpty() ? tr("No updater was found beside the application; the "
                             "release page has the downloads.")
                        : tr("Update now closes the application, replaces it "
                             "with the new version and starts it again. "
                             "Your settings and data are kept."));
  if (!notes.trimmed().isEmpty())
    box.setDetailedText(notes);
  QPushButton *update =
      updater.isEmpty()
          ? nullptr
          : box.addButton(tr("Update now"), QMessageBox::AcceptRole);
  QPushButton *page =
      box.addButton(tr("Open release page"), QMessageBox::ActionRole);
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  box.exec();
  if (update && box.clickedButton() == update) {
    QString why;
    if (!startUpdate(version, &why) && !why.isEmpty())
      QMessageBox::warning(parent_, tr("Update"), why);
  } else if (box.clickedButton() == page)
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace skg
