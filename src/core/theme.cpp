#include "core/theme.h"

#include "core/ui.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSvgRenderer>

#include <algorithm>

namespace skg {

const QString Theme::kSettingMode = QStringLiteral("ui/theme");

namespace {

const QHash<QString, QString> &lightTokens() {
  static const QHash<QString, QString> t = {
      {"bg", "#F3F5F8"},         {"surface", "#FFFFFF"},
      {"surface2", "#F7F9FB"},   {"border", "#D9DFE6"},
      {"text", "#1B2430"},       {"muted", "#667085"},
      {"accent", "#0F766E"},     {"accentHover", "#0B8A80"},
      {"onAccent", "#FFFFFF"},   {"side", "#16202B"},
      {"sideText", "#C3CCD8"},   {"sideMuted", "#7C8A9B"},
      {"sideActive", "#22303F"}, {"field", "#FFFFFF"},
      {"disabledBg", "#EEF1F4"}, {"grid", "#E8EBEF"},
      {"link", "#0F766E"},       {"mode", "light"},
      {"tip", "#16202B"},        {"tipText", "#F3F5F8"},
      {"tipBorder", "#16202B"}};
  return t;
}

const QHash<QString, QString> &darkTokens() {
  static const QHash<QString, QString> t = {
      {"bg", "#12161C"},         {"surface", "#1A2029"},
      {"surface2", "#1F2630"},   {"border", "#2B3440"},
      {"text", "#E3E8EE"},       {"muted", "#8D98A7"},
      {"accent", "#2BB3A6"},     {"accentHover", "#3CC7BA"},
      {"onAccent", "#06211E"},   {"side", "#0C1015"},
      {"sideText", "#B8C2CE"},   {"sideMuted", "#6B7786"},
      {"sideActive", "#1B242F"}, {"field", "#141920"},
      {"disabledBg", "#171C24"}, {"grid", "#2C333D"},
      {"link", "#3CC7BA"},       {"mode", "dark"},
      {"tip", "#263140"},        {"tipText", "#E3E8EE"},
      {"tipBorder", "#3D4A5B"}};
  return t;
}

QString modeName(Theme::Mode m) {
  switch (m) {
  case Theme::Mode::Light:
    return QStringLiteral("light");
  case Theme::Mode::Dark:
    return QStringLiteral("dark");
  case Theme::Mode::System:
    break;
  }
  return QStringLiteral("system");
}

QPixmap renderIcon(const QString &id, const QColor &color) {
  QFile f(QStringLiteral(":/theme/icons/%1.svg").arg(id));
  if (!f.open(QIODevice::ReadOnly)) {
    f.setFileName(QStringLiteral(":/theme/icons/module.svg"));
    if (!f.open(QIODevice::ReadOnly))
      return {};
  }
  QByteArray svg = f.readAll();
  svg.replace("#000", color.name().toLatin1());
  QPixmap pm(72, 72);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  QSvgRenderer r(svg);
  r.render(&p);
  return pm;
}

} // namespace

namespace {
Theme *g_current = nullptr;
}

Theme *Theme::current() { return g_current; }

Theme::~Theme() {
  if (g_current == this)
    g_current = nullptr;
}

Theme::Theme(QSettings *settings, QObject *parent)
    : QObject(parent), settings_(settings) {
  g_current = this;
  const QString saved = settings_->value(kSettingMode).toString();
  if (saved == QLatin1String("light"))
    mode_ = Mode::Light;
  else if (saved == QLatin1String("dark"))
    mode_ = Mode::Dark;
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this] {
            if (mode_ == Mode::System)
              apply();
          });
}

void Theme::setMode(Mode m) {
  if (m == mode_)
    return;
  mode_ = m;
  settings_->setValue(kSettingMode, modeName(m));
  apply();
}

bool Theme::dark() const {
  if (mode_ == Mode::System)
    return QGuiApplication::styleHints()->colorScheme() ==
           Qt::ColorScheme::Dark;
  return mode_ == Mode::Dark;
}

const QHash<QString, QString> &Theme::tokens() const {
  return dark() ? darkTokens() : lightTokens();
}

