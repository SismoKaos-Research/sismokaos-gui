#include "modules/study/studysetup.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QTimeZone>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <exception>
#include <memory>

#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/store.h"

namespace skg {

using study::Study;

std::function<QStringList(QWidget *, const QString &, const QString &)>
    pickFoldersHook;

QStringList pickFolders(QWidget *parent, const QString &title,
                        const QString &startDir) {
  if (pickFoldersHook)
    return pickFoldersHook(parent, title, startDir);
  QFileDialog dialog(parent, title, startDir);
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setOption(QFileDialog::DontUseNativeDialog, true);
  // Qt's own dialog picks one folder unless its two views are told
  // otherwise.
  for (QAbstractItemView *view : dialog.findChildren<QAbstractItemView *>())
    if (qobject_cast<QListView *>(view) || qobject_cast<QTreeView *>(view))
      view->setSelectionMode(QAbstractItemView::ExtendedSelection);
  if (dialog.exec() != QDialog::Accepted)
    return {};
  QStringList out;
  for (const QString &p : dialog.selectedFiles())
    if (QFileInfo(p).isDir())
      out << QDir::cleanPath(p);
  // With several folders selected the dialog also reports the folder they
  // are in; that one was not what the user picked.
  if (out.size() > 1) {
    const QString shown = QDir::cleanPath(dialog.directory().absolutePath());
    out.removeAll(shown);
  }
  return out;
}

QStringList foldersOf(const QMimeData *mime) {
  QStringList out;
  if (!mime || !mime->hasUrls())
    return out;
  for (const QUrl &u : mime->urls()) {
    if (!u.isLocalFile())
      continue;
    const QFileInfo fi(u.toLocalFile());
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    if (QFileInfo(dir).isDir() && !out.contains(QDir::cleanPath(dir)))
      out << QDir::cleanPath(dir);
  }
  return out;
}

// ---- FolderList ----

FolderList::FolderList(const QString &startDir, QWidget *parent)
    : QWidget(parent), startDir_(startDir) {
  setAcceptDrops(true);
  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("setupFolders"));
  list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  list_->setMinimumHeight(96);
  ui::setEmptyHint(list_, tr("No waveform folders.\nAdd them, or drop "
                             "folders here from the file manager."));
  add_ = new QPushButton(tr("Add folders…"), this);
  add_->setObjectName(QStringLiteral("setupAddFolders"));
  add_->setAutoDefault(false);
  remove_ = new QPushButton(tr("Remove"), this);
  remove_->setAutoDefault(false);
  note_ = new QLabel(this);
  note_->setObjectName(QStringLiteral("fieldLabel"));
  note_->setWordWrap(true);
  note_->hide();
  auto *buttons = new QVBoxLayout;
  buttons->addWidget(add_);
  buttons->addWidget(remove_);
  buttons->addStretch(1);
  auto *row = new QHBoxLayout;
  row->addWidget(list_, 1);
  row->addLayout(buttons);
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addLayout(row);
  layout->addWidget(note_);

  list_->setToolTip(ui::helpText(
      tr("The folders holding the waveforms you already have. A folder is "
         "read through all its sub-folders, so a folder of station folders "
         "(stations/CODE/…) is added once, as a whole. Folders can also be "
         "dropped here from the file manager.")));
  add_->setToolTip(ui::helpText(
      tr("Picks one or several folders. Hold Ctrl or Shift to pick more "
         "than one. Pick the folder that holds the station folders, not "
         "each station folder.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected folders off the list. Nothing on disk is "
         "deleted.")));

  connect(add_, &QPushButton::clicked, this, [this] {
    const QStringList picked =
        pickFolders(this, tr("Folders of waveforms"),
                    settings_ ? ui::dialogFolder(settings_, key_, startDir_)
                              : startDir_);
    if (!picked.isEmpty() && settings_)
      ui::rememberDialogFolder(settings_, key_,
                               QFileInfo(picked.first()).absolutePath());
    addFolders(picked);
  });
  connect(remove_, &QPushButton::clicked, this, [this] {
    qDeleteAll(list_->selectedItems());
    note_->hide();
    emit changed();
  });
}

