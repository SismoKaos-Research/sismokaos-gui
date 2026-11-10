// The theme: every token of the style sheet is substituted, the icons
// render, the mode is remembered.
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "core/theme.h"

using namespace skg;

class TstTheme : public QObject {
  Q_OBJECT
private slots:
  void styleSheetHasNoTokensLeft();
  void modeIsRemembered();
  void iconsRender();
  void seriesColorsAreDistinct();
};

void TstTheme::styleSheetHasNoTokensLeft() {
  QTemporaryDir dir;
  QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
  Theme t(&s);
  for (Theme::Mode m : {Theme::Mode::Light, Theme::Mode::Dark}) {
    t.setMode(m);
    t.apply();
    const QString qss = qApp->styleSheet();
    QVERIFY(qss.size() > 1000);
    QVERIFY2(!qss.contains(QLatin1Char('@')), "an unknown @token is left");
    QCOMPARE(t.dark(), m == Theme::Mode::Dark);
    QVERIFY(
        qss.contains(m == Theme::Mode::Dark ? "up-dark.svg" : "up-light.svg"));
  }
  QVERIFY(t.color("text") != t.color("bg"));
}

void TstTheme::modeIsRemembered() {
  QTemporaryDir dir;
  const QString path = dir.filePath("s.ini");
  {
    QSettings s(path, QSettings::IniFormat);
    Theme t(&s);
    QCOMPARE(t.mode(), Theme::Mode::System);
    t.setMode(Theme::Mode::Dark);
  }
  QSettings s(path, QSettings::IniFormat);
  Theme t(&s);
  QCOMPARE(t.mode(), Theme::Mode::Dark);
}

void TstTheme::iconsRender() {
  QTemporaryDir dir;
  QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
  Theme t(&s);
  for (const char *id : {"tdvms", "features", "classification", "analysis",
                         "indicators", "earlywarning", "settings", "cases",
                         "runs", "compare", "no-such-icon"}) {
    const QImage img =
        t.icon(QLatin1String(id), Qt::red).pixmap(32, 32).toImage();
    QVERIFY(!img.isNull());
    bool painted = false;
    for (int y = 0; y < img.height() && !painted; ++y)
      for (int x = 0; x < img.width() && !painted; ++x)
        painted = qAlpha(img.pixel(x, y)) > 0;
    QVERIFY2(painted, id);
  }
}

void TstTheme::seriesColorsAreDistinct() {
  QTemporaryDir dir;
  QSettings s(dir.filePath("s.ini"), QSettings::IniFormat);
  Theme t(&s);
  for (Theme::Mode m : {Theme::Mode::Light, Theme::Mode::Dark}) {
    t.setMode(m);
    const QList<QColor> c = t.seriesColors();
    QCOMPARE(c.size(), 8);
    QSet<QRgb> seen;
    for (const QColor &x : c)
      seen.insert(x.rgb());
    QCOMPARE(seen.size(), 8);
  }
}

QTEST_MAIN(TstTheme)
#include "tst_theme.moc"
