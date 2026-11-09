#include "tdvms/imap.h"

#include <QRegularExpression>

namespace skg::tdvms {

namespace {
QString quoteImap(const QString &s) {
  QString q = s;
  q.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\""));
  return '"' + q + '"';
}
} // namespace

ImapClient::ImapClient(int timeoutMs) : timeoutMs_(timeoutMs) {}

ImapClient::~ImapClient() {
  if (socket_ && socket_->state() == QAbstractSocket::ConnectedState) {
    socket_->disconnectFromHost();
    if (socket_->state() != QAbstractSocket::UnconnectedState)
      socket_->waitForDisconnected(1000);
  }
}

bool ImapClient::isConnected() const {
  return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

void ImapClient::ensureConnected() const {
  if (!isConnected())
    throw ImapError("not connected to the mail server");
}

void ImapClient::connectToHost(const QString &host, quint16 port) {
  socket_ = std::make_unique<QSslSocket>();
  socket_->connectToHostEncrypted(host, port);
  if (!socket_->waitForEncrypted(timeoutMs_))
    throw ImapError(QStringLiteral("could not connect to %1:%2: %3")
                        .arg(host)
                        .arg(port)
                        .arg(socket_->errorString())
                        .toStdString());
  const QByteArray greeting = readLine();
  if (!greeting.startsWith("* OK") && !greeting.startsWith("* PREAUTH"))
    throw ImapError(("unexpected greeting: " + greeting).toStdString());
}

QByteArray ImapClient::readLine() {
  ensureConnected();
  while (!socket_->canReadLine()) {
    if (!socket_->waitForReadyRead(timeoutMs_))
      throw ImapError(QStringLiteral("mail server did not answer within %1 s")
                          .arg(timeoutMs_ / 1000)
                          .toStdString());
  }
  QByteArray line = socket_->readLine();
  while (line.endsWith('\n') || line.endsWith('\r'))
    line.chop(1);
  return line;
}

QByteArray ImapClient::readExactly(int n) {
  ensureConnected();
  QByteArray out;
  out.reserve(n);
  while (out.size() < n) {
    if (socket_->bytesAvailable() == 0 &&
        !socket_->waitForReadyRead(timeoutMs_))
      throw ImapError("mail server stopped mid-literal");
    out += socket_->read(n - out.size());
  }
  return out;
}

ImapResponse ImapClient::command(const QString &cmd) {
  ensureConnected();
  const QString tag = QStringLiteral("A%1").arg(++tag_, 4, 10, QChar('0'));
  socket_->write((tag + ' ' + cmd + "\r\n").toUtf8());
  if (!socket_->waitForBytesWritten(timeoutMs_))
    throw ImapError("could not send to the mail server");
  ImapResponse resp;
  static const QRegularExpression literalRe(QStringLiteral(R"(\{(\d+)\}$)"));
  while (true) {
    QByteArray line = readLine();
    // A line ending in {N} announces N bytes of literal, then the line goes on.
    QString text = QString::fromUtf8(line);
    auto m = literalRe.match(text);
    while (m.hasMatch()) {
      const int n = m.captured(1).toInt();
      resp.literals << readExactly(n);
      text += QStringLiteral("<literal %1>").arg(resp.literals.size() - 1);
      const QByteArray rest = readLine();
      text += QString::fromUtf8(rest);
      m = literalRe.match(text);
    }
    if (text.startsWith(tag + ' ')) {
      const QString after = text.mid(tag.size() + 1);
      resp.status = after.section(' ', 0, 0).toUpper();
      resp.text = after.section(' ', 1);
      break;
    }
    if (text.startsWith('*') || text.startsWith('+'))
      resp.untagged << text;
  }
  if (resp.status != QLatin1String("OK"))
    throw ImapError(QStringLiteral("%1 -> %2 %3")
                        .arg(cmd.section(' ', 0, 0), resp.status, resp.text)
                        .toStdString());
  return resp;
}

void ImapClient::login(const QString &user, const QString &password) {
  command(
      QStringLiteral("LOGIN %1 %2").arg(quoteImap(user), quoteImap(password)));
}

int ImapClient::select(const QString &folder, bool readOnly) {
  const ImapResponse r = command(QStringLiteral("%1 %2").arg(
      readOnly ? "EXAMINE" : "SELECT", quoteImap(folder)));
  static const QRegularExpression exists(QStringLiteral(R"(^\* (\d+) EXISTS)"));
  for (const QString &line : r.untagged) {
    const auto m = exists.match(line);
    if (m.hasMatch())
      return m.captured(1).toInt();
  }
  return 0;
}

QStringList ImapClient::uidSearch(const QString &criteria) {
  const ImapResponse r = command(QStringLiteral("UID SEARCH %1").arg(criteria));
  QStringList out;
  for (const QString &line : r.untagged)
    if (line.startsWith(QLatin1String("* SEARCH")))
      out += line.mid(8).split(' ', Qt::SkipEmptyParts);
  return out;
}

QByteArray ImapClient::uidFetchMessage(const QString &uid) {
  const ImapResponse r =
      command(QStringLiteral("UID FETCH %1 (BODY.PEEK[])").arg(uid));
  if (r.literals.isEmpty())
    throw ImapError(("no message body returned for UID " + uid).toStdString());
  return r.literals.first();
}

void ImapClient::uidStore(const QString &uid, const QString &flags, bool add) {
  command(QStringLiteral("UID STORE %1 %2FLAGS.SILENT %3")
              .arg(uid, add ? "+" : "-", flags));
}

void ImapClient::logout() {
  if (!isConnected())
    return;
  try {
    command(QStringLiteral("LOGOUT"));
  } catch (const ImapError &) {
    // the server closes the connection after BYE; nothing to do
  }
  socket_->disconnectFromHost();
}

} // namespace skg::tdvms
