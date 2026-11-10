// The HTTP client the downloads go through, against a server on this
// machine's loopback: an answer whose connection broke after the status
// line is not a complete body, whatever the status says.
#include <QtTest>

#include <QBuffer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include "tdvms/http.h"

using namespace skg::tdvms;

namespace {

// Answers every connection with `head` and the first `send` bytes of
// `body`, then closes it.
class Server : public QObject {
public:
  QByteArray head;
  QByteArray body;
  int send = -1; // all of it
  int requests = 0;

  Server() {
    QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
      while (QTcpSocket *s = server_.nextPendingConnection()) {
        QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] {
          // One request per connection; its end is the empty line.
          if (!s->property("answered").toBool() &&
              s->peek(8192).contains("\r\n\r\n")) {
            s->setProperty("answered", true);
            ++requests;
            s->write(head);
            s->write(send < 0 ? body : body.left(send));
            s->flush();
            s->disconnectFromHost();
          }
        });
        QObject::connect(s, &QTcpSocket::disconnected, s,
                         &QObject::deleteLater);
      }
    });
  }
  bool listen() { return server_.listen(QHostAddress::LocalHost); }
  QUrl url(const QString &path = QStringLiteral("/x")) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                    .arg(server_.serverPort())
                    .arg(path));
  }

private:
  QTcpServer server_;
};

QByteArray okHead(int length, const QByteArray &type = "application/zip") {
  QByteArray h = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: " +
                 type + "\r\n";
  if (length >= 0)
    h += "Content-Length: " + QByteArray::number(length) + "\r\n";
  return h + "\r\n";
}

} // namespace

class TstTdvmsHttp : public QObject {
  Q_OBJECT
private slots:
  void aWholeAnswerIsOk() {
    Server server;
    QVERIFY(server.listen());
    server.body = QByteArray(5000, 'a');
    server.head = okHead(5000);
    auto http = makeQtHttp();
    const HttpResult r = http->get(server.url(), 10000);
    QCOMPARE(r.status, 200);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(r.ok());
    QCOMPARE(r.body.size(), 5000);
    QCOMPARE(server.requests, 1);
  }

  void anAnswerCutShortIsAnErrorThoughItSaid200() {
    Server server;
    QVERIFY(server.listen());
    server.body = QByteArray(5000, 'a');
    server.head = okHead(5000);
    server.send = 1200; // the connection drops after 1200 of 5000 bytes
    auto http = makeQtHttp();
    const HttpResult r = http->get(server.url(), 10000);
    QCOMPARE(r.status, 200);
    QVERIFY(!r.error.isEmpty());
    QVERIFY(!r.ok());
    QVERIFY(!r.timedOut);
    QVERIFY(r.body.size() < 5000);
    // The same into a file, as archives are fetched.
    QBuffer sink;
    QVERIFY(sink.open(QIODevice::WriteOnly));
    const HttpResult d = http->getToDevice(server.url(), &sink, 10000);
    QCOMPARE(d.status, 200);
    QVERIFY(!d.error.isEmpty());
    QVERIFY(!d.ok());
    QVERIFY(sink.data().size() < 5000);
  }

  void aStreamedAnswerInFullIsOk() {
    Server server;
    QVERIFY(server.listen());
    server.body = QByteArray(300000, 'z');
    server.head = okHead(300000);
    auto http = makeQtHttp();
    QBuffer sink;
    QVERIFY(sink.open(QIODevice::WriteOnly));
    const HttpResult d = http->getToDevice(server.url(), &sink, 10000);
    QVERIFY2(d.ok(), qPrintable(d.error));
    QCOMPARE(sink.data().size(), 300000);
    QCOMPARE(d.contentType, QString("application/zip"));
  }

  void anAnswerWithNoLengthEndsWhenTheServerCloses() {
    // Nothing announced, nothing to check against: what arrived is it.
    Server server;
    QVERIFY(server.listen());
    server.body = QByteArray(700, 'b');
    server.head = okHead(-1);
    auto http = makeQtHttp();
    const HttpResult r = http->get(server.url(), 10000);
    QCOMPARE(r.status, 200);
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.body.size(), 700);
  }

  void anHttpErrorStatusIsNotATransportError() {
    // 404 is how an FDSN service says "no data"; callers judge by the
    // status, and the error text stays for transport failures.
    Server server;
    QVERIFY(server.listen());
    server.body = "nothing here";
    server.head = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n"
                  "Content-Length: 12\r\n\r\n";
    auto http = makeQtHttp();
    const HttpResult r = http->get(server.url(), 10000);
    QCOMPARE(r.status, 404);
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QVERIFY(!r.ok());
    QVERIFY(!r.connectionError());
  }

  void nobodyListeningIsAConnectionError() {
    QUrl nowhere;
    {
      Server server;
      QVERIFY(server.listen());
      nowhere = server.url(); // closed again at the end of this block
    }
    auto http = makeQtHttp();
    const HttpResult r = http->get(nowhere, 10000);
    QCOMPARE(r.status, 0);
    QVERIFY(r.connectionError());
    QVERIFY(!r.ok());
  }
};

QTEST_GUILESS_MAIN(TstTdvmsHttp)
#include "tst_tdvms_http.moc"
