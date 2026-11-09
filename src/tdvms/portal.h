// The TDVMS portal, as typed outcomes rather than status codes. Port of
// tdvms/client.py. AcceptedUnconfirmed is the one that matters: a submission
// under load dies at ~60 s and is accepted anyway; the claim must be kept.
#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>

#include "tdvms/http.h"

namespace skg::tdvms {

extern const QString
    kStationsUrl; // https://tdvms.afad.gov.tr/api/Data/GetStations
extern const QString kRequestUrl; // https://tdvmservis.afad.gov.tr/GetData
extern const QStringList kDefaultNetcodes; // TU, KO, TB, TK
extern const QStringList kStrongMotion;    // TK
extern const QString kPreferredNetwork;    // TU

enum PortalResult {
  kResultOk = 0,
  kResultQueued = 109,
  kResultError = 110,
  kResultBusy = 111
};

struct Outcome {
  enum class Kind { Accepted, AcceptedUnconfirmed, Busy, Rejected };
  Kind kind = Kind::Rejected;
  QString detail;
  int code = 0;           // the portal's Result on Accepted
  bool permanent = false; // Rejected: a retry cannot fix it
  bool slotTaken() const {
    return kind == Kind::Accepted || kind == Kind::AcceptedUnconfirmed;
  }
  static Outcome accepted(int code, const QString &detail = {});
  static Outcome unconfirmed(const QString &detail);
  static Outcome busy(const QString &detail);
  static Outcome rejected(const QString &detail, bool permanent = false);
};
QString kindName(Outcome::Kind k);

// What the portal lists for one bare code.
struct PortalStation {
  QString code, network;
  bool deviceH = false, deviceL = false, deviceN = false;
  double latitude = 0, longitude = 0;
  bool hasPosition = false;
  QString devices() const; // "H", "HL", ... in H, L, N order
};

// Anything that can submit a request; the supervisor only needs this.
class Portal {
public:
  virtual ~Portal() = default;
  virtual Outcome submit(const QString &station, const QDateTime &start,
                         const QDateTime &end, const QString &email) = 0;
};

class TdvmsPortal : public Portal {
public:
  explicit TdvmsPortal(Http *http, int timeoutMs = 180000,
                       QStringList netcodes = kDefaultNetcodes);

  // The portal's station table, loaded once. Throws std::runtime_error when
  // the list is unreachable. Where a code exists in two networks TU wins,
  // then any broadband, then strong motion.
  const QMap<QString, PortalStation> &stations();
  // Every record the portal returned, including duplicates across networks
  // (for the catalog).
  QList<PortalStation> allStations();
  QStringList stationCodes();
  // Throw std::out_of_range when the portal does not list the code.
  QString stationNetwork(const QString &station);
  QString deviceCode(const QString &station);

  Outcome submit(const QString &station, const QDateTime &start,
                 const QDateTime &end, const QString &email) override;

private:
  QList<PortalStation> fetchStations();

  Http *http_;
  int timeoutMs_;
  QStringList netcodes_;
  bool loaded_ = false;
  QMap<QString, PortalStation> stations_;
  QList<PortalStation> all_;
};

} // namespace skg::tdvms
