#include "modules/tdvms/mailboxdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>

#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "tdvms/imap.h"
#include "tdvms/settings.h"

namespace skg {

using namespace tdvms;

MailboxDialog::MailboxDialog(JobModel *jobs, QWidget *parent)
    : QDialog(parent), jobs_(jobs) {
  setWindowTitle(tr("Mailbox and defaults"));
  const auto got = resolveCredentials();
  host_ = new QLineEdit(got.value(kCredentialVars[0]).value, this);
  host_->setPlaceholderText(QStringLiteral("imap.gmail.com"));
  user_ = new QLineEdit(got.value(kCredentialVars[1]).value, this);
  user_->setPlaceholderText(QStringLiteral("you@gmail.com"));
  password_ = new QLineEdit(got.value(kCredentialVars[2]).value, this);
  password_->setEchoMode(QLineEdit::Password);
  password_->setPlaceholderText(
      tr("an app password, not the account password"));
  slots_ = new QSpinBox(this);
  slots_->setRange(1, 64);
  int n = kDefaultSlots;
  try {
    n = slotCount();
  } catch (const std::exception &) {
  }
  slots_->setValue(n);
  notify_ = new QLineEdit(notifyTargets().join(", "), this);
  notify_->setPlaceholderText(tr("https://ntfy.sh/your-topic, desktop"));
  source_ = new QLabel(this);
  source_->setWordWrap(true);
  QStringList src;
  for (const QString &v : kCredentialVars)
    if (!got.value(v).source.isEmpty())
      src << QStringLiteral("%1: %2").arg(v, got.value(v).source);
  source_->setText(src.isEmpty() ? tr("No credentials set yet.")
                                 : src.join('\n'));

  check_ = new QPushButton(tr("Check login"), this);
  connect(check_, &QPushButton::clicked, this, &MailboxDialog::check);

  auto *form = new QFormLayout;
  form->addRow(tr("IMAP host"), host_);
  form->addRow(tr("User"), user_);
  form->addRow(tr("Password"), password_);
  form->addRow(QString(), check_);
  form->addRow(tr("Slots"), slots_);
  form->addRow(tr("Notify"), notify_);
  form->addRow(QString(), source_);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &MailboxDialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(
      new QLabel(tr("Saved to %1 and %2, readable by you only. Environment "
                    "variables of the same names take precedence.")
                     .arg(credentialsPath(), settingsPath()),
                 this));
  layout->addWidget(buttons);
}

void MailboxDialog::check() {
  const QString host = host_->text().trimmed(), user = user_->text().trimmed(),
                password = password_->text();
  if (host.isEmpty() || user.isEmpty() || password.isEmpty()) {
    QMessageBox::warning(this, windowTitle(),
                         tr("Host, user and password are all needed."));
    return;
  }
  jobs_->add(new TaskJob(
      tr("mailbox check"), QStringLiteral("tdvms"),
      tr("log in to %1 as %2").arg(host, user),
      [host, user, password](TaskContext &ctx) -> int {
        try {
          ImapClient c(20000);
          c.connectToHost(host);
          c.login(user, password);
          const int n = c.select(QStringLiteral("INBOX"), true);
          ctx.log(tr("logged in as %1; INBOX holds %n message(s)", nullptr, n)
                      .arg(user));
          c.logout();
          return 0;
        } catch (const std::exception &e) {
          ctx.log(QString::fromUtf8(e.what()));
          return 1;
        }
      }));
}

void MailboxDialog::save() {
  Credentials c{host_->text().trimmed(), user_->text().trimmed(),
                password_->text()};
  if (!saveCredentials(c)) {
    QMessageBox::warning(this, windowTitle(),
                         tr("Could not write %1.").arg(credentialsPath()));
    return;
  }
  if (!saveSettings({{kSlotsVar, QString::number(slots_->value())},
                     {kNotifyVar, notify_->text().trimmed()}})) {
    QMessageBox::warning(this, windowTitle(),
                         tr("Could not write %1.").arg(settingsPath()));
    return;
  }
  accept();
}

} // namespace skg
