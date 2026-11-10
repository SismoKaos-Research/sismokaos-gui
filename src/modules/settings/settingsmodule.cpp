#include "modules/settings/settingsmodule.h"

#include "core/ui.h"

#include "core/theme.h"

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "core/i18n.h"
#include "tools/toolregistry.h"

namespace skg {

namespace {

// One group box per tool.
class ToolBox : public QGroupBox {
public:
  ToolBox(ToolRegistry *reg, const ToolDesc &desc, QWidget *parent)
      : QGroupBox(parent), reg_(reg), id_(desc.id) {
    exe_ = new QLineEdit(this);
    exe_->setText(reg_->executable(id_));
    exeBrowse_ = new QPushButton(this);
    dir_ = new QLineEdit(this);
    dir_->setText(reg_->projectDir(id_));
    dirBrowse_ = new QPushButton(this);
    probe_ = new QPushButton(this);
    status_ = new QLabel(this);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setWordWrap(true);
    home_ = new QLabel(this);
    home_->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(desc.homepage));
    home_->setOpenExternalLinks(true);

    auto *exeRow = new QHBoxLayout;
    exeRow->addWidget(exe_, 1);
    exeRow->addWidget(exeBrowse_);
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(dir_, 1);
    dirRow->addWidget(dirBrowse_);
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(status_, 1);
    statusRow->addWidget(probe_);

    exeLabel_ = new QLabel(this);
    dirLabel_ = new QLabel(this);
    statusLabel_ = new QLabel(this);
    auto *form = new QFormLayout(this);
    form->addRow(QString(), home_);
    form->addRow(exeLabel_, exeRow);
    form->addRow(dirLabel_, dirRow);
    form->addRow(statusLabel_, statusRow);

    connect(exe_, &QLineEdit::editingFinished, this, [this] {
      reg_->setExecutable(id_, exe_->text().trimmed());
      refresh();
    });
    connect(dir_, &QLineEdit::editingFinished, this, [this] {
      reg_->setProjectDir(id_, dir_->text().trimmed());
      refresh();
    });
    connect(exeBrowse_, &QPushButton::clicked, this, [this] {
      const QString f = QFileDialog::getOpenFileName(
          this, tr("Executable of %1").arg(id_), exe_->text());
      if (!f.isEmpty()) {
        exe_->setText(f);
        reg_->setExecutable(id_, f);
        refresh();
      }
    });
    connect(dirBrowse_, &QPushButton::clicked, this, [this] {
      const QString d = QFileDialog::getExistingDirectory(
          this, tr("Project directory of %1").arg(id_), dir_->text());
      if (!d.isEmpty()) {
        dir_->setText(d);
        reg_->setProjectDir(id_, d);
        refresh();
      }
    });
    connect(probe_, &QPushButton::clicked, this, [this] {
      probe_->setEnabled(false);
      status_->setText(tr("asking %1 for its version...").arg(id_));
      reg_->probeVersion(id_);
      probe_->setEnabled(true);
      refresh();
    });
    retranslate();
    refresh();
  }

protected:
  void changeEvent(QEvent *e) override {
    if (e->type() == QEvent::LanguageChange) {
      retranslate();
      refresh();
    }
    QGroupBox::changeEvent(e);
  }

private:
  void retranslate() {
    setTitle(id_ + QStringLiteral("  -  ") + reg_->desc(id_)->displayName);
    exeBrowse_->setText(tr("Browse..."));
    dirBrowse_->setText(tr("Browse..."));
    probe_->setText(tr("Check"));
    exe_->setPlaceholderText(tr("executable (optional; found on PATH or in "
                                "the project's .venv otherwise)"));
    dir_->setPlaceholderText(
        tr("project directory (the checkout with pyproject.toml)"));
    exeLabel_->setText(tr("Executable"));
    dirLabel_->setText(tr("Project"));
    statusLabel_->setText(tr("Status"));
    ui::setHelp({exe_, exeBrowse_},
                tr("Optional: the tool's command-line executable, for "
                   "running the original tool beside the application. "
                   "Empty looks on the PATH and in the project's .venv. "
                   "Nothing in the other pages needs it."));
    ui::setHelp({dir_, dirBrowse_},
                tr("Optional: the tool's project directory, a checkout "
                   "with its pyproject.toml. Its .venv is searched for the "
                   "executable."));
    probe_->setToolTip(ui::helpText(
        tr("Asks the tool for its version to see whether it can be "
           "run.")));
  }