QStringList FolderList::folders() const {
  QStringList out;
  for (int i = 0; i < list_->count(); ++i)
    out << list_->item(i)->text();
  return out;
}

QString FolderList::note() const { return note_->text(); }

void FolderList::addFolders(const QStringList &paths) {
  if (paths.isEmpty())
    return;
  // The study's own rule decides what a second folder may add.
  Study scratch;
  scratch.dir = startDir_;
  for (const QString &f : folders())
    scratch.addWaveRoot(f);
  const Study::RootsAdded r = scratch.addWaveRoots(paths);
  list_->clear();
  for (const QString &p : scratch.waveformRoots())
    list_->addItem(QDir::toNativeSeparators(p));
  QStringList notes;
  if (!r.covered.isEmpty())
    notes << tr("Already covered by a listed folder: %1")
                 .arg(r.covered.join(QStringLiteral(", ")));
  if (!r.replaced.isEmpty())
    notes << tr("Now covered by a folder that holds it, so taken off the "
                "list: %1")
                 .arg(r.replaced.join(QStringLiteral(", ")));
  note_->setText(notes.join(QStringLiteral("\n")));
  note_->setVisible(!notes.isEmpty());
  emit changed();
}

void FolderList::dragEnterEvent(QDragEnterEvent *e) {
  if (!foldersOf(e->mimeData()).isEmpty())
    e->acceptProposedAction();
}

void FolderList::dropEvent(QDropEvent *e) {
  const QStringList dropped = foldersOf(e->mimeData());
  if (dropped.isEmpty())
    return;
  addFolders(dropped);
  e->acceptProposedAction();
}

// ---- StudySetupDialog ----

