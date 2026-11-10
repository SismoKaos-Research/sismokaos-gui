#include "app/updatecheck.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
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

bool UpdateCheck::isNewer(const QString &latest, const QString &current) {
  auto parse = [](QString s) {
    if (s.startsWith('v', Qt::CaseInsensitive))
      s.remove(0, 1);
    return QVersionNumber::fromString(s);
  };
  const QVersionNumber a = parse(latest), b = parse(current);
  if (a.isNull() || b.isNull())
    return false;
  const int order = QVersionNumber::compare(a, b);
  // A pre-release (0.4.0-beta.1) is older than the release it leads to.
  return order > 0 || (order == 0 && current.contains(QLatin1Char('-')) &&
                       !latest.contains(QLatin1Char('-')));
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

bool UpdateCheck::runUpdater() {
  const QString script = updaterPath();
  if (script.isEmpty())
    return false;
  const QString pid = QString::number(QCoreApplication::applicationPid());
#ifdef Q_OS_WIN
  return QProcess::startDetached(
      QStringLiteral("powershell"),
      {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
       QStringLiteral("Bypass"), QStringLiteral("-File"), script,
       QStringLiteral("-WaitPid"), pid, QStringLiteral("-Relaunch")});
#else
  // Output goes to a log beside the script; the script restarts the app.
  QProcess p;
  p.setProgram(QStringLiteral("/bin/sh"));
  p.setArguments({script, QStringLiteral("--wait-pid"), pid,
                  QStringLiteral("--relaunch")});
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
  QNetworkRequest req(
      QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
               .arg(kRepo)));
  req.setRawHeader("Accept", "application/vnd.github+json");
  req.setRawHeader(
      "User-Agent",
      ("sismokaos-gui/" + QCoreApplication::applicationVersion()).toUtf8());
  req.setTransferTimeout(15000);
  QNetworkReply *reply = net_.get(req);
  connect(reply, &QNetworkReply::finished, this, [this, reply, quiet] {
    busy_ = false;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      if (!quiet)
        QMessageBox::warning(
            parent_, tr("Check for updates"),
            tr("Could not reach GitHub: %1").arg(reply->errorString()));
      return;
    }
    const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = o.value(QStringLiteral("tag_name")).toString();
    const QString url = o.value(QStringLiteral("html_url")).toString();
    const QString notes = o.value(QStringLiteral("body")).toString();
    const QString current = QCoreApplication::applicationVersion();
    if (tag.isEmpty()) {
      if (!quiet)
        QMessageBox::information(parent_, tr("Check for updates"),
                                 tr("No release has been published yet."));
      return;
    }
    if (!isNewer(tag, current)) {
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
    if (runUpdater())
      QCoreApplication::quit();
    else
      QMessageBox::warning(
          parent_, tr("Update"),
          tr("The updater could not be started: %1").arg(updater));
  } else if (box.clickedButton() == page)
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace skg
