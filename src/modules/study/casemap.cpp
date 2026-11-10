#include "modules/study/casemap.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

#include "analysis/geo.h"
#include "analysis/mapview.h"
#include "core/theme.h"
#include "core/ui.h"

namespace skg {

CaseMap::CaseMap(Theme *theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  connect(theme_, &Theme::changed, this, qOverload<>(&QWidget::update));
}

void CaseMap::setCase(const study::Case &c,
                      const QList<study::StationRef> &candidates,
                      const QColor &color) {
  has_ = true;
  case_ = c;
  candidates_ = candidates;
  color_ = color;
  QStringList problems;
  faults_ = nullptr;
  try {
    faults_ = &cbi::shippedFaults();
  } catch (const std::exception &e) {
    problems << QString::fromUtf8(e.what());
  }
  zoneTraces_ = {};
  if (c.zone.kind == study::Zone::Kind::Fault) {
    try {
      zoneTraces_ =
          c.zone.faultGeoJson.isEmpty()
              ? cbi::eafz(c.zone.strands)
              : cbi::loadFaultZone(c.zone.faultGeoJson, c.zone.strands);
    } catch (const std::exception &e) {
      problems << QString::fromUtf8(e.what());
    }
  }
  faultNote_ = problems.join(QLatin1Char('\n'));
  setToolTip(ui::helpText(
      faultNote_.isEmpty()
          ? tr("The fault lines are seismogenic fault sources of EFSM20, a "
               "hazard model: simplified traces, not an active fault map.")
          : tr("A fault layer could not be read: %1").arg(faultNote_)));
  update();
}

void CaseMap::clear() {
  has_ = false;
  update();
}

void CaseMap::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const auto col = [this](const char *t) {
    return theme_->color(QLatin1String(t));
  };
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  QPainterPath frame;
  frame.addRoundedRect(r, 8, 8);
  p.fillPath(frame, col("surface2"));
  if (!has_) {
    p.setPen(col("border"));
    p.drawPath(frame);
    return;
  }
  p.setClipPath(frame);

  // The view reaches a little past the farthest thing drawn.
  double reachKm = case_.zone.kind == study::Zone::Kind::Circle
                       ? case_.zone.radiusKm
                       : 100.0;
  for (const study::StationRef &s : case_.stations)
    reachKm = std::max(reachKm, s.distanceKm);
  reachKm = std::max(20.0, reachKm) * 1.15;
  const analysis::LocalProjection proj(case_.lat, case_.lon);
  const QPointF c = r.center();
  const double pxPerKm = std::min(r.width(), r.height()) / 2.0 / reachKm;
  const auto toPx = [&](double lat, double lon) {
    const QPointF km = proj.toKm(lat, lon);
    return QPointF(c.x() + km.x() * pxPerKm, c.y() - km.y() * pxPerKm);
  };

  // Faults: the shipped layer as a muted backdrop, then the traces of the
  // case's fault zone (the ones its catalog selection is measured from).
  const QRectF seen = r.adjusted(-60, -60, 60, 60);
  const auto drawTraces = [&](const cbi::FaultZone &z) {
    for (const cbi::FaultZone::Segment &s : z.segments) {
      // The projection is local: skip what lies far from the case.
      if (std::abs(s.lat0 - case_.lat) > 8 || std::abs(s.lon0 - case_.lon) > 12)
        continue;
      const QPointF a = toPx(s.lat0, s.lon0), b = toPx(s.lat1, s.lon1);
      if (seen.intersects(QRectF(a, b).normalized().adjusted(-1, -1, 1, 1)))
        p.drawLine(a, b);
    }
  };
  if (faults_) {
    QColor line = col("muted");
    line.setAlphaF(0.8f);
    p.setPen(QPen(line, 1.2, Qt::SolidLine, Qt::RoundCap));
    drawTraces(*faults_);
  }
  if (!zoneTraces_.empty()) {
    p.setPen(QPen(color_, 1.8, Qt::SolidLine, Qt::RoundCap));
    drawTraces(zoneTraces_);
  }

