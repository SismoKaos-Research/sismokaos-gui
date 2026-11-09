// A small synchronous IMAP4rev1 client over TLS: enough to log in, search,
// fetch a message without marking it read (BODY.PEEK), and set flags. Meant
// to run in a worker thread (it blocks on the socket). Replaces imaplib.
#pragma once

#include <QByteArray>
#include <QSslSocket>
#include <QString>
#include <QStringList>

#include <memory>
#include <stdexcept>

namespace skg::tdvms {

struct ImapError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ImapResponse {
  QStringList untagged;       // "* ..." lines, literals replaced by their text
  QList<QByteArray> literals; // the literals, in order
  QString status;             // OK, NO, BAD of the tagged line
  QString text;               // the rest of the tagged line
};

class ImapClient {
public:
  explicit ImapClient(int timeoutMs = 30000);
  ~ImapClient();

  void connectToHost(const QString &host, quint16 port = 993);
  void login(const QString &user, const QString &password);
  // Returns the EXISTS count of the folder.
  int select(const QString &folder, bool readOnly = false);
  QStringList uidSearch(const QString &criteria); // e.g. "(UNSEEN)"
  // The whole message, fetched with BODY.PEEK[] so \Seen is not set.
  QByteArray uidFetchMessage(const QString &uid);
  // flags: "(\\Seen)" or "(\\Seen \\Flagged)"
  void uidStore(const QString &uid, const QString &flags, bool add = true);
  void logout();
  bool isConnected() const;

  // One tagged command; throws ImapError on NO/BAD, timeout or disconnect.
  ImapResponse command(const QString &cmd);

private:
  QByteArray readLine();
  QByteArray readExactly(int n);
  void ensureConnected() const;

  int timeoutMs_;
  int tag_ = 0;
  std::unique_ptr<QSslSocket> socket_;
};

} // namespace skg::tdvms
