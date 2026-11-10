#include "modules/study/casemap.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <exception>

#include "core/theme.h"
#include "core/ui.h"

namespace skg {

namespace {

GeoLegend::Entry markerEntry(GeoMarker::Kind kind, GeoMarker::State state,
                             const QString &text, double magnitude = 5.5) {
  GeoLegend::Entry e;
  e.marker.kind = kind;
  e.marker.state = state;
  e.marker.magnitude = magnitude;
  e.text = text;
  return e;
}

GeoLegend::Entry textEntry(const QString &text) {
  GeoLegend::Entry e;
  e.glyph = GeoLegend::Entry::Glyph::Text;
  e.text = text;
  return e;
}

QString caseLabel(const study::Case &c) {
  return c.reference || !(c.magnitude > 0)
             ? c.name
             : QStringLiteral("%1 · M %2").arg(c.name).arg(c.magnitude, 0, 'f',
                                                            1);
}

} // namespace

GeoContent caseMapContent(const study::Study &s, const Theme &theme,
                          const CaseMapSpec &spec, QStringList *problems) {
  GeoContent out;
  const QList<QColor> palette = theme.seriesColors();
  const bool compare = spec.selectedId.isEmpty();
  QStringList drawnStations;

  // The selected case first, so that its stations are not repeated grey.
  QList<int> order;
  for (int i = 0; i < int(s.cases.size()); ++i)
    if (s.cases[i].id == spec.selectedId)
      order << i;
  for (int i = 0; i < int(s.cases.size()); ++i)
    if (s.cases[i].id != spec.selectedId)
      order << i;

  for (int i : std::as_const(order)) {
    const study::Case &c = s.cases[i];
    const bool selected = c.id == spec.selectedId;
    const bool lit = compare || selected;
    const QColor color =
        spec.colors.contains(c.id)
            ? spec.colors.value(c.id)
            : palette.isEmpty() ? QColor() : palette[i % palette.size()];
    GeoMarker m;
    m.kind = c.reference ? GeoMarker::Kind::Reference
                         : GeoMarker::Kind::Earthquake;
    m.id = QStringLiteral("case:") + c.id;
    m.label = spec.labels.contains(c.id)
                  ? spec.labels.value(c.id)
                  : selected ? caseLabel(c) : c.name;
    m.tip = QStringLiteral("%1 · %2")
                .arg(caseLabel(c),
                     c.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")) +
                         QStringLiteral(" UTC"));
    m.lat = c.lat;
    m.lon = c.lon;
    m.magnitude = c.reference ? 0 : c.magnitude;
    m.color = color;
    m.dim = !lit;
    m.strong = lit;
    out.markers << m;

    if (c.zone.kind == study::Zone::Kind::Circle) {
      GeoZone z;
      z.lat = c.lat;
      z.lon = c.lon;
      z.radiusKm = c.zone.radiusKm;
      z.color = color;
      z.dotted = !lit;
      z.filled = selected;
      out.zones << z;
    } else if (lit) {
      // A fault zone: the traces its catalog selection is measured from.
      try {
        const cbi::FaultZone zone =
            c.zone.faultGeoJson.isEmpty()
                ? cbi::eafz(c.zone.strands)
                : cbi::loadFaultZone(s.resolved(c.zone.faultGeoJson),
                                     c.zone.strands);
        GeoTraces t;
        t.segments = zone.segments;
        t.color = color;
        out.traces << t;
      } catch (const std::exception &e) {
        if (problems)
          *problems << QString::fromUtf8(e.what());
      }
    }

    if (!spec.stations)
      continue;
    for (const study::StationRef &st : c.stations) {
      if (drawnStations.contains(st.code))
        continue;
      drawnStations << st.code;
      GeoMarker sm;
      sm.kind = GeoMarker::Kind::Station;
      const bool has = !spec.hasData || spec.hasData(st);
      // Beyond the catalog zone of the selected case: flagged, as in the
      // add-case dialog.
      const bool outside = selected &&
                           c.zone.kind == study::Zone::Kind::Circle &&
                           st.distanceKm > c.zone.radiusKm;
      sm.state = outside ? GeoMarker::State::Outside
                 : has   ? GeoMarker::State::Used
                         : GeoMarker::State::NoData;
      sm.id = QStringLiteral("station:") + st.code;
      sm.lat = st.lat;
      sm.lon = st.lon;
      sm.dim = !selected;
      sm.strong = selected;
      if (selected)
        sm.label = outside ? CaseMap::tr("%1 · outside the zone").arg(st.code)
                           : st.code;
      sm.tip = outside
                   ? CaseMap::tr("%1 · %2 km from the epicentre, outside the "
                                 "%3 km catalog zone: the earthquakes counted "
                                 "for this case are those of the zone, not "
                                 "those around this station")
                         .arg(st.code)
                         .arg(st.distanceKm, 0, 'f', 0)
                         .arg(c.zone.radiusKm, 0, 'f', 0)
               : selected
                   ? QStringLiteral("%1 · %2 km%3")
                         .arg(st.code)
                         .arg(st.distanceKm, 0, 'f', 0)
                         .arg(!spec.hasData
                                  ? QString()
                                  : has ? CaseMap::tr(" · in use, has waveforms")
                                        : CaseMap::tr(
                                              " · in use, no waveforms yet"))
                   : QStringLiteral("%1 · %2").arg(st.code, c.name);
      out.markers << sm;
    }
  }
  if (spec.stations)
    for (const study::StationRef &st : spec.candidates) {
      if (drawnStations.contains(st.code))
        continue;
      drawnStations << st.code;
      GeoMarker sm;
      sm.kind = GeoMarker::Kind::Station;
      const bool has = spec.hasData && spec.hasData(st);
      sm.state = has ? GeoMarker::State::HasData : GeoMarker::State::Candidate;
      sm.id = QStringLiteral("station:") + st.code;
      sm.lat = st.lat;
      sm.lon = st.lon;
      // Detail of the selected case: not drawn in the overview of all cases.
      sm.showBelowKm = 800.0;
      sm.tip = QStringLiteral("%1 · %2 km%3")
                   .arg(st.code)
                   .arg(st.distanceKm, 0, 'f', 0)
                   .arg(!spec.hasData
                            ? CaseMap::tr(" · not used")
                            : has ? CaseMap::tr(" · not used, has waveforms")
                                  : CaseMap::tr(" · not used, no waveforms"));
      out.markers << sm;
    }
  return out;
}

// ---- the overview on the Cases page ----

CaseMap::CaseMap(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  title_ = new QLabel(this);
  title_->setObjectName(QStringLiteral("cardTitle"));
  hint_ = new QLabel(this);
  hint_->setObjectName(QStringLiteral("fieldLabel"));
  hint_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  fitAll_ = new QPushButton(this);
  fitAll_->setObjectName(QStringLiteral("mapFitAll"));
  fitCase_ = new QPushButton(this);
  fitCase_->setObjectName(QStringLiteral("mapFitCase"));
  map_ = new GeoMap(theme, this);
  map_->setObjectName(QStringLiteral("caseGeoMap"));
  legend_ = new GeoLegend(theme, this);
  note_ = new QLabel(this);
  note_->setObjectName(QStringLiteral("fieldLabel"));
  note_->setWordWrap(true);
  note_->hide();

  auto *head = new QHBoxLayout;
  head->setSpacing(8);
  head->addWidget(title_);
  head->addWidget(hint_, 1);
  head->addWidget(fitAll_);
  head->addWidget(fitCase_);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addLayout(head);
  layout->addWidget(map_, 1);
  layout->addWidget(note_);
  layout->addWidget(legend_);

  connect(fitAll_, &QPushButton::clicked, this, [this] { map_->fit(false); });
  connect(fitCase_, &QPushButton::clicked, this, [this] { map_->fit(true); });
  connect(map_, &GeoMap::markerClicked, this, [this](const QString &id) {
    if (id.startsWith(QLatin1String("case:")))
      emit caseClicked(id.mid(5));
    else if (id.startsWith(QLatin1String("station:")))
      emit stationClicked(id.mid(8));
  });
  retranslate();
}

void CaseMap::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void CaseMap::retranslate() {
  title_->setText(tr("WHERE"));
  hint_->setText(tr("Selected case in colour · click a marker to select it"));
  fitAll_->setText(tr("All cases"));
  fitAll_->setToolTip(ui::helpText(tr("Show every case of the study.")));
  fitCase_->setText(tr("This case"));
  fitCase_->setToolTip(ui::helpText(
      tr("Zoom to the selected case, its zone and its stations. The "
         "candidate stations around it are drawn at this scale, not in the "
         "overview of all cases.")));
  map_->setPlaceholder(tr("The cases of the study appear here."));
  updateLegend();
}

void CaseMap::updateLegend() {
  QList<GeoLegend::Entry> key;
  key << markerEntry(GeoMarker::Kind::Earthquake, GeoMarker::State::Used,
                     tr("Earthquake"));
  if (hasReference_)
    key << markerEntry(GeoMarker::Kind::Reference, GeoMarker::State::Used,
                       tr("Reference date"));
  key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Used,
                     tr("Station in use"));
  if (hasData_)
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::NoData,
                       tr("In use, no waveforms"));
  if (hasOutside_)
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Outside,
                       tr("In use, outside the zone"));
  if (hasCandidates_) {
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Candidate,
                       tr("Candidate"));
    if (hasData_)
      key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::HasData,
                         tr("Candidate with waveforms"));
  }
  GeoLegend::Entry zone;
  zone.glyph = GeoLegend::Entry::Glyph::Zone;
  zone.text = tr("Catalog zone");
  key << zone;
  GeoLegend::Entry fault;
  fault.glyph = GeoLegend::Entry::Glyph::Fault;
  fault.text = tr("Fault source");
  key << fault;
  key << textEntry(tr("Magnitude"));
  for (double m : {5.0, 6.0, 7.0}) {
    GeoLegend::Entry e = markerEntry(GeoMarker::Kind::Earthquake,
                                     GeoMarker::State::Used,
                                     QString::number(m, 'f', 0), m);
    e.marker.dim = true;
    key << e;
  }
  legend_->setEntries(key);
}

