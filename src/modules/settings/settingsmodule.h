// Settings tab: where each tool is found (configured path, project dir,
// resolved executable, probed version) and the interface language.
#pragma once

#include "core/module.h"

namespace skg {

class I18n;

class SettingsModule : public Module {
  Q_OBJECT
public:
  explicit SettingsModule(I18n *i18n, QObject *parent = nullptr);

  QString id() const override { return QStringLiteral("settings"); }
  QString title() const override { return tr("Settings"); }
  QString description() const override {
    return tr("Tool locations and language");
  }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;

private:
  I18n *i18n_;
};

} // namespace skg
