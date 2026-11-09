// Mail from the portal, read as events. Port of tdvms/mailbox.py's pure
// half: a raw RFC 822 message in, one event out. It decides nothing else.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace skg::tdvms {

struct MailMessage {
  // Header names lowercased; several values when a header repeats.
  QMap<QString, QStringList> headers;
  QString text; // every text/* part, decoded and concatenated
  QString header(const QString &name) const {
    return headers.value(name.toLower()).value(0);
  }
};

// A minimal MIME parser: header unfolding, multipart recursion, base64 and
// quoted-printable, charsets through QStringDecoder, RFC 2047 subjects.
MailMessage parseMessage(const QByteArray &raw);
QString decodeHeaderWord(const QString &encoded); // RFC 2047

// Which slot this mail was delivered to: To, then Delivered-To, then
// X-Original-To; the first address with an @, lowercased. Empty if none.
QString recipientOf(const MailMessage &msg);

struct MailEvent {
  enum class Kind {
    Link,      // one or more download links for a slot we own
    NoData,    // the portal says there is no waveform (no link)
    Foreign,   // for a slot this ledger does not own; left unread
    Unrelated, // not from the portal, or portal mail of no use
    Unreadable // the fetch itself failed; retried next pass
  };
  Kind kind = Kind::Unrelated;
  QString uid;
  QString to;
  QString subject;
  QStringList urls;
};

MailEvent classifyMessage(const QString &uid, const MailMessage &msg,
                          const QSet<QString> &knownAddresses,
                          bool claimUnknown = false);

} // namespace skg::tdvms
