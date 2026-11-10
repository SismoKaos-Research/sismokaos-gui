#include "modules/tdvms/fdsnpane.h"

#include "core/ui.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/tdvms/ledgermodel.h"
#include "tdvms/fdsn.h"
#include "tdvms/plan.h"

namespace skg {

using namespace tdvms;

FdsnPane::FdsnPane(JobModel *jobs, QWidget *parent)
    : QWidget(parent), jobs_(jobs) {
  model_ = new LedgerModel(this);
  table_ = new QTableView(this);
  table_->setModel(model_);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(LedgerModel::Note,
                                                   QHeaderView::Stretch);
  table_->setColumnHidden(LedgerModel::Email, true);
  table_->setColumnHidden(LedgerModel::Submitted, true);

  station_ = new QLineEdit(this);
  station_->setPlaceholderText(QStringLiteral("SVRC"));
  network_ = new QLineEdit(QStringLiteral("KO"), this);
  channels_ = new QLineEdit(QStringLiteral("BH?"), this);
  service_ = new QLineEdit(kKoeri, this);
  start_ = new QDateEdit(QDate::currentDate().addMonths(-1), this);
  end_ = new QDateEdit(QDate::currentDate(), this);
  for (QDateEdit *d : {start_, end_}) {
    d->setCalendarPopup(true);
    d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  }
  retries_ = new QSpinBox(this);
  retries_->setRange(1, 20);
  retries_->setValue(5);
  planOnly_ = new QCheckBox(this);
  retryFailed_ = new QCheckBox(this);
  go_ = new QPushButton(this);
  stop_ = new QPushButton(this);
  where_ = new QLabel(this);
  where_->setWordWrap(true);
  why_ = new QLabel(this);
  why_->setObjectName(QStringLiteral("fdsnWhy"));
  why_->setWordWrap(true);
  station_->setObjectName(QStringLiteral("fdsnStation"));
  network_->setObjectName(QStringLiteral("fdsnNetwork"));
  start_->setObjectName(QStringLiteral("fdsnStart"));
  end_->setObjectName(QStringLiteral("fdsnEnd"));
  planOnly_->setObjectName(QStringLiteral("fdsnPlanOnly"));
  go_->setObjectName(QStringLiteral("fdsnGo"));

  auto *form = new QFormLayout;
  form->addRow(tr("Station"), station_);
  form->addRow(tr("Network"), network_);
  form->addRow(tr("Channels"), channels_);
  form->addRow(tr("Service"), service_);
  form->addRow(tr("First day"), start_);
  form->addRow(tr("Day after the last"), end_);
  form->addRow(tr("Tries per day"), retries_);
  form->addRow(QString(), planOnly_);
  form->addRow(QString(), retryFailed_);
  auto *buttons = new QHBoxLayout;
  buttons->addWidget(go_);
  buttons->addWidget(stop_);
  buttons->addStretch(1);
  form->addRow(QString(), buttons);
  form->addRow(QString(), why_);
  form->addRow(QString(), where_);

  auto *left = new QWidget(this);
  left->setLayout(form);
  left->setMaximumWidth(420);
  auto *layout = new QHBoxLayout(this);
  layout->addWidget(left);
  layout->addWidget(table_, 1);

  connect(go_, &QPushButton::clicked, this, &FdsnPane::start);
  connect(station_, &QLineEdit::textChanged, this, &FdsnPane::updateButtons);
  connect(station_, &QLineEdit::textChanged, this,
          [this] { emit stationChanged(station()); });
  connect(service_, &QLineEdit::editingFinished, this,
          &FdsnPane::defaultsChanged);
  connect(channels_, &QLineEdit::editingFinished, this,
          &FdsnPane::defaultsChanged);
  connect(start_, &QDateEdit::dateChanged, this, &FdsnPane::updateButtons);
  connect(end_, &QDateEdit::dateChanged, this, &FdsnPane::updateButtons);
  connect(stop_, &QPushButton::clicked, this, [this] {
    if (running_)
      running_->cancel();
  });
  retranslate();
  updateButtons();
}

void FdsnPane::setCampaign(const Campaign &c) {
  campaign_ = c;
  const QString state = c.ledgerPath.isEmpty() ? QString() : fdsnStatePath(c);
  model_->setLedgerPath(state);
  where_->setText(c.ledgerPath.isEmpty()
                      ? QString()
                      : tr("Days are recorded in %1 and filed under %2.")
                            .arg(state, fdsnDefaultOut(c)));
  updateButtons();
}

void FdsnPane::reload() { model_->reload(); }

void FdsnPane::prefill(const QString &station, const QString &network,
                       const QDate &firstDay, const QDate &dayAfterLast) {
  station_->setText(station);
  if (!network.isEmpty())
    network_->setText(network);
  if (firstDay.isValid())
    start_->setDate(firstDay);
  if (dayAfterLast.isValid())
    end_->setDate(dayAfterLast);
  updateButtons();
}

QString FdsnPane::station() const { return station_->text().trimmed(); }
QString FdsnPane::service() const { return service_->text().trimmed(); }
QString FdsnPane::channels() const { return channels_->text().trimmed(); }

QString FdsnPane::blockedReason() const {
  if (campaign_.ledgerPath.isEmpty())
    return tr("Choose or create a campaign first: the days are recorded in "
              "it.");
  if (normaliseStation(station_->text()).isEmpty())
    return tr("Give the station code.");
  if (start_->date() >= end_->date())
    return tr("The day after the last must be later than the first day.");
  return {};
}

void FdsnPane::start() {
  if (running_ || !blockedReason().isEmpty())
    return;
  const QString station = normaliseStation(station_->text());
  const QString statePath = fdsnStatePath(campaign_),
                outDir = fdsnDefaultOut(campaign_);
  const QString network = network_->text().trimmed().toUpper(),
                channels = channels_->text().trimmed(),
                service = service_->text().trimmed();
  const QList<QDate> days = daysBetween(start_->date(), end_->date());
  FdsnOptions opt;
  opt.retries = retries_->value();
  const bool planOnly = planOnly_->isChecked(),
             retryFailed = retryFailed_->isChecked();
  QPointer<FdsnPane> self(this);
  auto *job = new TaskJob(
      tr("fdsn %1").arg(station), QStringLiteral("tdvms"),
      tr("%n day(s) of %1 from %2", nullptr, days.size()).arg(station, service),
      [=](TaskContext &ctx) -> int {
        Ledger ledger(statePath);
        const int added =
            fdsnPlan(ledger, station, days, network, channels, service);
        int back = fdsnReclaim(ledger);
        if (retryFailed)
          back += fdsnRetryFailed(ledger, station);
        const int adopted = fdsnAdopt(ledger, outDir);
        const int pending = ledger.inState({QStringLiteral("pending")}).size();
        ctx.log(tr("%1: %2 day(s) added, %3 already on disk, %4 back to "
                   "pending; %5 to fetch "
                   "from %6 into %7")
                    .arg(statePath)
                    .arg(added)
                    .arg(adopted)
                    .arg(back)
                    .arg(pending)
                    .arg(service, outDir));
        auto refresh = [self] {
          QMetaObject::invokeMethod(
              self.data(),
              [self] {
                if (self)
                  self->reload();
              },
              Qt::QueuedConnection);
        };
        refresh();
        if (planOnly)
          return 0;
        auto http = makeQtHttp();
        const auto counts = fdsnWork(
            ledger, outDir, *http, opt,
            [&](const QString &m) {
              ctx.log(m);
              refresh();
            },
            [&] { return ctx.cancelled(); },
            [&](int done, int total) {
              if (total)
                ctx.progress(double(done) / total,
                             QStringLiteral("%1/%2").arg(done).arg(total));
            });
        QStringList bits;
        for (auto it = counts.begin(); it != counts.end(); ++it)
          bits << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
        ctx.log(tr("done: %1").arg(bits.join(", ")));
        refresh();
        return 0;
      });
  emit fetchStarted(outDir);
  running_ = jobs_->add(job);
  connect(running_, &Job::stateChanged, this, [this] {
    if (running_ && running_->state() != Job::State::Running) {
      running_ = nullptr;
      reload();
      updateButtons();
    }
  });
  updateButtons();
}

void FdsnPane::updateButtons() {
  const QString why = blockedReason();
  why_->setText(why);
  why_->setVisible(!why.isEmpty());
  go_->setEnabled(why.isEmpty() && !running_);
  stop_->setEnabled(running_ != nullptr);
}

void FdsnPane::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QWidget::changeEvent(e);
}

