// The study workflow's pages: Cases, Runs and Compare. Each is a Module in
// the sidebar's Study section; all three work on the study that is open
// (AppContext::study).
#pragma once

#include "core/module.h"

namespace skg {

class CasesModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("cases"); }
  QString title() const override { return tr("Cases"); }
  QString description() const override {
    return tr("The earthquakes of this study and the places each one is "
              "studied from");
  }
  Section section() const override { return Section::Study; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

} // namespace skg