  void refresh() {
    const ToolStatus st = reg_->status(id_);
    QString text;
    if (!st.found()) {
      text = QStringLiteral("<b>%1</b>").arg(sourceName(st.source));
      if (!reg_->executable(id_).isEmpty())
        text += QStringLiteral("<br>") +
                tr("the configured path is not an executable file");
    } else {
      text = QStringLiteral("<b>%1</b>: %2")
                 .arg(sourceName(st.source), st.program);
      if (!st.prefixArgs.isEmpty())
        text += ' ' + st.prefixArgs.join(' ');
      if (!st.version.isEmpty())
        text += QStringLiteral("<br>") + st.version.toHtmlEscaped();
    }
    status_->setText(text);
    probe_->setEnabled(st.found());
  }

  ToolRegistry *reg_;
  QString id_;
  QLabel *exeLabel_;
  QLabel *dirLabel_;
  QLabel *statusLabel_;
  QLineEdit *exe_;
  QPushButton *exeBrowse_;
  QLineEdit *dir_;
  QPushButton *dirBrowse_;
  QPushButton *probe_;
  QLabel *status_;
  QLabel *home_;
};

class SettingsWidget : public QWidget {
public:
  SettingsWidget(AppContext &ctx, I18n *i18n, Theme *theme, QWidget *parent)
      : QWidget(parent), i18n_(i18n), theme_(theme) {
    lang_ = new QComboBox(this);
    for (const QString &code : I18n::available())
      lang_->addItem(QLocale(code).nativeLanguageName(), code);
    lang_->setCurrentIndex(lang_->findData(i18n_->current()));
    connect(lang_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0)
        i18n_->setLanguage(lang_->itemData(i).toString());
    });

    langBox_ = new QGroupBox(this);
    auto *langLayout = new QHBoxLayout(langBox_);
    langLabel_ = new QLabel(langBox_);
    langLayout->addWidget(langLabel_);
    langLayout->addWidget(lang_, 1);
    themeLabel_ = new QLabel(langBox_);
    themeCombo_ = new QComboBox(this);
    for (Theme::Mode m :
         {Theme::Mode::System, Theme::Mode::Light, Theme::Mode::Dark})
      themeCombo_->addItem(QString(), int(m));
    themeCombo_->setCurrentIndex(themeCombo_->findData(int(theme_->mode())));
    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int i) {
      if (i >= 0)
        theme_->setMode(Theme::Mode(themeCombo_->itemData(i).toInt()));
    });
    connect(theme_, &Theme::changed, this, [this] {
      const QSignalBlocker block(themeCombo_);
      themeCombo_->setCurrentIndex(themeCombo_->findData(int(theme_->mode())));
    });
    connect(i18n_, &I18n::languageChanged, this, [this](const QString &code) {
      const QSignalBlocker block(lang_);
      lang_->setCurrentIndex(lang_->findData(code));
    });
    langLayout->addSpacing(16);
    langLayout->addWidget(themeLabel_);
    langLayout->addWidget(themeCombo_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(langBox_);
    for (const ToolDesc &d : ctx.tools->tools())
      layout->addWidget(new ToolBox(ctx.tools, d, this));
    layout->addStretch(1);
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
    langBox_->setTitle(tr("Interface"));
    langLabel_->setText(tr("Language"));
    themeLabel_->setText(tr("Theme"));
    themeCombo_->setItemText(0, tr("Follow the system"));
    themeCombo_->setItemText(1, tr("Light"));
    themeCombo_->setItemText(2, tr("Dark"));
    ui::setHelp({langLabel_, lang_},
                tr("The language of the application. It changes at "
                   "once."));
    ui::setHelp({themeLabel_, themeCombo_},
                tr("Light or dark colours, or whichever the system is set "
                   "to."));
  }

  I18n *i18n_;
  Theme *theme_;
  QComboBox *lang_;
  QComboBox *themeCombo_;
  QLabel *themeLabel_;
  QGroupBox *langBox_;
  QLabel *langLabel_;
};

} // namespace

SettingsModule::SettingsModule(I18n *i18n, Theme *theme, QObject *parent)
    : Module(parent), i18n_(i18n), theme_(theme) {}

QWidget *SettingsModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new SettingsWidget(ctx, i18n_, theme_, parent);
}

} // namespace skg