void CaseMap::setStudy(const study::Study &s, const CaseMapSpec &spec) {
  QStringList problems;
  const GeoContent content = caseMapContent(s, *theme_, spec, &problems);
  hasSelection_ = s.findCase(spec.selectedId) != nullptr;
  hasReference_ = std::any_of(s.cases.begin(), s.cases.end(),
                              [](const study::Case &c) { return c.reference; });
  hasData_ = bool(spec.hasData);
  hasCandidates_ = !spec.candidates.isEmpty();
  hasOutside_ = false;
  for (const GeoMarker &m : content.markers)
    hasOutside_ = hasOutside_ || (m.kind == GeoMarker::Kind::Station &&
                                  m.state == GeoMarker::State::Outside);
  fitCase_->setEnabled(hasSelection_);
  fitAll_->setEnabled(!s.cases.isEmpty());
  note_->setText(problems.isEmpty()
                     ? QString()
                     : tr("A fault zone could not be drawn: %1")
                           .arg(problems.join(QStringLiteral("; "))));
  note_->setVisible(!problems.isEmpty());
  // A newly selected case comes into view; edits of the same case leave the
  // view where the user put it.
  const bool refit = spec.selectedId != lastSelected_ &&
                     hasSelection_ && lastSelected_.isEmpty();
  lastSelected_ = spec.selectedId;
  map_->setContent(content, refit);
  updateLegend();
}

