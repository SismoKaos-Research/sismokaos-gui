#include "core/i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>

namespace skg {

I18n::I18n(QSettings *settings, QObject *parent)
    : QObject(parent), settings_(settings) {}

QStringList I18n::available() {
  QStringList out;
  const QDir dir(QStringLiteral(":/i18n"));
  for (const QString &f : dir.entryList({QStringLiteral("*.qm")})) {
    // sismokaos-gui_tr.qm -> tr
    const int us = f.lastIndexOf('_');
    const int dot = f.lastIndexOf('.');
    if (us >= 0 && dot > us)
      out << f.mid(us + 1, dot - us - 1);
  }
  if (!out.contains(QStringLiteral("en")))
    out << QStringLiteral("en"); // the source language
  out.sort();
  return out;
}

void I18n::applySaved() {
  QString code = settings_->value(QStringLiteral("ui/language")).toString();
  if (code.isEmpty()) {
    const QString sys = QLocale::system().name().left(2);
    code = available().contains(sys) ? sys : QStringLiteral("en");
  }
  setLanguage(code);
}

void I18n::setLanguage(const QString &code) {
  QCoreApplication::removeTranslator(&app_);
  QCoreApplication::removeTranslator(&qt_);
  if (code != QLatin1String("en")) {
    if (app_.load(QStringLiteral(":/i18n/sismokaos-gui_") + code))
      QCoreApplication::installTranslator(&app_);
    if (qt_.load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"),
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
      QCoreApplication::installTranslator(&qt_);
  }
  current_ = code;
  settings_->setValue(QStringLiteral("ui/language"), code);
  emit languageChanged(code);
}

} // namespace skg
