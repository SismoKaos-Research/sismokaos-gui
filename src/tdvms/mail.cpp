#include "tdvms/mail.h"

#include <QRegularExpression>
#include <QStringDecoder>

namespace skg::tdvms {

namespace {

const QRegularExpression kLinkRe(
    QStringLiteral(R"(https?://tdvms\.afad\.gov\.tr/[^\s"'<>\\]+\.zip)"));
// "...talep ettiginiz istasyon/istasyonlara ait veri bulunmamaktadir"
const QRegularExpression kNoDataRe(QStringLiteral(R"(veri\s+bulunmamaktad)"),
                                   QRegularExpression::CaseInsensitiveOption);
const QRegularExpression
    kEncodedWord(QStringLiteral(R"(=\?([^?]+)\?([bBqQ])\?([^?]*)\?=)"));

// ISO-8859-9 is Latin-1 with six Turkish letters; windows-1254 is cp1252
// with the same six. Qt without ICU (the Windows builds) knows neither.
QString decodeTurkish(const QByteArray &bytes, bool windows1254) {
  static const char16_t cp1252High[32] = {
      0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0xFFFD, 0xFFFD,
      0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0xFFFD, 0x0178};
  QString out;
  out.reserve(bytes.size());
  for (unsigned char c : bytes) {
    char16_t u = c;
    switch (c) {
    case 0xD0:
      u = 0x011E;
      break; // Ğ
    case 0xDD:
      u = 0x0130;
      break; // İ
    case 0xDE:
      u = 0x015E;
      break; // Ş
    case 0xF0:
      u = 0x011F;
      break; // ğ
    case 0xFD:
      u = 0x0131;
      break; // ı
    case 0xFE:
      u = 0x015F;
      break; // ş
    default:
      if (windows1254 && c >= 0x80 && c < 0xA0)
        u = cp1252High[c - 0x80];
    }
    out.append(QChar(u));
  }
  return out;
}

QString decodeText(const QByteArray &bytes, const QString &charset) {
  const QString cs = charset.toLower().remove('_').remove('-');
  if (cs == QLatin1String("iso88599") || cs == QLatin1String("latin5"))
    return decodeTurkish(bytes, false);
  if (cs == QLatin1String("windows1254") || cs == QLatin1String("cp1254"))
    return decodeTurkish(bytes, true);
  QStringDecoder dec(charset.isEmpty() ? "UTF-8"
                                       : charset.toLatin1().constData());
  if (!dec.isValid())
    dec = QStringDecoder(QStringDecoder::Utf8);
  QString out = dec.decode(bytes);
  if (dec.hasError()) // not valid in that charset: Latin-1 never fails
    out = QString::fromLatin1(bytes);
  return out;
}

QByteArray decodeQuotedPrintable(const QByteArray &in, bool headerMode) {
  QByteArray out;
  out.reserve(in.size());
  for (int i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (c == '=' && i + 2 < in.size() &&
        std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      out += static_cast<char>(in.mid(i + 1, 2).toInt(nullptr, 16));
      i += 2;
    } else if (c == '=' && i + 1 < in.size() &&
               (in[i + 1] == '\r' || in[i + 1] == '\n')) {
      // soft line break
      if (in[i + 1] == '\r' && i + 2 < in.size() && in[i + 2] == '\n')
        ++i;
      ++i;
    } else if (headerMode && c == '_') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

struct ContentType {
  QString type = QStringLiteral("text");
  QString subtype = QStringLiteral("plain");
  QMap<QString, QString> params;
};

ContentType parseContentType(const QString &value) {
  ContentType ct;
  if (value.isEmpty())
    return ct;
  const QStringList parts = value.split(';');
  const QString media = parts.first().trimmed().toLower();
  const int slash = media.indexOf('/');
  if (slash > 0) {
    ct.type = media.left(slash);
    ct.subtype = media.mid(slash + 1);
  }
  for (int i = 1; i < parts.size(); ++i) {
    const QString p = parts[i].trimmed();
    const int eq = p.indexOf('=');
    if (eq <= 0)
      continue;
    QString v = p.mid(eq + 1).trimmed();
    if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"'))
      v = v.mid(1, v.size() - 2);
    ct.params.insert(p.left(eq).trimmed().toLower(), v);
  }
  return ct;
}

// Splits raw into (headers, body) at the first empty line and unfolds the
// headers.
QMap<QString, QStringList> parseHeaders(const QByteArray &raw, int &bodyStart) {
  QMap<QString, QStringList> out;
  int pos = 0;
  QString current;
  QString currentName;
  auto flush = [&] {
    if (!currentName.isEmpty())
      out[currentName.toLower()] << current.trimmed();
    currentName.clear();
    current.clear();
  };
  while (pos < raw.size()) {
    int nl = raw.indexOf('\n', pos);
    if (nl < 0)
      nl = raw.size();
    QByteArray line = raw.mid(pos, nl - pos);
    if (line.endsWith('\r'))
      line.chop(1);
    pos = nl + 1;
    if (line.isEmpty()) {
      flush();
      bodyStart = pos;
      return out;
    }
    if (line[0] == ' ' || line[0] == '\t') { // continuation
      current += ' ' + QString::fromLatin1(line.trimmed());
      continue;
    }
    flush();
    const int colon = line.indexOf(':');
    if (colon <= 0)
      continue;
    currentName = QString::fromLatin1(line.left(colon));
    current = QString::fromLatin1(line.mid(colon + 1));
  }
  flush();
  bodyStart = raw.size();
  return out;
}

void collectText(const QByteArray &raw, QString &text, int depth) {
  if (depth > 10)
    return;
  int bodyStart = 0;
  const auto headers = parseHeaders(raw, bodyStart);
  const QByteArray body = raw.mid(bodyStart);
  const ContentType ct =
      parseContentType(headers.value(QStringLiteral("content-type")).value(0));
  if (ct.type == QLatin1String("multipart")) {
    const QString boundary = ct.params.value(QStringLiteral("boundary"));
    if (boundary.isEmpty())
      return;
    const QByteArray marker = "--" + boundary.toLatin1();
    int start = body.indexOf(marker);
    while (start >= 0) {
      int lineEnd = body.indexOf('\n', start);
      if (lineEnd < 0)
        break;
      const QByteArray markerLine = body.mid(start, lineEnd - start).trimmed();
      if (markerLine.endsWith("--"))
        break; // closing boundary
      const int next = body.indexOf(marker, lineEnd + 1);
      QByteArray part =
          body.mid(lineEnd + 1, next < 0 ? -1 : next - lineEnd - 1);
      if (part.endsWith("\r\n"))
        part.chop(2);
      else if (part.endsWith('\n'))
        part.chop(1);
      collectText(part, text, depth + 1);
      start = next;
    }
    return;
  }
  if (ct.type != QLatin1String("text"))
    return;
  const QString enc = headers.value(QStringLiteral("content-transfer-encoding"))
                          .value(0)
                          .trimmed()
                          .toLower();
  QByteArray bytes = body;
  if (enc == QLatin1String("base64"))
    bytes = QByteArray::fromBase64(body);
  else if (enc == QLatin1String("quoted-printable"))
    bytes = decodeQuotedPrintable(body, false);
  text += decodeText(bytes, ct.params.value(QStringLiteral("charset")));
}

} // namespace

QString decodeHeaderWord(const QString &encoded) {
  QString out;
  int pos = 0;
  auto it = kEncodedWord.globalMatch(encoded);
  bool lastWasEncoded = false;
  while (it.hasNext()) {
    const auto m = it.next();
    const QString between = encoded.mid(pos, m.capturedStart() - pos);
    // Whitespace between two encoded words is dropped (RFC 2047 6.2).
    if (!(lastWasEncoded && between.trimmed().isEmpty()))
      out += between;
    const QString charset = m.captured(1).section('*', 0, 0);
    const QByteArray payload = m.captured(3).toLatin1();
    const QByteArray bytes = m.captured(2).toLower() == QLatin1String("b")
                                 ? QByteArray::fromBase64(payload)
                                 : decodeQuotedPrintable(payload, true);
    out += decodeText(bytes, charset);
    pos = m.capturedEnd();
    lastWasEncoded = true;
  }
  out += encoded.mid(pos);
  return out;
}

MailMessage parseMessage(const QByteArray &raw) {
  MailMessage msg;
  int bodyStart = 0;
  msg.headers = parseHeaders(raw, bodyStart);
  collectText(raw, msg.text, 0);
  return msg;
}

QString recipientOf(const MailMessage &msg) {
  static const QRegularExpression angle(
      QStringLiteral(R"(<([^<>\s]+@[^<>\s]+)>)"));
  static const QRegularExpression bare(
      QStringLiteral(R"(([^\s<>,;"]+@[^\s<>,;"]+))"));
  for (const char *name : {"to", "delivered-to", "x-original-to"}) {
    for (const QString &value : msg.headers.value(QLatin1String(name))) {
      auto m = angle.match(value);
      if (!m.hasMatch())
        m = bare.match(value);
      if (m.hasMatch())
        return m.captured(1).trimmed().toLower();
    }
  }
  return {};
}

MailEvent classifyMessage(const QString &uid, const MailMessage &msg,
                          const QSet<QString> &knownAddresses,
                          bool claimUnknown) {
  MailEvent ev;
  ev.uid = uid;
  ev.to = recipientOf(msg);
  ev.subject = decodeHeaderWord(msg.header(QStringLiteral("subject")));
  const QString sender = msg.header(QStringLiteral("from")).toLower();
  if (!ev.to.isEmpty() && !claimUnknown && !knownAddresses.contains(ev.to)) {
    ev.kind = MailEvent::Kind::Foreign;
    return ev;
  }
  auto it = kLinkRe.globalMatch(msg.text);
  while (it.hasNext()) {
    const QString url = it.next().captured(0);
    if (!ev.urls.contains(url))
      ev.urls << url;
  }
  if (!ev.urls.isEmpty()) {
    ev.kind = MailEvent::Kind::Link;
    return ev;
  }
  if (kNoDataRe.match(msg.text).hasMatch()) {
    ev.kind = MailEvent::Kind::NoData;
    return ev;
  }
  Q_UNUSED(sender); // portal mail with neither link nor notice is consumed
                    // like any other unrelated message
  ev.kind = MailEvent::Kind::Unrelated;
  return ev;
}

} // namespace skg::tdvms
