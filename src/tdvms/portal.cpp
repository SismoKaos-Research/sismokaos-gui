#include "tdvms/portal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>
#include <utility>

namespace skg::tdvms {

const QString kStationsUrl =
    QStringLiteral("https://tdvms.afad.gov.tr/api/Data/GetStations");
const QString kRequestUrl =
    QStringLiteral("https://tdvmservis.afad.gov.tr/GetData");
const QStringList kDefaultNetcodes = {
    QStringLiteral("TU"), QStringLiteral("KO"), QStringLiteral("TB"),
    QStringLiteral("TK")};
const QStringList kStrongMotion = {QStringLiteral("TK")};
const QString kPreferredNetwork = QStringLiteral("TU");

Outcome Outcome::accepted(int code, const QString &detail) {
  Outcome o;
  o.kind = Kind::Accepted;
  o.code = code;
  o.detail = detail.isEmpty()
                 ? QStringLiteral("accepted with Result=%1").arg(code)
                 : detail;
  return o;
}
Outcome Outcome::unconfirmed(const QString &detail) {
  Outcome o;
  o.kind = Kind::AcceptedUnconfirmed;
  o.detail = detail;
  return o;
}
Outcome Outcome::busy(const QString &detail) {
  Outcome o;
  o.kind = Kind::Busy;
  o.detail = detail;
  return o;
}
Outcome Outcome::rejected(const QString &detail, bool permanent) {
  Outcome o;
  o.kind = Kind::Rejected;
  o.detail = detail;
  o.permanent = permanent;
  return o;
}

QString kindName(Outcome::Kind k) {
  switch (k) {
  case Outcome::Kind::Accepted:
    return QStringLiteral("accepted");
  case Outcome::Kind::AcceptedUnconfirmed:
    return QStringLiteral("unconfirmed");
  case Outcome::Kind::Busy:
    return QStringLiteral("busy");
  case Outcome::Kind::Rejected:
    return QStringLiteral("rejected");
  }
  return {};
}

QString PortalStation::devices() const {
  QString d;
  if (deviceH)
    d += 'H';
  if (deviceL)
    d += 'L';
  if (deviceN)
    d += 'N';
  return d;
}

namespace {
// Lower wins when one bare code is listed in several networks.
int rank(const PortalStation &s) {
  if (s.network == kPreferredNetwork)
    return 0;
  return kStrongMotion.contains(s.network) ? 2 : 1;
}
} // namespace

TdvmsPortal::TdvmsPortal(Http *http, int timeoutMs, QStringList netcodes)
    : http_(http), timeoutMs_(timeoutMs), netcodes_(std::move(netcodes)) {}

QList<PortalStation> TdvmsPortal::fetchStations() {
  QJsonObject payload;
  payload.insert("netcodes", QJsonArray::fromStringList(netcodes_));
  payload.insert("deviceCode", QString());
  payload.insert("component", QString());
  const HttpResult r = http_->postJson(
      QUrl(kStationsUrl), QJsonDocument(payload).toJson(QJsonDocument::Compact),
      30000);
  if (!r.ok())
    throw std::runtime_error(
        QStringLiteral("station list unreachable (%1)")
            .arg(r.error.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status)
                                   : r.error)
            .toStdString());
  const QJsonDocument doc = QJsonDocument::fromJson(r.body);
  if (!doc.isArray())
    throw std::runtime_error("station list is not a JSON array");
  QList<PortalStation> out;
  for (const QJsonValue &v : doc.array()) {
    const QJsonObject o = v.toObject();
    PortalStation s;
    s.code = o.value("code").toString();
    s.network = o.value("network").toString(kPreferredNetwork);
    s.deviceH = o.value("deviceH").toBool();
    s.deviceL = o.value("deviceL").toBool();
    s.deviceN = o.value("deviceN").toBool();
    if (o.value("latitude").isDouble() && o.value("longitude").isDouble()) {
      s.latitude = o.value("latitude").toDouble();
      s.longitude = o.value("longitude").toDouble();
      s.hasPosition = true;
    }
    if (!s.code.isEmpty())
      out << s;
  }
  return out;
}

