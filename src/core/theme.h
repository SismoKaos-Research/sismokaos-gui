// The look of the application: a light and a dark set of colour tokens, the
// style sheet built from them (resources/theme.qss), the palette, and the
// line icons (resources/icons/*.svg, recoloured per use). The mode is
// "follow the system", light or dark, remembered in the settings.
#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QSettings>
#include <QString>

namespace skg {

class Theme : public QObject {
  Q_OBJECT
public:
  enum class Mode { System, Light, Dark };
  Q_ENUM(Mode)

  explicit Theme(QSettings *settings, QObject *parent = nullptr);
  ~Theme() override;
  // The application's theme (the one made last), for a widget that is built
  // where no AppContext reaches, such as a dialog; null before one exists.
  static Theme *current();

  Mode mode() const { return mode_; }
  void setMode(Mode m);
  // The scheme in effect (the system's when the mode is System).
  bool dark() const;
  // Sets the style, palette and style sheet of the application.
  void apply();

  // A colour token: bg, surface, surface2, border, text, muted, accent,
  // accentHover, onAccent, side, sideText, sideMuted, sideActive, field,
  // disabledBg, grid.
  QColor color(const QString &token) const;
  // An icon from resources/icons drawn in one colour, or in two (unchecked
  // and checked) for the sidebar buttons.
  QIcon icon(const QString &id, const QColor &color) const;
  QIcon navIcon(const QString &id) const;
  // The fixed categorical order for series (never cycled); 8 slots.
  QList<QColor> seriesColors() const;
  // The light scheme whatever the mode: a figure written to a file stays
  // white on any theme.
  static QColor lightColor(const QString &token);
  static QList<QColor> lightSeriesColors();
  // Status colours used with a glyph and a label: good, warning, serious.
  QColor statusColor(const QString &name) const;

  static const QString kSettingMode;

signals:
  void changed();

private:
  const QHash<QString, QString> &tokens() const;
  static QList<QColor> seriesPalette(bool dark);

  QSettings *settings_;
  Mode mode_ = Mode::System;
};

} // namespace skg
