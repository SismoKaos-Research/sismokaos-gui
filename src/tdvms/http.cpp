#include "tdvms/http.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace skg::tdvms {

namespace {

class QtHttp : public Http {
public:
  HttpResult postJson(const QUrl &url, const QByteArray &json, int timeoutMs,
                      const QMap<QString, QString> &headers) override {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    apply(req, headers);
    return run(nam_.post(req, json), timeoutMs, nullptr, {});
  }

  HttpResult postBody(const QUrl &url, const QByteArray &body, int timeoutMs,
                      const QMap<QString, QString> &headers) override {
    QNetworkRequest req(url);
    apply(req, headers);
    return run(nam_.post(req, body), timeoutMs, nullptr, {});
  }

  HttpResult get(const QUrl &url, int timeoutMs) override {
    return run(nam_.get(QNetworkRequest(url)), timeoutMs, nullptr, {});
  }

  HttpResult getToDevice(const QUrl &url, QIODevice *sink, int timeoutMs,
                         const std::function<bool()> &shouldStop) override {
    return run(nam_.get(QNetworkRequest(url)), timeoutMs, sink, shouldStop);
  }

private:
  static void apply(QNetworkRequest &req,
                    const QMap<QString, QString> &headers) {
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    for (auto it = headers.begin(); it != headers.end(); ++it)
      req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
  }

  HttpResult run(QNetworkReply *reply, int timeoutMs, QIODevice *sink,
                 const std::function<bool()> &shouldStop) {
    HttpResult out;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timedOut = false, stopped = false;
    qint64 received = 0;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
      timedOut = true;
      reply->abort();
    });
    // Idle timeout: a transfer that keeps delivering bytes is not stuck.
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
      timer.start(timeoutMs);
      if (sink) {
        const QByteArray part = reply->readAll();
        received += part.size();
        sink->write(part);
        if (shouldStop && shouldStop()) {
          stopped = true;
          reply->abort();
        }
      }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    if (!reply->isFinished())
      loop.exec();
    timer.stop();
    if (sink) {
      const QByteArray rest = reply->readAll();
      received += rest.size();
      sink->write(rest);
    } else {
      out.body = reply->readAll();
      received = out.body.size();
    }
    out.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    out.timedOut = timedOut;
    if (stopped)
      out.error = QStringLiteral("stopped");
    else if (timedOut)
      out.error = QStringLiteral("timed out after %1 s").arg(timeoutMs / 1000);
    else if (reply->error() != QNetworkReply::NoError && out.status < 400)
      // No answer at all, or one that broke off after its status line: a
      // 200 whose connection dropped half way is not a complete body.
      out.error = reply->errorString();
    else if (out.status >= 200 && out.status < 300) {
      // The length the server announced, when the body comes as it was sent
      // (Qt inflates a compressed one, whose announced length is then not
      // the one received).
      bool known = false;
      const qint64 announced =
          reply->header(QNetworkRequest::ContentLengthHeader)
              .toLongLong(&known);
      if (known && announced >= 0 && received != announced &&
          !reply->hasRawHeader("Content-Encoding"))
        out.error = QStringLiteral("incomplete: %1 of %2 bytes arrived")
                        .arg(received)
                        .arg(announced);
    }
    reply->deleteLater();
    return out;
  }

  QNetworkAccessManager nam_;
};

} // namespace

std::unique_ptr<Http> makeQtHttp() { return std::make_unique<QtHttp>(); }

} // namespace skg::tdvms