void FdsnPane::retranslate() {
  planOnly_->setText(tr("only add the days, do not fetch"));
  retryFailed_->setText(tr("put this station's failed days back first"));
  go_->setText(tr("Fetch"));
  ui::setPrimary(go_);
  stop_->setText(tr("Stop"));

  ui::setHelp(station_, tr("The station code at the FDSN service, e.g. "
                           "SVRC. One station per pull."));
  ui::setHelp(network_, tr("The FDSN network code of the station. KO is "
                           "KOERI's network."));
  ui::setHelp(channels_,
              tr("Which channels to fetch, as an FDSN pattern; ? stands for "
                 "any one letter. BH? is the three broadband components "
                 "(BHE, BHN, BHZ)."));
  ui::setHelp(service_, tr("Address of the FDSN dataselect service. The "
                           "default is KOERI's."));
  ui::setHelp(start_, tr("The first UTC day to fetch. Whole days are "
                         "fetched, one file per day."));
  ui::setHelp(end_, tr("The day after the last one to fetch: this day "
                       "itself is not fetched."));
  ui::setHelp(retries_,
              tr("How many times a day is tried before it is marked "
                 "failed, with a growing pause between tries. Default 5."));
  ui::setHelp(planOnly_, tr("Adds the days to the FDSN ledger as pending "
                            "without downloading anything now."));
  ui::setHelp(retryFailed_,
              tr("Before fetching, puts the days of this station that "
                 "failed earlier back to pending so they are tried "
                 "again."));
  go_->setToolTip(ui::helpText(
      tr("Adds the days to the campaign's FDSN ledger and downloads the "
         "pending ones, oldest first, as a job.")));
  stop_->setToolTip(
      ui::helpText(tr("Stops the pull; the days not yet fetched stay "
                      "pending.")));
}

} // namespace skg
