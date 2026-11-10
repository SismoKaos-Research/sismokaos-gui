// The small map of one case: the epicentre (or the reference point), the
// stations around it, distance rings and the fault traces.
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
  cbi::FaultZone faults_;
};

} // namespace skg
