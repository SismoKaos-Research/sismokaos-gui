// One message in, one event out; and how a bare station code becomes a
// network and an instrument in a portal request. Mirrors test_mailbox.py and
// test_client.py.
#include <QtTest>

#include "tdvms/mail.h"
#include "tdvms/portal.h"

using namespace skg::tdvms;

namespace {
const QSet<QString> MINE{"you+a1@gmail.com", "you+a2@gmail.com"};
const QString URL = "https://tdvms.afad.gov.tr/files/abc123.zip";

QByteArray message(const QString &to, const QString &body,
                   const QString &from = "noreply@tdvms.afad.gov.tr",
                   const QString &subject = "TDVMS") {
  return QStringLiteral("To: %1\r\nFrom: %2\r\nSubject: %3\r\n"
                        "Content-Type: text/plain; charset=\"utf-8\"\r\n"
                        "Content-Transfer-Encoding: 7bit\r\n\r\n%4\r\n")
      .arg(to, from, subject, body)
      .toUtf8();
}

MailEvent classify(const QByteArray &raw, bool claimUnknown = false) {
  return classifyMessage("1", parseMessage(raw), MINE, claimUnknown);
}

// The two portal endpoints as a scripted Http.
class FakeHttp : public Http {
public:
  QJsonArray stations;
  QList<QJsonObject> requests;
  HttpResult nextAnswer{200, R"({"Result": 0})", "application/json", {}, false};

  HttpResult postJson(const QUrl &url, const QByteArray &json, int,
                      const QMap<QString, QString> &) override {
    if (url.toString().contains("GetStations")) {
      HttpResult r;
      r.status = 200;
      r.body = QJsonDocument(stations).toJson(QJsonDocument::Compact);
      return r;
    }
    requests << QJsonDocument::fromJson(json).object();
    return nextAnswer;
  }
  HttpResult postBody(const QUrl &, const QByteArray &, int,
                      const QMap<QString, QString> &) override {
    return {};
  }
  HttpResult get(const QUrl &, int) override { return {}; }
  HttpResult getToDevice(const QUrl &, QIODevice *, int,
                         const std::function<bool()> &) override {
    return {};
  }
};

QJsonObject station(const QString &code, const QString &network,
                    const QString &devices) {
  return {{"code", code},
          {"network", network},
          {"deviceH", devices.contains('H')},
          {"deviceL", devices.contains('L')},
          {"deviceN", devices.contains('N')},
          {"latitude", 40.0},
          {"longitude", 30.0}};
}
} // namespace

class TstTdvmsMail : public QObject {
  Q_OBJECT
private slots:
  void linkInPlainText() {
    const MailEvent e =
        classify(message("you+a1@gmail.com", "Indirme linki: " + URL));
    QCOMPARE(e.kind, MailEvent::Kind::Link);
    QCOMPARE(e.urls, QStringList{URL});
    QCOMPARE(e.to, QString("you+a1@gmail.com"));
    QCOMPARE(e.subject, QString("TDVMS"));
  }

  void linkInHtmlPart() {
    const QByteArray raw =
        QStringLiteral(
            "To: you+a1@gmail.com\r\nFrom: noreply@tdvms.afad.gov.tr\r\n"
            "Subject: =?utf-8?B?VERWTVMgxLBuZGlybWU=?=\r\n"
            "Content-Type: multipart/alternative; boundary=\"XYZ\"\r\n\r\n"
            "--XYZ\r\nContent-Type: text/plain\r\n\r\nnothing here\r\n"
            "--XYZ\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Content-Transfer-Encoding: base64\r\n\r\n%1\r\n--XYZ--\r\n")
            .arg(QString::fromLatin1(
                QStringLiteral(
                    "<html><body><a href=\"%1\">indir</a></body></html>")
                    .arg(URL)
                    .toUtf8()
                    .toBase64()))
            .toUtf8();
    const MailEvent e = classify(raw);
    QCOMPARE(e.kind, MailEvent::Kind::Link);
    QVERIFY(e.urls.contains(URL));
    QCOMPARE(e.subject, QString::fromUtf8("TDVMS İndirme"));
  }

  void quotedPrintableAndFoldedHeaders() {
    const QByteArray raw = "To: shared@gmail.com\r\n"
                           "Delivered-To:\r\n you+a2@gmail.com\r\n"
                           "From: noreply@tdvms.afad.gov.tr\r\n"
                           "Content-Type: text/plain; charset=iso-8859-9\r\n"
                           "Content-Transfer-Encoding: quoted-printable\r\n\r\n"
                           "Talep etti=F0iniz istasyon/istasyonlara ait "
                           "veri=\r\n bulunmamaktad=FDr.\r\n";
    const MailMessage m = parseMessage(raw);
    QCOMPARE(recipientOf(m),
             QString("shared@gmail.com")); // To is checked first
    QVERIFY(m.text.contains(QString::fromUtf8("ettiğiniz")));
    MailMessage noTo = m;
    noTo.headers.remove("to");
    QCOMPARE(recipientOf(noTo), QString("you+a2@gmail.com"));
    const MailEvent e = classifyMessage("2", m, MINE, true);
    QCOMPARE(e.kind, MailEvent::Kind::NoData);
  }

