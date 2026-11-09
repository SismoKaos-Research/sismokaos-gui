#include "modules/placeholder/placeholdermodule.h"

#include <QEvent>
#include <QLabel>
#include <QVBoxLayout>

namespace skg {

namespace {

class PlaceholderWidget : public QWidget {
public:
  PlaceholderWidget(const PlaceholderModule *m, QWidget *parent)
      : QWidget(parent), module_(m) {
    title_ = new QLabel(this);
    QFont f = title_->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    title_->setFont(f);
    text_ = new QLabel(this);
    text_->setWordWrap(true);
    tools_ = new QLabel(this);
    auto *layout = new QVBoxLayout(this);
    layout->addStretch(1);
    layout->addWidget(title_, 0, Qt::AlignHCenter);
    layout->addWidget(text_, 0, Qt::AlignHCenter);
    layout->addWidget(tools_, 0, Qt::AlignHCenter);
    layout->addStretch(2);
    retranslate();
  }

protected:
  void changeEvent(QEvent *e) override {
    if (e->type() == QEvent::LanguageChange)
      retranslate();
    QWidget::changeEvent(e);
  }

private:
  void retranslate() {
    title_->setText(module_->title());
    text_->setText(module_->description());
    const QStringList t = module_->requiredTools();
    tools_->setText(t.isEmpty() ? QString()
                                : tr("Tools: %1").arg(t.join(", ")));
  }

  const PlaceholderModule *module_;
  QLabel *title_;
  QLabel *text_;
  QLabel *tools_;
};

} // namespace

PlaceholderModule::PlaceholderModule(QString id, std::function<QString()> title,
                                     std::function<QString()> description,
                                     QStringList tools, QObject *parent)
    : Module(parent), id_(std::move(id)), title_(std::move(title)),
      description_(std::move(description)), tools_(std::move(tools)) {}

QWidget *PlaceholderModule::createWidget(AppContext &, QWidget *parent) {
  return new PlaceholderWidget(this, parent);
}

} // namespace skg