StudySetupDialog::StudySetupDialog(study::StudyStore *store, QWidget *parent)
    : QDialog(parent), store_(store) {
  const Study &s = store_->study();
  setWindowTitle(tr("Set up the study"));
  setAcceptDrops(true);

  auto *intro = new QLabel(
      tr("Both parts can be left as they are and changed later on the Cases "
         "page."),
      this);
  intro->setObjectName(QStringLiteral("fieldLabel"));
  intro->setWordWrap(true);

  auto *catBox = new QGroupBox(tr("Earthquake catalog"), this);
  builtIn_ = new QRadioButton(tr("Start with the built-in list"), catBox);
  builtIn_->setObjectName(QStringLiteral("setupBuiltIn"));
  own_ = new QRadioButton(tr("Use my own file"), catBox);
  own_->setObjectName(QStringLiteral("setupOwn"));
  const study::BuiltInCatalog b = study::builtInCatalog();
  builtInNote_ = new QLabel(
      tr("%1 earthquakes of magnitude %2 and above since %3, updated %4. "
         "Enough to pick the earthquakes of the study. The Indicators and "
         "the Features calibration count small earthquakes and need a full "
         "catalog, which can be chosen or downloaded later.")
          .arg(b.count)
          .arg(b.minMagnitude, 0, 'f', 1)
          .arg(b.first.year())
          .arg(b.updated.toString(Qt::ISODate)),
      catBox);
  builtInNote_->setObjectName(QStringLiteral("fieldLabel"));
  builtInNote_->setWordWrap(true);
  catalog_ = new QLineEdit(catBox);
  catalog_->setObjectName(QStringLiteral("setupCatalog"));
  catalog_->setReadOnly(true);
  catalog_->setPlaceholderText(tr("AFAD catalog export, CSV or .gz"));
  browse_ = new QPushButton(QStringLiteral("…"), catBox);
  browse_->setAutoDefault(false);
  ui::setCompact(browse_);
  time_ = new QComboBox(catBox);
  time_->setObjectName(QStringLiteral("setupCatalogTime"));
  time_->addItem(tr("times in UTC"), 0.0);
  time_->addItem(tr("times in Turkey time (+3)"), 3.0);
  auto *ownRow = new QHBoxLayout;
  ownRow->addWidget(catalog_, 1);
  ownRow->addWidget(browse_);
  ownRow->addWidget(time_);
  auto *catLayout = new QVBoxLayout(catBox);
  catLayout->addWidget(builtIn_);
  catLayout->addWidget(builtInNote_);
  catLayout->addWidget(own_);
  catLayout->addLayout(ownRow);

  auto *waveBox = new QGroupBox(tr("Waveform folders"), this);
  later_ = new QLabel(
      tr("Folders of miniSEED you already have, plain files or zip "
         "archives, in any layout. Waveforms downloaded with the Data page "
         "are added by themselves."),
      waveBox);
  later_->setObjectName(QStringLiteral("fieldLabel"));
  later_->setWordWrap(true);
  folders_ = new FolderList(s.dir, waveBox);
  folders_->rememberIn(store->settings(), QStringLiteral("cases/waveformFolders"));
  auto *waveLayout = new QVBoxLayout(waveBox);
  waveLayout->addWidget(later_);
  waveLayout->addWidget(folders_);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Done"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Not now"));
  ui::setPrimary(buttons->button(QDialogButtonBox::Ok));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro);
  layout->addWidget(catBox);
  layout->addWidget(waveBox, 1);
  layout->addWidget(buttons);
  resize(720, 560);
  ui::polishPage(this);

  ui::setHelp(builtIn_,
              tr("The list of large earthquakes that comes with the "
                 "application, so cases can be added at once."));
  ui::setHelp(own_, tr("A catalog file of your own, for example an AFAD "
                       "export: used for picking the cases and by the "
                       "analyses."));
  ui::setHelp({catalog_, browse_},
              tr("The catalog file: an AFAD export as CSV, plain or .gz."));
  ui::setHelp(time_, tr("Whether the times in the catalog file are UTC or "
                        "Turkey time (UTC+3). A wrong choice shifts every "
                        "event by three hours."));

  // What the study already has.
  if (s.catalog.isEmpty()) {
    builtIn_->setChecked(true);
  } else {
    own_->setChecked(true);
    catalog_->setText(QDir::toNativeSeparators(s.catalogPath()));
  }
  time_->setCurrentIndex(std::max(0, time_->findData(s.catalogUtcOffsetHours)));
  folders_->addFolders(s.waveformRoots());

  connect(builtIn_, &QRadioButton::toggled, this,
          &StudySetupDialog::updateState);
  connect(browse_, &QPushButton::clicked, this,
          &StudySetupDialog::chooseCatalog);
  updateState();
}

void StudySetupDialog::chooseCatalog() {
  const QString path = ui::pickFile(
      this, store_->settings(), QStringLiteral("cases/catalog"),
      tr("Earthquake catalog"), tr("AFAD catalog (*.csv *.gz);;All files (*)"),
      store_->study().dir);
  if (path.isEmpty())
    return;
  catalog_->setText(QDir::toNativeSeparators(path));
  own_->setChecked(true);
  updateState();
}

void StudySetupDialog::updateState() {
  const bool own = own_->isChecked();
  catalog_->setEnabled(own);
  time_->setEnabled(own);
  builtInNote_->setEnabled(!own);
}

void StudySetupDialog::accept() {
  const bool own = own_->isChecked() && !catalog_->text().trimmed().isEmpty();
  const QString catalog = QDir::fromNativeSeparators(catalog_->text().trimmed());
  const double hours = time_->currentData().toDouble();
  QStringList wanted;
  for (const QString &f : folders_->folders())
    wanted << QDir::fromNativeSeparators(f);
  QString error;
  const bool ok = store_->update(
      [&](Study &s) {
        if (own) {
          s.catalog = s.stored(catalog);
          s.catalogUtcOffsetHours = hours;
        } else {
          s.catalog.clear();
          s.catalogUtcOffsetHours = 0.0;
        }
        // The list is what the user left: folders taken off it go.
        for (int i = int(s.waveRoots.size()) - 1; i >= 0; --i)
          if (!wanted.contains(
                  QDir::cleanPath(s.resolved(s.waveRoots[i].path))))
            s.waveRoots.removeAt(i);
        s.addWaveRoots(wanted);
      },
      &error);
  if (!ok) {
    later_->setText(tr("⚠ The study could not be saved: %1").arg(error));
    return;
  }
  QDialog::accept();
}

