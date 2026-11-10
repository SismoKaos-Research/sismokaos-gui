// The small map of one case: the epicentre (or the reference point), the
// stations around it, distance rings, the shipped fault layer and, for a
// case whose catalog zone is a fault zone, the traces that define it.
#pragma once

#include <QWidget>

#include "cbi/zones.h"
#include "study/study.h"

namespace skg {

class Theme;

class CaseMap : public QWidget {
  Q_OBJECT
public:
  explicit CaseMap(Theme *theme, QWidget *parent = nullptr);

  // `candidates` are drawn faintly unless the case uses them.
  void setCase(const study::Case &c, const QList<study::StationRef> &candidates,
               const QColor &color);
  void clear();
  QSize minimumSizeHint() const override { return {260, 220}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  Theme *theme_;
  bool has_ = false;
  study::Case case_;
  QList<study::StationRef> candidates_;
  QColor color_;
  // The shipped fault layer (EFSM20) is the backdrop; a fault zone of the
  // case is drawn over it. A layer that could not be read is named on the
  // map instead of being left out silently.
  const cbi::FaultZone *faults_ = nullptr;
  cbi::FaultZone zoneTraces_;
  QString faultNote_;
};

} // namespace skg
