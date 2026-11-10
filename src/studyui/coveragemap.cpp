#include "studyui/coveragemap.h"

#include <QDateTime>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimeZone>
#include <QVBoxLayout>

#include <cmath>

#include "analysis/geo.h"
#include "core/defaults.h"
#include "core/theme.h"
#include "core/ui.h"
#include "studyui/geomap.h"

namespace skg {

CoverageResult stationCoverage(const std::vector<analysis::Event> &events,
                               const QList<CoverageStation> &stations,
                               double radiusKm, double minMag) {
  CoverageResult out;
  for (int i = 0; i < int(events.size()); ++i) {
    const analysis::Event &e = events[static_cast<size_t>(i)];
    if (!(e.mag + 1e-9 >= minMag))
      continue;
    bool in = false;
    for (const CoverageStation &s : stations) {
      // A cheap bound first: a degree of latitude is about 111 km.
      if (std::abs(s.lat - e.lat) * 110.5 > radiusKm)
        continue;
      if (analysis::haversineKm(s.lat, s.lon, e.lat, e.lon) <= radiusKm) {
        in = true;
        break;
      }
    }
    (in ? out.covered : out.missed).push_back(i);
  }
  return out;
}

CoverageMap::CoverageMap(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  map_ = new GeoMap(theme, this);
  map_->setObjectName(QStringLiteral("coverageGeoMap"));
  map_->fit(true); // the stations and their discs, not the whole catalog
  legend_ = new GeoLegend(theme, this);
  radiusLabel_ = new QLabel(this);
  radiusLabel_->setObjectName(QStringLiteral("fieldLabel"));
  radius_ = new QDoubleSpinBox(this);
  radius_->setObjectName(QStringLiteral("coverageRadius"));
  radius_->setRange(5, 1000);
  radius_->setDecimals(0);
  radius_->setSingleStep(10);
  radius_->setValue(defaults::kStationRadiusKm);
  radiusLabel_->setBuddy(radius_);
  magLabel_ = new QLabel(this);
  magLabel_->setObjectName(QStringLiteral("fieldLabel"));
  minMag_ = new QDoubleSpinBox(this);
  minMag_->setObjectName(QStringLiteral("coverageMinMag"));
  minMag_->setRange(0.0, 9.5);
  minMag_->setDecimals(1);
  minMag_->setSingleStep(0.1);
  minMag_->setValue(5.0);
  magLabel_->setBuddy(minMag_);
  summary_ = new QLabel(this);
  summary_->setObjectName(QStringLiteral("coverageSummary"));
  summary_->setWordWrap(true);

  auto *row = new QHBoxLayout;
  row->addWidget(radiusLabel_);
  row->addWidget(radius_);
  row->addSpacing(12);
  row->addWidget(magLabel_);
  row->addWidget(minMag_);
  row->addStretch(1);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addLayout(row);
  layout->addWidget(summary_);
  layout->addWidget(map_, 1);
  layout->addWidget(legend_);

  connect(radius_, &QDoubleSpinBox::valueChanged, this, [this](double km) {
    rebuild(false);
    emit radiusChanged(km);
  });
  connect(minMag_, &QDoubleSpinBox::valueChanged, this,
          [this] { rebuild(false); });
  connect(map_, &GeoMap::markerClicked, this, [this](const QString &id) {
    if (id.startsWith(QLatin1String("station:")))
      emit stationClicked(id.mid(8));
  });
  connect(theme_, &Theme::changed, this, [this] { rebuild(false); });
  retranslate();
}

void CoverageMap::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void CoverageMap::retranslate() {
  radiusLabel_->setText(tr("Within"));
  radius_->setSuffix(tr(" km"));
  magLabel_->setText(tr("Earthquakes of"));
  minMag_->setPrefix(QStringLiteral("M ≥ "));
  ui::setHelp(radius_,
              tr("How far from a station an earthquake may be and still count "
                 "as covered, in km. The shaded area is everything within "
                 "this distance of at least one station."));
  ui::setHelp(minMag_, tr("Only earthquakes of at least this magnitude are "
                          "drawn and counted."));
  map_->setPlaceholder(tr("No station with known coordinates to draw."));
  rebuild(false);
}

void CoverageMap::setStations(const QList<CoverageStation> &stations) {
  stations_ = stations;
  rebuild(true);
}

void CoverageMap::setEvents(const std::vector<analysis::Event> &events,
                            const QString &source) {
  events_ = events;
  source_ = source;
  rebuild(false);
}

void CoverageMap::setRadiusKm(double km) { radius_->setValue(km); }
double CoverageMap::radiusKm() const { return radius_->value(); }
void CoverageMap::setMinMagnitude(double mag) { minMag_->setValue(mag); }
double CoverageMap::minMagnitude() const { return minMag_->value(); }

QString CoverageMap::summary() const { return summary_->text(); }

void CoverageMap::rebuild(bool refit) {
  const double km = radius_->value();
  result_ = stationCoverage(events_, stations_, km, minMag_->value());
  GeoContent c;
  const QColor accent = theme_->color(QStringLiteral("accent"));
  c.coverageColor = accent;
  for (const CoverageStation &s : stations_) {
    GeoZone z;
    z.lat = s.lat;
    z.lon = s.lon;
    z.radiusKm = km;
    c.coverage << z;
    GeoMarker m;
    m.kind = GeoMarker::Kind::Station;
    m.state = GeoMarker::State::Used;
    m.id = QStringLiteral("station:") + s.code;
    m.label = stations_.size() <= 40 ? s.code : QString();
    m.tip = s.code;
    m.lat = s.lat;
    m.lon = s.lon;
    m.strong = true;
    c.markers << m;
  }
  const auto quake = [this](int index, bool covered) {
    const analysis::Event &e = events_[static_cast<size_t>(index)];
    GeoMarker m;
    m.kind = GeoMarker::Kind::Earthquake;
    m.magnitude = e.mag;
    m.lat = e.lat;
    m.lon = e.lon;
    m.hollow = !covered;
    m.dim = !covered;
    const QString when =
        QDateTime::fromSecsSinceEpoch(qint64(std::floor(e.epoch)),
                                      QTimeZone::UTC)
            .toString(QStringLiteral("yyyy-MM-dd"));
    m.tip = (covered ? tr("M %1 · %2 · %3 · covered")
                     : tr("M %1 · %2 · %3 · not covered"))
                .arg(e.mag, 0, 'f', 1)
                .arg(when, e.place);
    return m;
  };
  for (int i : result_.missed)
    c.markers << quake(i, false);
  for (int i : result_.covered)
    c.markers << quake(i, true);
  // Fit to the stations and their discs, not to every earthquake of the
  // catalog.
  map_->setContent(c, false);
  if (refit)
    map_->fit(true);

  summary_->setText(
      stations_.isEmpty()
          ? tr("No station is selected.")
          : events_.empty()
                ? tr("%n station(s). No earthquake catalog to count from.",
                     nullptr, int(stations_.size()))
                : tr("%1 of %2 earthquakes of M ≥ %3 lie within %4 km of a "
                     "selected station (%5).")
                      .arg(result_.covered.size())
                      .arg(result_.considered())
                      .arg(minMag_->value(), 0, 'f', 1)
                      .arg(km, 0, 'f', 0)
                      .arg(source_));

  QList<GeoLegend::Entry> key;
  GeoLegend::Entry st;
  st.marker.kind = GeoMarker::Kind::Station;
  st.text = tr("Station");
  key << st;
  GeoLegend::Entry area;
  area.glyph = GeoLegend::Entry::Glyph::Coverage;
  area.color = accent;
  area.text = tr("Within %1 km of a station").arg(km, 0, 'f', 0);
  key << area;
  GeoLegend::Entry in;
  in.marker.kind = GeoMarker::Kind::Earthquake;
  in.marker.magnitude = 5.2;
  in.text = tr("Earthquake, covered");
  key << in;
  GeoLegend::Entry outE = in;
  outE.marker.hollow = true;
  outE.marker.dim = true;
  outE.text = tr("Earthquake, not covered");
  key << outE;
  GeoLegend::Entry fault;
  fault.glyph = GeoLegend::Entry::Glyph::Fault;
  fault.text = tr("Fault source");
  key << fault;
  legend_->setEntries(key);
}

} // namespace skg