// ---- CatalogFetchDialog ----

CatalogFetchDialog::CatalogFetchDialog(const Study &study, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("Get the catalog for this study"));
  const study::CatalogQuery q = study::queryForStudy(study);

  note_ = new QLabel(
      tr("Downloads the earthquakes around the cases of this study from an "
         "FDSN event service into the study folder, with a record of the "
         "request, and makes the file the study's catalog.\n\n"
         "How far down in magnitude a service goes differs. Outside the "
         "United States USGS lists mostly the larger earthquakes (roughly "
         "magnitude 4 and above), which is too few events for b-values; "
         "what the download holds is reported when it ends. AFAD's own "
         "catalog goes much lower, but its public web service only returns "
         "the latest 1000 earthquakes: export the period from AFAD's "
         "website (deprem.afad.gov.tr) and choose the file with “Use my own "
         "file…” instead."),
      this);
  note_->setObjectName(QStringLiteral("fieldLabel"));
  note_->setWordWrap(true);

  service_ = new QComboBox(this);
  service_->setObjectName(QStringLiteral("fetchService"));
  for (const study::EventService &s : study::eventServices())
    service_->addItem(s.name, s.id);
  service_->addItem(tr("Another FDSN event service…"), QString());
  url_ = new QLineEdit(this);
  url_->setObjectName(QStringLiteral("fetchUrl"));
  url_->setPlaceholderText(
      QStringLiteral("https://…/fdsnws/event/1/query"));
  const QDate today = QDate::currentDate();
  from_ = new QDateEdit(q.valid() ? q.from.date() : today.addYears(-2), this);
  from_->setObjectName(QStringLiteral("fetchFrom"));
  to_ = new QDateEdit(q.valid() ? q.to.date() : today, this);
  to_->setObjectName(QStringLiteral("fetchTo"));
  for (QDateEdit *d : {from_, to_}) {
    d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    d->setCalendarPopup(true);
  }
  auto box = [this](double lo, double hi, double v, const QString &name) {
    auto *s = new QDoubleSpinBox(this);
    s->setObjectName(name);
    s->setRange(lo, hi);
    s->setDecimals(1);
    s->setSingleStep(0.5);
    s->setValue(v);
    s->setSuffix(QStringLiteral("°"));
    return s;
  };
  minLat_ = box(-90, 90, q.valid() ? q.minLat : 34.5, QStringLiteral("fetchMinLat"));
  maxLat_ = box(-90, 90, q.valid() ? q.maxLat : 43.0, QStringLiteral("fetchMaxLat"));
  minLon_ = box(-180, 180, q.valid() ? q.minLon : 25.0, QStringLiteral("fetchMinLon"));
  maxLon_ = box(-180, 180, q.valid() ? q.maxLon : 45.5, QStringLiteral("fetchMaxLon"));
  minMag_ = new QDoubleSpinBox(this);
  minMag_->setObjectName(QStringLiteral("fetchMinMag"));
  minMag_->setRange(0.0, 9.0);
  minMag_->setDecimals(1);
  minMag_->setSingleStep(0.5);
  minMag_->setValue(1.0);
  minMag_->setPrefix(QStringLiteral("M ≥ "));

  auto *period = new QHBoxLayout;
  period->addWidget(from_);
  period->addWidget(new QLabel(QStringLiteral("–"), this));
  period->addWidget(to_);
  period->addStretch(1);
  auto *lat = new QHBoxLayout;
  lat->addWidget(minLat_);
  lat->addWidget(new QLabel(QStringLiteral("–"), this));
  lat->addWidget(maxLat_);
  lat->addStretch(1);
  auto *lon = new QHBoxLayout;
  lon->addWidget(minLon_);
  lon->addWidget(new QLabel(QStringLiteral("–"), this));
  lon->addWidget(maxLon_);
  lon->addStretch(1);
  auto *form = new QFormLayout;
  form->addRow(tr("Service"), service_);
  form->addRow(tr("Address"), url_);
  form->addRow(tr("Period (UTC)"), period);
  form->addRow(tr("Latitude"), lat);
  form->addRow(tr("Longitude"), lon);
  form->addRow(tr("Smallest magnitude"), minMag_);

  problem_ = new QLabel(this);
  problem_->setObjectName(QStringLiteral("caseWarning"));
  problem_->setWordWrap(true);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  ok_->setText(tr("Download"));
  ui::setPrimary(ok_);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(note_);
  layout->addLayout(form);
  layout->addWidget(problem_);
  layout->addWidget(buttons);
  resize(640, 520);
  ui::polishPage(this);

  ui::setHelp(service_,
              tr("Where the earthquakes are asked from. USGS needs no "
                 "account and its data are in the public domain. Another "
                 "service can be given by its address; check its terms of "
                 "use first."));
  ui::setHelp(url_, tr("The query address of an FDSN event service "
                       "(…/fdsnws/event/1/query). It must answer in the FDSN "
                       "text format without an account."));
  ui::setHelp({from_, to_},
              tr("The period to download, in UTC. Proposed from the cases: "
                 "the earliest span start, minus the longest Indicators "
                 "window, to the last earthquake."));
  ui::setHelp({minLat_, maxLat_},
              tr("South and north edge of the area. Proposed from the "
                 "cases' zones with a margin."));
  ui::setHelp({minLon_, maxLon_},
              tr("West and east edge of the area. Proposed from the cases' "
                 "zones with a margin."));
  ui::setHelp(minMag_,
              tr("Earthquakes below this magnitude are not downloaded. A "
                 "low value gives the analyses more events; the service may "
                 "not have them. Default 1.0."));

  connect(service_, &QComboBox::currentIndexChanged, this,
          &CatalogFetchDialog::updateState);
  connect(url_, &QLineEdit::textChanged, this,
          &CatalogFetchDialog::updateState);
  for (QDateEdit *d : {from_, to_})
    connect(d, &QDateEdit::dateChanged, this,
            &CatalogFetchDialog::updateState);
  for (QDoubleSpinBox *s : {minLat_, maxLat_, minLon_, maxLon_})
    connect(s, &QDoubleSpinBox::valueChanged, this,
            &CatalogFetchDialog::updateState);
  updateState();
}

