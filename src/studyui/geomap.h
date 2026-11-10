// The map the pages share: a recessive basemap (country outlines, graticule,
// the shipped fault layer, scale bar) under layers of earthquakes, reference
// dates, stations, zones and coverage discs. It pans with a drag, zooms with
// the wheel, fits its content on a double click, names what is under the
// pointer and reports a click on a marker.
//
// Identity is never carried by colour alone: every kind and every station
// state has its own glyph, and markers that matter are labelled. Labels are
// placed so that they do not cover each other or a marker; where there is no
// room beside the marker the label moves away and a leader line joins them.
#pragma once

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <vector>

#include "cbi/zones.h"

class QPainter;

namespace skg {

class Theme;

struct GeoMarker {
  enum class Kind {
    Earthquake, // filled circle, sized by magnitude
    Reference,  // hollow diamond: a date with no earthquake
    Station,    // triangle, see State
    Dot,        // a small faint point (a station nobody chose yet)
  };
  enum class State {
    Used,      // filled
    Candidate, // hollow
    HasData,   // hollow with a filled core
    NoData,    // hollow, struck through
    Outside,   // filled in the warning colour, with "!"
  };
  Kind kind = Kind::Earthquake;
  State state = State::Used; // stations only
  QString id;                // reported on a click; empty: not clickable
  QString label;             // empty: not labelled
  QString tip;               // shown under the pointer (the label if empty)
  double lat = 0, lon = 0;
  double magnitude = 0; // earthquakes: the size
  QColor color;         // invalid: the text colour
  bool dim = false;     // context: grey, behind the rest
  bool strong = false;  // drawn last and labelled first
  // Detail: drawn only while the view is at most this wide (0: always), so
  // that an overview is not buried under, say, forty candidate stations.
  double showBelowKm = 0;
  bool hollow = false;  // earthquakes: a ring (e.g. one that is not covered)
};

struct GeoZone {
  double lat = 0, lon = 0, radiusKm = 0;
  QColor color;        // invalid: the text colour
  bool dotted = false; // context zones
  bool filled = true;
};

struct GeoTraces {
  std::vector<cbi::FaultZone::Segment> segments;
  QColor color;
};

struct GeoContent {
  QList<GeoMarker> markers;
  QList<GeoZone> zones;
  // Discs drawn as one area (their union), e.g. what a set of stations
  // covers.
  QList<GeoZone> coverage;
  QColor coverageColor;
  QList<GeoTraces> traces; // e.g. the traces a fault zone is measured from
};

class GeoMap : public QWidget {
  Q_OBJECT
public:
  explicit GeoMap(Theme *theme, QWidget *parent = nullptr);

  // `refit` shows all of the new content; otherwise the view is kept once
  // the user has moved it.
  void setContent(const GeoContent &content, bool refit = false);
  const GeoContent &content() const { return content_; }
  // Shows everything that is not a faint dot (or, with `strongOnly`, the
  // strong markers and the zones that are not dotted).
  void fit(bool strongOnly = false);
  // The view: its centre and the kilometres its width spans.
  void setView(double lat, double lon, double widthKm);
  double widthKm() const;
  // Shown where the map has nothing to draw yet.
  void setPlaceholder(const QString &text);

  QPointF toPx(double lat, double lon) const;
  QPointF toLatLon(const QPointF &px) const; // (lat, lon)
  // The id of the clickable marker under `px`, or empty.
  QString markerAt(const QPointF &px) const;
  // Where the labels were placed for the current size and view (tests).
  QList<QRectF> labelRects() const;
  // Where a marker is drawn (it may be moved off a marker it would hide).
  QPointF markerPx(int index) const;
  // Radius in pixels of an earthquake of this magnitude.
  static double quakeRadius(double magnitude);
  // The glyph of a marker, for legends.
  static void drawGlyph(QPainter &p, const GeoMarker &m, const QPointF &at,
                        double r, const Theme &theme);

  QSize minimumSizeHint() const override { return {260, 200}; }
  QSize sizeHint() const override { return {520, 380}; }

signals:
  void markerClicked(const QString &id);

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void leaveEvent(QEvent *) override;
  bool event(QEvent *) override;

private:
  struct Placed {
    QPointF at;     // where the glyph is drawn
    QPointF truth;  // where the place is
    double r = 0;   // glyph radius
    QRectF label;   // empty: no label drawn
    bool leader = false;
    bool hidden = false; // detail that the view is too wide for
  };
  void applyFit();
  void invalidate();
  const std::vector<Placed> &placed() const;
  void renderBase();
  int hit(const QPointF &px) const;
  QColor ink(const GeoMarker &m) const;

  Theme *theme_;
  GeoContent content_;
  QString placeholder_;
  // View: centre and scale.
  double lat0_ = 38.5, lon0_ = 35.5;
  double pxPerKm_ = 0.3;
  bool moved_ = false; // the user panned or zoomed
  bool fitStrong_ = false;
  QPixmap base_;
  bool baseDirty_ = true;
  mutable std::vector<Placed> placed_;
  mutable bool placedDirty_ = true;
  QPointF pressAt_;
  QPointF lastAt_;
  bool pressed_ = false, dragging_ = false;
  QString faultNote_;
};

// The key under a map: glyphs with their names, wrapped to the width.
class GeoLegend : public QWidget {
  Q_OBJECT
public:
  struct Entry {
    enum class Glyph { Marker, Zone, Fault, Coverage, Text };
    Glyph glyph = Glyph::Marker;
    GeoMarker marker; // for Glyph::Marker
    QColor color;
    QString text;
  };
  explicit GeoLegend(Theme *theme, QWidget *parent = nullptr);
  void setEntries(const QList<Entry> &entries);
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return {120, 20}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  int lay(int width, QPainter *p) const;
  Theme *theme_;
  QList<Entry> entries_;
};

} // namespace skg
