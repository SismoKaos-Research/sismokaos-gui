// Settings tab: where each tool is found (configured path, project dir,
// resolved executable, probed version), the interface language and the theme.
#pragma once

#include "core/module.h"

namespace skg {

class I18n;
class Theme;

class SettingsModule : public Module {
  Q_OBJECT
public:
  SettingsModule(I18n *i18n, Theme *theme, QObject *parent = nullptr);

  QString id() const override { return QStringLiteral("settings"); }
  QString title() const override { return tr("Settings"); }
  QString description() const override {
    return tr("Tool locations, language and theme");
  }
  Section section() const override { return Section::Footer; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;

private:
  I18n *i18n_;
  Theme *theme_;
};

} // namespace skg