  void sameLinkTwiceIsOne() {
    const MailEvent e =
        classify(message("you+a1@gmail.com", URL + "\nve tekrar: " + URL));
    QCOMPARE(e.urls, QStringList{URL});
  }

  void noDataMailIsAnAnswer() {
    const MailEvent e = classify(message(
        "you+a2@gmail.com",
        "Talep ettiginiz istasyon/istasyonlara ait veri bulunmamaktadir."));
    QCOMPARE(e.kind, MailEvent::Kind::NoData);
    QCOMPARE(e.to, QString("you+a2@gmail.com"));
  }

  void foreignAndClaimUnknown() {
    QCOMPARE(classify(message("other+z9@gmail.com", URL)).kind,
             MailEvent::Kind::Foreign);
    QCOMPARE(classify(message("other+z9@gmail.com", URL), true).kind,
             MailEvent::Kind::Link);
    QCOMPARE(classify(message("Name <You+A1@Gmail.com>", URL)).kind,
             MailEvent::Kind::Link);
  }

  void ordinaryMailIsLeftAlone() {
    const MailEvent e =
        classify(message("you+a1@gmail.com", "lunch?", "friend@x.com"));
    QCOMPARE(e.kind, MailEvent::Kind::Unrelated);
  }

  // --- portal -----------------------------------------------------------
  void strongMotionStationRequestedAsOne() {
    FakeHttp http;
    http.stations = {station("ELBA", "TU", "H"), station("XT01", "TK", "N")};
    TdvmsPortal portal(&http);
    const Outcome o = portal.submit(
        "XT01", QDateTime(QDate(2024, 1, 1), QTime(0, 0), QTimeZone::utc()),
        QDateTime(QDate(2024, 1, 22), QTime(0, 0), QTimeZone::utc()),
        "you+a1@gmail.com");
    QCOMPARE(o.kind, Outcome::Kind::Accepted);
    QVERIFY(o.slotTaken());
    QCOMPARE(http.requests.size(), 1);
    const QJsonObject req = http.requests.first();
    QCOMPARE(req.value("networks").toArray(), QJsonArray{"TK"});
    QCOMPARE(req.value("device_codes").toArray(), QJsonArray{"N"});
    QCOMPARE(req.value("start_time").toString(),
             QString("2024-01-01 00:00:00"));
    QCOMPARE(req.value("e_mail").toString(), QString("you+a1@gmail.com"));
  }

  void codeInTwoNetworksResolvesToTU() {
    FakeHttp http;
    http.stations = {station("MADM", "TB", "H"), station("MADM", "TU", "H")};
    TdvmsPortal portal(&http);
    QCOMPARE(portal.stationNetwork("MADM"), QString("TU"));
    QCOMPARE(portal.allStations().size(), 2);
    QCOMPARE(portal.stationCodes(), QStringList{"MADM"});
  }

  void codeSharedWithStrongMotionResolvesToBroadband() {
    for (const QJsonArray &order :
         {QJsonArray{station("XYZ", "TK", "N"), station("XYZ", "KO", "H")},
          QJsonArray{station("XYZ", "KO", "H"), station("XYZ", "TK", "N")}}) {
      FakeHttp http;
      http.stations = order;
      TdvmsPortal portal(&http);
      QCOMPARE(portal.stationNetwork("XYZ"), QString("KO"));
    }
  }

  void outcomesFromPortalAnswers() {
    FakeHttp http;
    http.stations = {station("ELBA", "TU", "H")};
    TdvmsPortal portal(&http);
    const QDateTime t(QDate(2024, 1, 1), QTime(0, 0), QTimeZone::utc());
    http.nextAnswer = {
        200, R"({"Result": 111})", "application/json", {}, false};
    QCOMPARE(portal.submit("ELBA", t, t, "e").kind, Outcome::Kind::Busy);
    http.nextAnswer = {
        200, R"({"Result": 110})", "application/json", {}, false};
    QCOMPARE(portal.submit("ELBA", t, t, "e").kind, Outcome::Kind::Rejected);
    http.nextAnswer = {500, "boom", "text/plain", {}, false};
    QCOMPARE(portal.submit("ELBA", t, t, "e").kind, Outcome::Kind::Rejected);
    http.nextAnswer = {0, {}, {}, "timed out after 180 s", true};
    const Outcome o = portal.submit("ELBA", t, t, "e");
    QCOMPARE(o.kind, Outcome::Kind::AcceptedUnconfirmed);
    QVERIFY(o.slotTaken());
    // unlisted station: permanent
    const Outcome u = portal.submit("BAKC", t, t, "e");
    QCOMPARE(u.kind, Outcome::Kind::Rejected);
    QVERIFY(u.permanent);
    QVERIFY(u.detail.contains("not in the TDVMS station list"));
  }
};

QTEST_GUILESS_MAIN(TstTdvmsMail)
#include "tst_tdvms_mail.moc"