void CaseMap::clear() {
  lastSelected_.clear();
  hasSelection_ = false;
  map_->setContent({}, true);
  fitCase_->setEnabled(false);
  fitAll_->setEnabled(false);
}

// ---- the preview of the add-case dialog ----

CasePreview::CasePreview(QWidget *parent)
    : QWidget(parent), theme_(Theme::current()) {
  if (!theme_)
    return;
  map_ = new GeoMap(theme_, this);
  map_->setObjectName(QStringLiteral("previewGeoMap"));
  legend_ = new GeoLegend(theme_, this);
  warning_ = new QLabel(this);
  warning_->setObjectName(QStringLiteral("previewWarning"));
  warning_->setWordWrap(true);
  warning_->hide();
  hint_ = new QLabel(this);
  hint_->setObjectName(QStringLiteral("fieldLabel"));
  hint_->setWordWrap(true);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);
  layout->addWidget(map_, 1);
  layout->addWidget(legend_);
  layout->addWidget(warning_);
  layout->addWidget(hint_);
  setMinimumWidth(360);
  connect(map_, &GeoMap::markerClicked, this, [this](const QString &id) {
    if (id.startsWith(QLatin1String("event:")))
      emit eventClicked(id.mid(6).toInt());
    else if (id.startsWith(QLatin1String("station:")))
      emit stationClicked(id.mid(8));
  });
}