  // Distance rings, at a round step.
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.85);
  p.setFont(small);
  const double step = analysis::niceScaleKm(reachKm * 2.0);
  for (double d = step; d < reachKm * 1.5; d += step) {
    p.setPen(QPen(col("border"), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, d * pxPerKm, d * pxPerKm);
    p.setPen(col("muted"));
    p.drawText(QPointF(c.x() + 4, c.y() - d * pxPerKm - 4),
               tr("%1 km").arg(d, 0, 'f', 0));
  }
  // The catalog zone, when it is a circle.
  if (case_.zone.kind == study::Zone::Kind::Circle) {
    QColor fill = color_;
    fill.setAlphaF(0.07f);
    p.setPen(QPen(color_, 1.2));
    p.setBrush(fill);
    p.drawEllipse(c, case_.zone.radiusKm * pxPerKm,
                  case_.zone.radiusKm * pxPerKm);
  }

  // Stations: the ones the case uses in ink, the other candidates faint.
  const auto used = [this](const QString &code) {
    for (const study::StationRef &s : case_.stations)
      if (s.code == code)
        return true;
    return false;
  };
  QList<study::StationRef> all = candidates_;
  for (const study::StationRef &s : case_.stations) {
    const bool known =
        std::any_of(all.begin(), all.end(), [&s](const study::StationRef &o) {
          return o.code == s.code;
        });
    if (!known)
      all << s;
  }
  for (int pass = 0; pass < 2; ++pass)
    for (const study::StationRef &s : all) {
      const bool u = used(s.code);
      if (u != (pass == 1))
        continue;
      const QPointF q = toPx(s.lat, s.lon);
      if (!r.contains(q))
        continue;
      const QPolygonF tri(
          {q + QPointF(0, -7), q + QPointF(6.5, 5), q + QPointF(-6.5, 5)});
      p.setPen(QPen(col("surface2"), 2));
      p.setBrush(u ? col("text") : col("border"));
      p.drawPolygon(tri);
      if (!u)
        continue; // candidates are marked, only the used ones are named
      p.setPen(col("text"));
      QFont f = font();
      f.setBold(true);
      p.setFont(f);
      p.drawText(q + QPointF(10, 5), s.code);
    }

  // The epicentre: a star; a reference point: a ring.
  p.setPen(QPen(col("surface2"), 2));
  p.setBrush(color_);
  if (case_.reference) {
    p.drawEllipse(c, 7, 7);
    p.setBrush(col("surface2"));
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, 3, 3);
  } else {
    p.drawPath(analysis::starPath(c, 8));
  }

  // What the fault lines are (the attribution their licence asks for), or
  // that a layer is missing.
  {
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.8);
    p.setFont(f);
    const bool bad = !faultNote_.isEmpty();
    const QString text =
        bad ? tr("A fault layer is missing; hover for details")
            : tr("Fault sources: EFSM20 (hazard model), CC BY 4.0");
    const QFontMetricsF fm(f);
    const QRectF box = r.adjusted(8, 0, -8, -6);
    const QString shown = fm.elidedText(text, Qt::ElideRight, box.width());
    const QRectF tb =
        fm.boundingRect(box, Qt::AlignLeft | Qt::AlignBottom, shown)
            .adjusted(-3, -1, 3, 1);
    QColor halo = col("surface2");
    halo.setAlphaF(0.85f);
    p.setPen(Qt::NoPen);
    p.setBrush(halo);
    p.drawRoundedRect(tb, 3, 3);
    p.setPen(bad ? col("text") : col("muted"));
    p.drawText(box, Qt::AlignLeft | Qt::AlignBottom, shown);
  }

  p.setClipping(false);
  p.setPen(QPen(col("border"), 1));
  p.setBrush(Qt::NoBrush);
  p.drawPath(frame);
}

} // namespace skg
