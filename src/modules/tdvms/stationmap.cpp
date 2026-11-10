#include "modules/tdvms/stationmap.h"

#include <QEvent>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

#include "core/theme.h"
#include "studyui/geomap.h"

namespace skg {

StationPickMap::StationPickMap(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("stationMapSummary"));
  summary_->setWordWrap(true);
  map_ = new GeoMap(theme, this);
  map_->setObjectName(QStringLiteral("stationGeoMap"));
  legend_ = new GeoLegend(theme, this);
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(8);
  layout->addWidget(summary_);
  layout->addWidget(map_, 1);
  layout->addWidget(legend_);
  connect(map_, &GeoMap::markerClicked, this, [this](const QString &id) {
    if (id.startsWith(QLatin1String("station:")))
      emit toggled(id.mid(8));
  });
  connect(theme_, &Theme::changed, this, [this] { rebuild(false); });
  retranslate();
}

void StationPickMap::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void StationPickMap::retranslate() { rebuild(false); }

void StationPickMap::setStations(const QList<tdvms::CatalogStation> &stations) {
  stations_ = stations;
  rebuild(false);
}

void StationPickMap::setSelected(const QStringList &codes,
                                 const QString &current) {
  const bool changed = codes != selected_;
  selected_ = codes;
  current_ = current;
  rebuild(changed);
}

void StationPickMap::setCase(const QString &name, double lat, double lon,
                             double zoneKm) {
  hasCase_ = true;
  caseName_ = name;
  caseLat_ = lat;
  caseLon_ = lon;
  caseZoneKm_ = zoneKm;
  rebuild(true);
}

void StationPickMap::clearCase() {
  if (!hasCase_)
    return;
  hasCase_ = false;
  rebuild(false);
}

QStringList StationPickMap::unknown() const {
  QStringList out;
  for (const QString &code : selected_)
    if (std::none_of(stations_.begin(), stations_.end(),
                     [&code](const tdvms::CatalogStation &s) {
                       return s.code.compare(code, Qt::CaseInsensitive) == 0;
                     }))
      out << code;
  return out;
}

void StationPickMap::rebuild(bool refit) {
  GeoContent c;
  const auto chosen = [this](const QString &code) {
    return selected_.contains(code, Qt::CaseInsensitive);
  };
  for (const tdvms::CatalogStation &s : stations_) {
    GeoMarker m;
    m.id = QStringLiteral("station:") + s.code;
    m.lat = s.latitude;
    m.lon = s.longitude;
    const QString what =
        s.requestable() ? tr("%1 · network %2 · instruments %3")
                              .arg(s.code, s.network, s.devices)
                        : tr("%1 · network %2 · nothing to request from the "
                             "portal")
                              .arg(s.code, s.network);
    if (chosen(s.code)) {
      m.kind = GeoMarker::Kind::Station;
      m.state = GeoMarker::State::Used;
      m.label = s.code;
      m.strong = true;
      m.tip = what + tr(" · chosen, click to take it out");
    } else {
      m.kind = GeoMarker::Kind::Dot;
      m.tip = what + tr(" · click to choose it");
    }
    c.markers << m;
  }
  if (hasCase_) {
    const QList<QColor> palette = theme_->seriesColors();
    const QColor color = palette.isEmpty() ? QColor() : palette.first();
    GeoMarker m;
    m.kind = GeoMarker::Kind::Earthquake;
    m.magnitude = 6.0;
    m.label = caseName_;
    m.tip = tr("%1: the case the waveforms are wanted for").arg(caseName_);
    m.lat = caseLat_;
    m.lon = caseLon_;
    m.color = color;
    m.strong = true;
    c.markers << m;
    if (caseZoneKm_ > 0) {
      GeoZone z;
      z.lat = caseLat_;
      z.lon = caseLon_;
      z.radiusKm = caseZoneKm_;
      z.color = color;
      c.zones << z;
    }
  }
  map_->setContent(c, false);
  if (refit) {
    if (selected_.isEmpty() && !hasCase_)
      map_->fit(false); // the whole network
    else
      map_->fit(true);
  }

  const QStringList lost = unknown();
  QString text =
      selected_.isEmpty()
          ? tr("No station is chosen. Click a station to choose it; %1 are "
               "listed by the portal.")
                .arg(stations_.size())
          : tr("%n station(s) chosen: %1. Click a station to add it or take "
               "it out.",
               nullptr, int(selected_.size()))
                .arg(selected_.join(QStringLiteral(", ")));
  if (!lost.isEmpty())
    text += QLatin1Char('\n') +
            tr("⚠ Not in the portal's list, so not on the map: %1.")
                .arg(lost.join(QStringLiteral(", ")));
  summary_->setText(text);

  QList<GeoLegend::Entry> key;
  GeoLegend::Entry on;
  on.marker.kind = GeoMarker::Kind::Station;
  on.text = tr("Chosen for download");
  key << on;
  GeoLegend::Entry dot;
  dot.marker.kind = GeoMarker::Kind::Dot;
  dot.text = tr("Station of the portal");
  key << dot;
  if (hasCase_) {
    GeoLegend::Entry q;
    q.marker.kind = GeoMarker::Kind::Earthquake;
    q.marker.magnitude = 5.5;
    q.marker.color = theme_->seriesColors().value(0);
    q.text = tr("The case");
    key << q;
    if (caseZoneKm_ > 0) {
      GeoLegend::Entry z;
      z.glyph = GeoLegend::Entry::Glyph::Zone;
      z.color = theme_->seriesColors().value(0);
      z.text = tr("Its catalog zone");
      key << z;
    }
  }
  GeoLegend::Entry fault;
  fault.glyph = GeoLegend::Entry::Glyph::Fault;
  fault.text = tr("Fault source");
  key << fault;
  legend_->setEntries(key);
}

} // namespace skg