QColor Theme::color(const QString &token) const {
  return QColor(tokens().value(token, QStringLiteral("#FF00FF")));
}

QIcon Theme::icon(const QString &id, const QColor &color) const {
  return QIcon(renderIcon(id, color));
}

QIcon Theme::navIcon(const QString &id) const {
  QIcon ic;
  ic.addPixmap(renderIcon(id, color(QStringLiteral("sideText"))), QIcon::Normal,
               QIcon::Off);
  ic.addPixmap(renderIcon(id, Qt::white), QIcon::Normal, QIcon::On);
  return ic;
}

QColor Theme::lightColor(const QString &token) {
  return QColor(lightTokens().value(token, QStringLiteral("#FF00FF")));
}

QList<QColor> Theme::lightSeriesColors() { return seriesPalette(false); }

QList<QColor> Theme::seriesColors() const { return seriesPalette(dark()); }

QList<QColor> Theme::seriesPalette(bool dark) {
  // Validated per mode against the chart surface (lightness band, chroma,
  // colour-vision separation); fixed order, never cycled.
  static const char *const light[] = {"#2a78d6", "#eb6834", "#1baf7a",
                                      "#eda100", "#e87ba4", "#008300",
                                      "#6250d6", "#e34948"};
  static const char *const darkc[] = {"#3987e5", "#d95926", "#199e70",
                                      "#c98500", "#d55181", "#008300",
                                      "#9085e9", "#e66767"};
  QList<QColor> out;
  for (const char *c : dark ? darkc : light)
    out << QColor(QLatin1String(c));
  return out;
}

QColor Theme::statusColor(const QString &name) const {
  if (name == QLatin1String("good"))
    return QColor(QStringLiteral("#0ca30c"));
  if (name == QLatin1String("warning"))
    return QColor(QStringLiteral("#fab219"));
  if (name == QLatin1String("serious"))
    return QColor(QStringLiteral("#ec835a"));
  return color(QStringLiteral("muted"));
}

void Theme::apply() {
  auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!app)
    return;
  // One style on every platform, so the style sheet means the same thing.
  if (!app->style() || app->style()->objectName().compare(
                           QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  const auto c = [this](const char *t) { return color(QLatin1String(t)); };
  QPalette pal;
  pal.setColor(QPalette::Window, c("bg"));
  pal.setColor(QPalette::WindowText, c("text"));
  pal.setColor(QPalette::Base, c("surface"));
  pal.setColor(QPalette::AlternateBase, c("surface2"));
  pal.setColor(QPalette::Text, c("text"));
  pal.setColor(QPalette::Button, c("surface"));
  pal.setColor(QPalette::ButtonText, c("text"));
  pal.setColor(QPalette::BrightText, Qt::white);
  pal.setColor(QPalette::PlaceholderText, c("muted"));
  pal.setColor(QPalette::Highlight, c("accent"));
  pal.setColor(QPalette::HighlightedText, c("onAccent"));
  pal.setColor(QPalette::Link, c("link"));
  pal.setColor(QPalette::LinkVisited, c("link"));
  pal.setColor(QPalette::ToolTipBase, c("tip"));
  pal.setColor(QPalette::ToolTipText, c("tipText"));
  pal.setColor(QPalette::Light, c("surface"));
  pal.setColor(QPalette::Midlight, c("surface2"));
  pal.setColor(QPalette::Mid, c("border"));
  pal.setColor(QPalette::Dark, c("border"));
  pal.setColor(QPalette::Shadow, c("side"));
  for (QPalette::ColorRole role :
       {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
    pal.setColor(QPalette::Disabled, role, c("muted"));
  app->setPalette(pal);

  QFile f(QStringLiteral(":/theme/theme.qss"));
  QString qss;
  if (f.open(QIODevice::ReadOnly))
    qss = QString::fromUtf8(f.readAll());
  // Longest names first, so "@surface2" is not cut by "@surface".
  QStringList names = tokens().keys();
  std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
    return a.size() > b.size();
  });
  for (const QString &name : names)
    qss.replace(QLatin1Char('@') + name, tokens().value(name));
  app->setStyleSheet(qss);
  emit changed();
}

} // namespace skg