QString CasePreview::warning() const {
  return warning_ ? warning_->text() : QString();
}

void CasePreview::setList(const QList<PreviewEvent> &events,
                          const QList<CoverageStation> &filterStations,
                          double filterKm) {
  if (!map_)
    return;
  GeoContent c;
  c.coverageColor = theme_->color(QStringLiteral("accent"));
  for (const CoverageStation &s : filterStations) {
    GeoZone z;
    z.lat = s.lat;
    z.lon = s.lon;
    z.radiusKm = filterKm;
    c.coverage << z;
    GeoMarker m;
    m.kind = GeoMarker::Kind::Station;
    m.label = s.code;
    m.tip = tr("%1 · the list is limited to earthquakes within %2 km of it")
                .arg(s.code)
                .arg(filterKm, 0, 'f', 0);
    m.lat = s.lat;
    m.lon = s.lon;
    m.strong = true;
    c.markers << m;
  }
  for (const PreviewEvent &e : events) {
    GeoMarker m;
    m.kind = GeoMarker::Kind::Earthquake;
    m.id = QStringLiteral("event:%1").arg(e.row);
    m.tip = e.tip;
    m.lat = e.lat;
    m.lon = e.lon;
    m.magnitude = e.magnitude;
    c.markers << m;
  }
  // Coming back from a chosen earthquake, or a changed list: show it all.
  map_->setContent(c, false);
  map_->fit(false);
  caseMode_ = false;
  warning_->hide();
  warning_->clear();
  hint_->setText(
      events.isEmpty()
          ? tr("No earthquake passes the filters.")
          : filterStations.isEmpty()
                ? tr("The earthquakes of the list. Click one to select it; "
                     "drag to move the map, turn the wheel to zoom.")
                : tr("The earthquakes within %1 km of the stations named. "
                     "Click one to select it.")
                      .arg(filterKm, 0, 'f', 0));
  QList<GeoLegend::Entry> key;
  key << markerEntry(GeoMarker::Kind::Earthquake, GeoMarker::State::Used,
                     tr("Earthquake of the list"));
  if (!filterStations.isEmpty()) {
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Used,
                       tr("Station of the filter"));
    GeoLegend::Entry area;
    area.glyph = GeoLegend::Entry::Glyph::Coverage;
    area.color = c.coverageColor;
    area.text = tr("Within %1 km of one").arg(filterKm, 0, 'f', 0);
    key << area;
  }
  GeoLegend::Entry fault;
  fault.glyph = GeoLegend::Entry::Glyph::Fault;
  fault.text = tr("Fault source");
  key << fault;
  legend_->setEntries(key);
}

