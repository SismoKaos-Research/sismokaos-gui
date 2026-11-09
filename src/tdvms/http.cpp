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
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
      timedOut = true;
      reply->abort();
    });
    // Idle timeout: a transfer that keeps delivering bytes is not stuck.
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
      timer.start(timeoutMs);
      if (sink) {
        sink->write(reply->readAll());
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
    if (sink)
      sink->write(reply->readAll());
    else
      out.body = reply->readAll();
    out.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    out.timedOut = timedOut;
    if (stopped)
      out.error = QStringLiteral("stopped");
    else if (timedOut)
      out.error = QStringLiteral("timed out after %1 s").arg(timeoutMs / 1000);
    else if (reply->error() != QNetworkReply::NoError && out.status == 0)
      out.error = reply->errorString();
    reply->deleteLater();
    return out;
  }

  QNetworkAccessManager nam_;
};

} // namespace

std::unique_ptr<Http> makeQtHttp() { return std::make_unique<QtHttp>(); }

} // namespace skg::tdvms
