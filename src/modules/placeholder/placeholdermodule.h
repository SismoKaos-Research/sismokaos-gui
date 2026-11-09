// A module that only says what will be here. Used for tabs whose design is
// not yet confirmed, so the shell of the application is navigable. Also the
// smallest example of a Module.
#pragma once

#include <functional>

#include "core/module.h"

namespace skg {

class PlaceholderModule : public Module {
  Q_OBJECT
public:
  // title/description are functions so that they are re-evaluated after a
  // language change (they call tr() themselves).
  PlaceholderModule(QString id, std::function<QString()> title,
                    std::function<QString()> description,
                    QStringList tools = {}, QObject *parent = nullptr);

  QString id() const override { return id_; }
  QString title() const override { return title_(); }
  QString description() const override { return description_(); }
  QStringList requiredTools() const override { return tools_; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;

private:
  QString id_;
  std::function<QString()> title_;
  std::function<QString()> description_;
  QStringList tools_;
};

} // namespace skg