const QMap<QString, PortalStation> &TdvmsPortal::stations() {
  if (!loaded_) {
    all_ = fetchStations();
    stations_.clear();
    for (const PortalStation &s : all_) {
      auto it = stations_.find(s.code);
      if (it != stations_.end() && rank(*it) <= rank(s))
        continue;
      stations_.insert(s.code, s);
    }
    loaded_ = true;
  }
  return stations_;
}

QList<PortalStation> TdvmsPortal::allStations() {
  stations();
  return all_;
}

QStringList TdvmsPortal::stationCodes() { return stations().keys(); }

QString TdvmsPortal::stationNetwork(const QString &station) {
  const auto &table = stations();
  const auto it = table.constFind(station);
  if (it == table.constEnd())
    throw std::out_of_range(
        (station + ": not in the TDVMS station list").toStdString());
  return it->network;
}

QString TdvmsPortal::deviceCode(const QString &station) {
  const auto &table = stations();
  const auto it = table.constFind(station);
  if (it == table.constEnd())
    throw std::out_of_range(
        (station + ": not in the TDVMS station list").toStdString());
  if (it->deviceH)
    return QStringLiteral("H");
  if (it->deviceL)
    return QStringLiteral("L");
  if (it->deviceN)
    return QStringLiteral("N");
  throw std::out_of_range(
      (station + ": listed but carries no H/L/N device").toStdString());
}

Outcome TdvmsPortal::submit(const QString &station, const QDateTime &start,
                            const QDateTime &end, const QString &email) {
  QString device, network;
  try {
    device = deviceCode(station);
    network = stationNetwork(station);
  } catch (const std::out_of_range &e) {
    return Outcome::rejected(QString::fromUtf8(e.what()), true);
  } catch (const std::runtime_error &e) {
    return Outcome::rejected(QString::fromUtf8(e.what()));
  }
  const QString fmt = QStringLiteral("yyyy-MM-dd HH:mm:ss");
  QJsonObject payload;
  payload.insert("start_time", start.toString(fmt));
  payload.insert("end_time", end.toString(fmt));
  payload.insert("data_type", QStringLiteral("mseed"));
  payload.insert("instrument", false);
  payload.insert("networks", QJsonArray{network});
  payload.insert("stations", QJsonArray{station});
  payload.insert("location", QJsonArray{QJsonValue::Null});
  payload.insert("device_codes", QJsonArray{device});
  payload.insert("components", QJsonArray{QJsonArray{"Z", "N", "E"}});
  payload.insert("e_mail", email);
  const HttpResult r = http_->postJson(
      QUrl(kRequestUrl), QJsonDocument(payload).toJson(QJsonDocument::Compact),
      timeoutMs_);
  if (r.timedOut || r.connectionError())
    return Outcome::unconfirmed(
        QStringLiteral("%1 -- no answer, usually accepted").arg(r.error));
  if (r.status != 200)
    return Outcome::rejected(QStringLiteral("HTTP %1: %2")
                                 .arg(r.status)
                                 .arg(QString::fromUtf8(r.body.left(200))));
  const QJsonDocument doc = QJsonDocument::fromJson(r.body);
  if (!doc.isObject())
    return Outcome::rejected(QStringLiteral("unparseable answer: %1")
                                 .arg(QString::fromUtf8(r.body.left(200))));
  const int result = doc.object().value("Result").toInt(-1);
  if (result == kResultBusy)
    return Outcome::busy(QStringLiteral(
        "the portal is still processing this address's last request"));
  if (result == kResultError)
    return Outcome::rejected(
        QStringLiteral("portal returned a general error: %1")
            .arg(QString::fromUtf8(r.body.left(200))));
  return Outcome::accepted(result);
}

} // namespace skg::tdvms