void CasePreview::setCase(
    const study::Case &c, const QList<study::StationRef> &candidates,
    const QStringList &ticked,
    const std::function<bool(const study::StationRef &)> &hasData,
    const QList<PreviewEvent> &others) {
  if (!map_)
    return;
  const QList<QColor> palette = theme_->seriesColors();
  const QColor color = palette.isEmpty() ? QColor() : palette.first();
  GeoContent out;
  GeoMarker m;
  m.kind = GeoMarker::Kind::Earthquake;
  m.label = !(c.magnitude > 0)
                ? tr("Epicentre")
                : tr("Epicentre · M %1").arg(c.magnitude, 0, 'f', 1);
  m.lat = c.lat;
  m.lon = c.lon;
  m.magnitude = c.magnitude;
  m.color = color;
  m.strong = true;
  out.markers << m;
  // The rest of the list stays in the background, to pick another one.
  for (const PreviewEvent &e : others) {
    if (std::abs(e.lat - c.lat) < 1e-6 && std::abs(e.lon - c.lon) < 1e-6)
      continue;
    GeoMarker o;
    o.kind = GeoMarker::Kind::Earthquake;
    o.id = QStringLiteral("event:%1").arg(e.row);
    o.tip = e.tip;
    o.lat = e.lat;
    o.lon = e.lon;
    o.magnitude = e.magnitude;
    o.dim = true;
    out.markers << o;
  }
  const bool circle = c.zone.kind == study::Zone::Kind::Circle;
  if (circle) {
    GeoZone z;
    z.lat = c.lat;
    z.lon = c.lon;
    z.radiusKm = c.zone.radiusKm;
    z.color = color;
    out.zones << z;
  }
  QStringList outside;
  bool anyData = false, anyOutside = false;
  for (const study::StationRef &st : candidates) {
    const bool used = ticked.contains(st.code);
    const bool has = hasData && hasData(st);
    anyData = anyData || has;
    const bool out_ = used && circle && st.distanceKm > c.zone.radiusKm;
    GeoMarker sm;
    sm.kind = GeoMarker::Kind::Station;
    sm.state = out_   ? GeoMarker::State::Outside
               : used ? GeoMarker::State::Used
               : has  ? GeoMarker::State::HasData
                      : GeoMarker::State::Candidate;
    sm.id = QStringLiteral("station:") + st.code;
    sm.lat = st.lat;
    sm.lon = st.lon;
    sm.strong = used;
    const QString dist = tr("%1 km").arg(st.distanceKm, 0, 'f', 0);
    if (used)
      sm.label = out_ ? tr("%1 · %2 · outside the zone").arg(st.code, dist)
                      : QStringLiteral("%1 · %2").arg(st.code, dist);
    sm.tip = QStringLiteral("%1 · %2%3")
                 .arg(st.code, dist,
                      used ? tr(" · ticked") : tr(" · click to tick"));
    out.markers << sm;
    if (out_) {
      anyOutside = true;
      outside << tr("%1 (%2)").arg(st.code, dist);
    }
  }
  // The first look at a case fits it; ticking stations leaves the view.
  map_->setContent(out, false);
  map_->fit(true);
  caseMode_ = true;
  warning_->setText(
      outside.isEmpty()
          ? QString()
          : tr("⚠ Outside the %1 km catalog zone: %2. The earthquakes counted "
               "for this case are those of the zone, not those around such a "
               "station. Untick it, or widen the zone on the Cases page.")
                .arg(c.zone.radiusKm, 0, 'f', 0)
                .arg(outside.join(QStringLiteral(", "))));
  warning_->setVisible(!outside.isEmpty());
  hint_->setText(tr("The preview follows what you tick. Click a station to "
                    "tick or untick it, or another earthquake (grey) to "
                    "choose that one."));
  QList<GeoLegend::Entry> key;
  key << markerEntry(GeoMarker::Kind::Earthquake, GeoMarker::State::Used,
                     tr("Epicentre"));
  key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Used,
                     tr("Station in use"));
  key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Candidate,
                     tr("Candidate"));
  if (anyData)
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::HasData,
                       tr("Candidate with waveforms"));
  if (anyOutside)
    key << markerEntry(GeoMarker::Kind::Station, GeoMarker::State::Outside,
                       tr("In use, outside the zone"));
  GeoLegend::Entry zone;
  zone.glyph = GeoLegend::Entry::Glyph::Zone;
  zone.text = tr("Catalog zone");
  key << zone;
  GeoLegend::Entry fault;
  fault.glyph = GeoLegend::Entry::Glyph::Fault;
  fault.text = tr("Fault source");
  key << fault;
  legend_->setEntries(key);
}

} // namespace skg
