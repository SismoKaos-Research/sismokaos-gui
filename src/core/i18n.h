// Language switching at run time: loads the application's .qm from :/i18n/
// and Qt's own base translation, installs both, remembers the choice.
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTranslator>

namespace skg {

class I18n : public QObject {
  Q_OBJECT
public:
  explicit I18n(QSettings *settings, QObject *parent = nullptr);

  // "tr", "en"; the list is what the build embedded.
  static QStringList available();
  // Where the build embedded Qt's own translation for `code`, when the Qt it
  // was built with has one (src/core/CMakeLists.txt); the file may not exist.
  static QString qtTranslationResource(const QString &code);
  QString current() const { return current_; }
  // Applies the saved language, or the system one when nothing is saved.
  void applySaved();
  void setLanguage(const QString &code);

signals:
  void languageChanged(const QString &code);

private:
  QSettings *settings_;
  QTranslator app_;
  QTranslator qt_;
  QString current_;
};

} // namespace skg