QString CatalogFetchDialog::serviceId() const {
  const QString id = service_->currentData().toString();
  return id.isEmpty() ? QStringLiteral("fdsn") : id;
}

study::CatalogQuery CatalogFetchDialog::query() const {
  study::CatalogQuery q;
  const QString id = service_->currentData().toString();
  if (id.isEmpty()) {
    q.serviceUrl = url_->text().trimmed();
  } else {
    for (const study::EventService &s : study::eventServices())
      if (s.id == id)
        q.serviceUrl = s.url;
  }
  q.from = QDateTime(from_->date(), QTime(0, 0), QTimeZone::UTC);
  q.to = QDateTime(to_->date(), QTime(0, 0), QTimeZone::UTC);
  q.minLat = minLat_->value();
  q.maxLat = maxLat_->value();
  q.minLon = minLon_->value();
  q.maxLon = maxLon_->value();
  q.minMagnitude = minMag_->value();
  return q;
}

void CatalogFetchDialog::updateState() {
  const bool custom = service_->currentData().toString().isEmpty();
  url_->setEnabled(custom);
  const study::CatalogQuery q = query();
  QString why;
  if (custom && !QUrl(q.serviceUrl).isValid())
    why = tr("Give the address of the service.");
  else if (custom && !q.serviceUrl.startsWith(QLatin1String("http")))
    why = tr("Give the address of the service, starting with https://.");
  else if (q.from >= q.to)
    why = tr("The period ends before it starts.");
  else if (q.minLat >= q.maxLat || q.minLon >= q.maxLon)
    why = tr("The area is empty: the first value must be the smaller one.");
  problem_->setText(why.isEmpty() ? QString() : QStringLiteral("⚠ ") + why);
  ok_->setEnabled(why.isEmpty());
}

