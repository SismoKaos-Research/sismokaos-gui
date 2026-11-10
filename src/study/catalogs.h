// Earthquake catalogs a study can use without the user naming a file.
//
// The built-in list (tdvms/catalog.h: M 5+ since 1990 around Türkiye, from
// USGS, shipped with the application and refreshable) is enough to PICK the
// earthquakes of a study. It is far too sparse for the analyses that count
// small earthquakes (the Indicators, the Features calibration): those need a
// dense catalog, which the user either has as a file or downloads here from
// an FDSN event service into the study folder.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "analysis/events.h"
#include "study/study.h"
#include "tdvms/http.h"

namespace skg::study {

// What the built-in list holds, for the line that describes it.
struct BuiltInCatalog {
  int count = 0;
  double minMagnitude = 0;
  QDate first, last;
  QDate updated;         // the day it was last refreshed (or shipped)
  bool refreshed = false; // a refreshed copy is in use, not the shipped one
};
BuiltInCatalog builtInCatalog();
// The built-in list as catalog events (UTC), oldest first.
std::vector<analysis::Event> builtInEvents();

// The earthquakes cases are picked from: the study's catalog when it names
// one, otherwise the built-in list. Throws when the study's file cannot be
// read.
std::vector<analysis::Event> pickingEvents(const Study &study);

// ---- downloading a catalog from an FDSN event service ----

struct EventService {
  QString id;   // "usgs"
  QString name; // "USGS (ComCat)"
  QString url;  // .../fdsnws/event/1/query
};
// The services offered by name. Others can be given by address.
QList<EventService> eventServices();

struct CatalogQuery {
  QString serviceUrl; // .../fdsnws/event/1/query
  QDateTime from, to; // UTC
  double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
  double minMagnitude = 0;
  bool valid() const {
    return !serviceUrl.isEmpty() && from.isValid() && to.isValid() &&
           from < to && minLat < maxLat && minLon < maxLon;
  }
};
// What the study's cases need: a box around every case's zone (its radius or
// buffer, plus `padKm`), from the earliest span start minus the longest
// recipe window (`window_months` of the Indicators recipes; a year when there
// is none) to a day after the last case. Invalid when there are no cases.
CatalogQuery queryForStudy(const Study &study, double padKm = 50.0);

struct FetchHooks {
  std::function<void(const QString &)> log;
  std::function<void(double fraction, const QString &what)> progress;
  std::function<bool()> shouldStop;
};
struct FetchResult {
  QString csvPath;        // <outDir>/catalog_<service>_<from>_<to>.csv
  QString provenancePath; // the same name with .provenance.json
  int events = 0;
  int requests = 0;
  double minMagnitudeFound = 0;
  // The smallest magnitude with at least 1% of the events at or below it
  // rounded to 0.1: a rough idea of how far down the list goes.
  double lowMagnitude = 0;
  bool stopped = false; // nothing was written
};
// Asks the service in pieces of time (halving a piece the service refuses as
// too large), and writes the events in the AFAD export's layout (Date as
// DD/MM/YYYY HH:MM:SS in UTC, Latitude, Longitude, Depth, Type, Magnitude,
// Location, EventID) so every tool reads it, with a provenance record
// beside it. Throws std::runtime_error on a failure; a stop writes nothing.
FetchResult fetchCatalog(tdvms::Http &http, const CatalogQuery &query,
                         const QString &outDir, const QString &serviceId,
                         const FetchHooks &hooks = {});

// One answer of the service in the FDSN "text" format, public for tests.
// Lines that cannot be read are counted in `bad`.
std::vector<analysis::Event> parseFdsnText(const QByteArray &body,
                                           int *bad = nullptr);

} // namespace skg::study
