// Blocking HTTP for the worker thread, behind an interface so the portal,
// fetch, FDSN and notification code can be tested without a network.
#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QMap>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace skg::tdvms {

struct HttpResult {
  int status = 0;  // HTTP status, 0 when no answer
  QByteArray body; // for post/get without a sink
  QString contentType;
  // Transport error text, empty on success. It is also set when the
  // connection broke after a 2xx/3xx status line, or the body is shorter
  // than its Content-Length: the body is then incomplete, whatever the
  // status says. An HTTP error status (4xx, 5xx) alone leaves it empty.
  QString error;
  bool timedOut = false;
  bool connectionError() const { return status == 0 && !error.isEmpty(); }
  // A complete answer with a 2xx status.
  bool ok() const { return status >= 200 && status < 300 && error.isEmpty(); }
};

class Http {
public:
  virtual ~Http() = default;
  virtual HttpResult postJson(const QUrl &url, const QByteArray &json,
                              int timeoutMs,
                              const QMap<QString, QString> &headers = {}) = 0;
  virtual HttpResult postBody(const QUrl &url, const QByteArray &body,
                              int timeoutMs,
                              const QMap<QString, QString> &headers) = 0;
  virtual HttpResult get(const QUrl &url, int timeoutMs) = 0;
  // Streams the body to `sink` (contentType and status are still filled; the
  // body stays empty). `shouldStop` is polled between chunks.
  virtual HttpResult
  getToDevice(const QUrl &url, QIODevice *sink, int timeoutMs,
              const std::function<bool()> &shouldStop = {}) = 0;
};

// Qt's QNetworkAccessManager driven by a local event loop. Create it in the
// thread that uses it.
std::unique_ptr<Http> makeQtHttp();

} // namespace skg::tdvms