// ---- running a download ----

Job *startCatalogFetch(AppContext ctx, const study::CatalogQuery &query,
                       const QString &serviceId,
                       const std::function<void(bool, const QString &)> &done,
                       HttpFactory http) {
  if (!ctx.study || !ctx.study->isOpen() || !ctx.jobs)
    return nullptr;
  const QString outDir =
      QDir(ctx.study->study().dir).filePath(QStringLiteral("catalog"));
  auto result = std::make_shared<study::FetchResult>();
  auto failure = std::make_shared<QString>();
  auto *job = new TaskJob(
      StudySetupDialog::tr("Earthquake catalog download"),
      QStringLiteral("catalog"),
      StudySetupDialog::tr("%1, %2 to %3, M ≥ %4")
          .arg(query.serviceUrl,
               query.from.date().toString(Qt::ISODate),
               query.to.date().toString(Qt::ISODate))
          .arg(query.minMagnitude, 0, 'f', 1),
      [query, serviceId, outDir, result, failure, http](TaskContext &t) -> int {
        try {
          std::unique_ptr<tdvms::Http> client =
              http ? http() : tdvms::makeQtHttp();
          study::FetchHooks hooks;
          hooks.log = [&t](const QString &m) { t.log(m); };
          hooks.progress = [&t](double f, const QString &what) {
            t.progress(f, what);
          };
          hooks.shouldStop = [&t] { return t.cancelled(); };
          *result =
              study::fetchCatalog(*client, query, outDir, serviceId, hooks);
          return result->stopped ? 1 : 0;
        } catch (const std::exception &e) {
          *failure = QString::fromUtf8(e.what());
          t.log(*failure);
          return 1;
        }
      });
  Job *added = ctx.jobs->add(job);
  study::StudyStore *store = ctx.study;
  const QString studyDir = store->study().dir;
  QObject::connect(added, &Job::stateChanged, store,
                   [added, store, studyDir, result, failure, done] {
    const Job::State st = added->state();
    if (st == Job::State::Running || st == Job::State::Queued)
      return;
    if (st != Job::State::Done) {
      if (done)
        done(false,
             st == Job::State::Cancelled || result->stopped
                 ? StudySetupDialog::tr("The catalog download was stopped; "
                                        "nothing was written.")
                 : StudySetupDialog::tr("The catalog download failed: %1")
                       .arg(*failure));
      return;
    }
    // The study the download was for may have been closed meanwhile; the
    // file is in its folder either way.
    QString error;
    if (!store->isOpen() || store->study().dir != studyDir) {
      if (done)
        done(true, StudySetupDialog::tr("The catalog was written to %1.")
                       .arg(result->csvPath));
      return;
    }
    const QString path = result->csvPath;
    if (!store->update(
            [&path](Study &s) {
              s.catalog = s.stored(path);
              s.catalogUtcOffsetHours = 0.0;
            },
            &error)) {
      if (done)
        done(false, error);
      return;
    }
    if (done)
      done(true,
           StudySetupDialog::tr(
               "The catalog now has %1 earthquakes, most of them of "
               "magnitude %2 and above (the smallest is %3). Check that "
               "this goes low enough for the Indicators before running "
               "them.")
               .arg(result->events)
               .arg(result->lowMagnitude, 0, 'f', 1)
               .arg(result->minMagnitudeFound, 0, 'f', 1));
  });
  return added;
}

} // namespace skg
