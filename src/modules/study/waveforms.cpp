#include "modules/study/waveforms.h"
#include "modules/study/coveragedialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "core/ui.h"
#include "modules/study/studysetup.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/scancache.h"
#include "study/store.h"

namespace skg {

using study::ScanPass;
using study::Study;
using study::WaveIndex;

// ---- the scan ----

struct WaveScan::Live {
  QMutex mutex;
  QString dir; // the study the ids are of
  QStringList ids;
};

namespace {

// An index a scan could not save at its end (the file was held open by
// something else for longer than the retries last): kept here so the next
// scan of that study starts from it and saves it, instead of reading it all
// again. Per study folder; any thread.
QMutex &unsavedMutex() {
  static QMutex m;
  return m;
}
QMap<QString, WaveIndex> &unsavedIndexes() {
  static QMap<QString, WaveIndex> map;
  return map;
}

} // namespace

WaveScan::WaveScan(AppContext ctx, QObject *parent)
    : QObject(parent), ctx_(ctx), live_(std::make_shared<Live>()) {
  if (ctx_.study)
    connect(ctx_.study, &study::StudyStore::changed, this,
            &WaveScan::studyChanged);
}

QString WaveScan::rootsKey() const {
  QString key;
  if (!ctx_.study || !ctx_.study->isOpen())
    return key;
  const Study &s = ctx_.study->study();
  key = s.dir + QLatin1Char('\n');
  for (const study::WaveRoot &r : s.waveRoots)
    key += r.id + QLatin1Char('=') + s.resolved(r.path) + QLatin1Char('\n');
  return key;
}

// The study changed while a scan may be running. The job is told which
// folders are still listed (it drops the others before it saves); and when
// the list itself changed, the scan is stopped and started again on the new
// one, since it works on the list it was started with.
void WaveScan::studyChanged() {
  QStringList ids;
  QString dir;
  if (ctx_.study && ctx_.study->isOpen()) {
    dir = ctx_.study->study().dir;
    for (const study::WaveRoot &r : ctx_.study->study().waveRoots)
      ids << r.id;
  }
  {
    const QMutexLocker lock(&live_->mutex);
    live_->dir = dir;
    live_->ids = ids;
  }
  if (running() && !halting_ && rootsKey() != scannedRoots_) {
    again_ = true;
    job_->cancel();
  }
}

void WaveScan::start(bool forgetEmpty) {
  if (running() || !ctx_.study || !ctx_.study->isOpen() || !ctx_.jobs)
    return;
  if (ctx_.study->study().waveRoots.isEmpty()) {
    status_.clear();
    detail_.clear();
    emit updated();
    return;
  }
  stoppedByUser_ = false;
  again_ = false;
  forgetEmpty_ = forgetEmpty;
  run(ScanPass::Identity);
}

void WaveScan::cancel() {
  if (job_) {
    stoppedByUser_ = true;
    again_ = false;
    job_->cancel();
  }
}

bool WaveScan::stopAndWait(int timeoutMs) {
  if (!running())
    return true;
  halting_ = true;
  again_ = false;
  job_->cancel();
  QEventLoop loop;
  const QMetaObject::Connection c =
      connect(this, &WaveScan::updated, &loop, [this, &loop] {
        if (!running())
          loop.quit();
      });
  QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
  if (running())
    loop.exec();
  disconnect(c);
  halting_ = false;
  return !running();
}

QStringList WaveScan::unavailable() const {
  if (!ctx_.study || !ctx_.study->isOpen())
    return {};
  const Study &s = ctx_.study->study();
  return study::unavailableWaveRoots(s, *WaveIndex::shared(s.dir));
}

int WaveScan::workers(const QSettings *settings) {
  const int set =
      settings
          ? settings->value(QLatin1String(study::kScanWorkersSetting), 0).toInt()
          : 0;
  return set > 0 ? std::min(set, 64) : study::defaultScanWorkers();
}

namespace {

QString sizeText(qint64 bytes) {
  const double gb = double(bytes) / 1073741824.0;
  return gb >= 1.0 ? WaveScan::tr("%1 GB").arg(gb, 0, 'f', gb >= 100 ? 0 : 1)
                   : WaveScan::tr("%1 MB").arg(double(bytes) / 1048576.0, 0,
                                                'f', 0);
}

QString durationText(double seconds) {
  if (seconds < 90)
    return WaveScan::tr("%1 s").arg(std::max(1, int(std::lround(seconds))));
  const int minutes = int(std::lround(seconds / 60.0));
  return minutes < 90 ? WaveScan::tr("%1 min").arg(minutes)
                      : WaveScan::tr("%1 h %2 min")
                            .arg(minutes / 60)
                            .arg(minutes % 60);
}

} // namespace

void WaveScan::run(ScanPass pass) {
  const Study study = ctx_.study->study();
  const bool identity = pass == ScanPass::Identity;
  const int workers = WaveScan::workers(ctx_.settings);
  const QString cacheDir = study::ScanCache::sharedDir();
  scannedRoots_ = rootsKey();
  {
    const QMutexLocker lock(&live_->mutex);
    live_->dir = study.dir;
    live_->ids.clear();
    for (const study::WaveRoot &r : study.waveRoots)
      live_->ids << r.id;
  }
  const std::shared_ptr<Live> live = live_;
  const bool forgetEmpty = forgetEmpty_;
  // The stations cases are waiting for are read first.
  QStringList first;
  for (const study::Case &c : study.cases)
    for (const study::StationRef &st : c.stations)
      if (!first.contains(st.code))
        first << st.code;
  auto *job = new TaskJob(
      identity ? tr("Waveforms: which stations") : tr("Waveforms: coverage"),
      QStringLiteral("waveforms"),
      identity ? tr("the names and the first record of every file in the "
                    "waveform folders")
               : tr("every record header in the waveform folders, %n file(s) "
                    "at a time",
                    nullptr, workers),
      [study, pass, workers, cacheDir, first, live,
       forgetEmpty](TaskContext &t) -> int {
        const bool identity = pass == ScanPass::Identity;
        // What an earlier scan read but could not save comes first.
        WaveIndex index;
        bool carried = false;
        {
          const QMutexLocker lock(&unsavedMutex());
          const auto it = unsavedIndexes().constFind(study.dir);
          if (it != unsavedIndexes().constEnd()) {
            index = *it;
            carried = true;
          }
        }
        if (!carried)
          index = WaveIndex::load(study.dir);
        // Folders the study no longer lists leave the index: at the start,
        // and before every save, since the list can change while this runs
        // (the scan is then stopped and started again, but what it saves on
        // the way out must not bring a removed folder back).
        const auto dropUnlisted = [&index, &live, &study] {
          QStringList listed;
          bool sameStudy = false;
          {
            const QMutexLocker lock(&live->mutex);
            // Another study may have been opened meanwhile: its list says
            // nothing about this one's folders.
            sameStudy = live->dir == study.dir;
            listed = live->ids;
          }
          for (auto it = index.roots.begin(); it != index.roots.end();)
            it = study.waveRootPath(it.key()).isEmpty() ||
                         (sameStudy && !listed.contains(it.key()))
                     ? index.roots.erase(it)
                     : std::next(it);
        };
        dropUnlisted();
        QList<study::ScanFolder> folders;
        for (const study::WaveRoot &root : study.waveRoots) {
          folders << study::ScanFolder{root.id, study.resolved(root.path)};
          t.log(QStringLiteral("%1: %2").arg(root.id, folders.last().path));
        }
        study::ScanCache cache(cacheDir);
        study::ScanOptions options;
        options.workers = workers;
        options.firstStations = first;
        options.cache = cacheDir.isEmpty() ? nullptr : &cache;
        options.forgetEmptyFolders = forgetEmpty;
        QElapsedTimer clock;
        clock.start();
        // The speed over the last few seconds, for the time left.
        QList<QPair<qint64, qint64>> samples; // ms, bytes
        study::ScanHooks hooks;
        hooks.shouldStop = [&t] { return t.cancelled(); };
        // A save on the way that fails (the file is open elsewhere for a
        // moment) is said once and made up for by the next one: the index
        // in memory has everything.
        bool saveWarned = false;
        hooks.checkpoint = [&] {
          dropUnlisted();
          QString why;
          if (!index.save(study.dir, &why, 2) && !saveWarned) {
            saveWarned = true;
            t.log(QStringLiteral("  note: the index could not be saved just "
                                 "now (%1); it is saved again as the scan "
                                 "goes on")
                      .arg(why));
          }
        };
        if (identity) {
          hooks.progress = [&t](int done, int total, const QString &) {
            t.progress(total > 0 ? double(done) / total : 0.0,
                       WaveScan::tr("%1 of %2 files").arg(done).arg(total));
          };
        } else {
          hooks.bytes = [&](qint64 done, qint64 total, int filesDone,
                            int filesTotal) {
            const qint64 now = clock.elapsed();
            samples.append({now, done});
            while (samples.size() > 2 && now - samples.first().first > 8000)
              samples.removeFirst();
            const double seconds =
                double(now - samples.first().first) / 1000.0;
            const double rate =
                seconds > 0.5
                    ? double(done - samples.first().second) / seconds
                    : 0.0;
            QString text = WaveScan::tr("%1 of %2")
                               .arg(sizeText(done), sizeText(total));
            if (rate > 0) {
              text += QStringLiteral(" · ") +
                      WaveScan::tr("%1/s").arg(sizeText(qint64(rate)));
              if (total > done)
                text += QStringLiteral(" · ") +
                        WaveScan::tr("about %1 left")
                            .arg(durationText(double(total - done) / rate));
            }
            text += QStringLiteral(" · ") + WaveScan::tr("%1 of %2 files")
                                                .arg(filesDone)
                                                .arg(filesTotal);
            t.progress(total > 0 ? double(done) / double(total) : 0.0, text);
          };
        }
        const study::ScanStats s =
            study::scanRoots(index, folders, pass, hooks, options);
        const double seconds = double(clock.elapsed()) / 1000.0;
        t.log(QStringLiteral("  %1 files, %2 read now, %3 from the scan "
                             "cache, %4 not miniSEED, %5 gone")
                  .arg(s.listed)
                  .arg(s.read)
                  .arg(s.cached)
                  .arg(s.skipped)
                  .arg(s.dropped));
        if (s.unfiled > 0)
          t.log(QStringLiteral("  note: %1 result(s) could not be written to "
                               "the scan cache in %2; the scan itself is "
                               "complete")
                    .arg(s.unfiled)
                    .arg(cacheDir));
        if (!identity && s.read > 0 && seconds > 0)
          t.log(QStringLiteral("  %1 MB in %2 s with %3 worker(s): %4 MB/s")
                    .arg(double(s.bytes) / 1048576.0, 0, 'f', 0)
                    .arg(seconds, 0, 'f', 1)
                    .arg(workers)
                    .arg(double(s.bytes) / 1048576.0 / seconds, 0, 'f', 0));
        for (const QString &path : s.missing)
          t.log(QStringLiteral("  error: the folder %1 does not exist")
                    .arg(path));
        for (const QString &path : s.unavailable)
          t.log(QStringLiteral("  note: %1 is there but lists nothing now (a "
                               "drive that is not connected looks like "
                               "this); what the study knew of it is kept")
                    .arg(path));
        dropUnlisted();
        QString error;
        if (!index.save(study.dir, &error, 12)) {
          // Not lost: the next scan starts from what this one read.
          {
            const QMutexLocker lock(&unsavedMutex());
            unsavedIndexes().insert(study.dir, index);
          }
          t.log(QStringLiteral("error: the index could not be saved (%1). "
                               "What was read is kept in memory and saved "
                               "by the next scan.")
                    .arg(error));
          return 1;
        }
        {
          const QMutexLocker lock(&unsavedMutex());
          unsavedIndexes().remove(study.dir);
        }
        return !s.missing.isEmpty() || s.stopped ? 1 : 0;
      });
  Job *added = ctx_.jobs->add(job);
  job_ = added;
  status_ = identity ? tr("Looking for stations…") : tr("Reading coverage…");
  detail_ = status_;
  emit updated();
  connect(added, &Job::progressChanged, this, [this, added, identity] {
    const ProgressState p = added->progress();
    // The short form names how far it is; the long one is the job's own line.
    const QString percent =
        tr("%1%").arg(int(std::floor(std::clamp(p.fraction, 0.0, 1.0) * 100)));
    const QString left = p.text.section(QStringLiteral(" · "), 2, 2);
    status_ = identity ? tr("Looking for stations: %1").arg(p.text)
                       : left.isEmpty()
                             ? tr("Reading coverage: %1").arg(percent)
                             : tr("Reading coverage: %1, %2").arg(percent, left);
    detail_ = (identity ? tr("Looking for stations: %1")
                        : tr("Reading coverage: %1"))
                  .arg(p.text);
    // The progress arrives several times a second; the pages that follow
    // the scan redraw their tables, so they are told once a second.
    if (!sinceUpdate_.isValid() || sinceUpdate_.elapsed() >= 1000) {
      sinceUpdate_.start();
      emit updated();
    }
  });
  connect(added, &Job::stateChanged, this, [this, added, pass] {
    const Job::State st = added->state();
    if (st == Job::State::Running || st == Job::State::Queued)
      return;
    job_.clear();
    if (again_ && !halting_) {
      // The study's folders changed under the scan: once more, on the list
      // as it is now.
      again_ = false;
      status_.clear();
      detail_.clear();
      start(forgetEmpty_);
      if (!running())
        emit updated();
      return;
    }
    if (halting_) {
      // stopAndWait(): whoever asked goes on from here.
      status_.clear();
      detail_.clear();
      emit updated();
      return;
    }
    if (st == Job::State::Done && pass == ScanPass::Identity) {
      // The stations are usable before the coverage is read: starting the
      // second job says so (it emits `updated` with a scan running again).
      run(ScanPass::Coverage);
      return;
    }
    const QStringList away = unavailable();
    status_ =
        st == Job::State::Done
            ? away.isEmpty()
                  ? QString()
                  : tr("Not available now (a drive that is not connected?), "
                       "what was known of it is kept: %1")
                        .arg(away.join(QStringLiteral(", ")))
            : st == Job::State::Cancelled
                  ? tr("The scan was stopped; what it had read is kept.")
                  : tr("The scan ended with errors; see the Jobs panel.");
    detail_ = status_;
    emit updated();
  });
}

// ---- the dialog ----

namespace {

QString dayText(double epoch) {
  return QDateTime::fromSecsSinceEpoch(qint64(epoch), QTimeZone::UTC)
      .toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

WaveformsDialog::WaveformsDialog(AppContext ctx, WaveScan *scan,
                                 QWidget *parent)
    : QDialog(parent), ctx_(ctx), scan_(scan) {
  setWindowTitle(tr("Waveforms of this study"));
  intro_ = new QLabel(
      tr("Folders holding the waveforms you already have. Any layout works: "
         "the station, the channels and the times are read from the miniSEED "
         "headers, plain files or zip archives, not from folder or file "
         "names. Other files are listed as skipped."),
      this);
  intro_->setWordWrap(true);
  intro_->setObjectName(QStringLiteral("fieldLabel"));

  folders_ = new QListWidget(this);
  folders_->setObjectName(QStringLiteral("waveFolders"));
  folders_->setMaximumHeight(96);
  setAcceptDrops(true);
  add_ = new QPushButton(tr("Add folders…"), this);
  ui::setPrimary(add_);
  repoint_ = new QPushButton(tr("Moved…"), this);
  remove_ = new QPushButton(tr("Remove"), this);
  rescan_ = new QPushButton(tr("Scan again"), this);
  stop_ = new QPushButton(tr("Stop"), this);
  auto *folderButtons = new QVBoxLayout;
  folderButtons->addWidget(add_);
  folderButtons->addWidget(repoint_);
  folderButtons->addWidget(remove_);
  folderButtons->addStretch(1);
  auto *folderRow = new QHBoxLayout;
  folderRow->addWidget(folders_, 1);
  folderRow->addLayout(folderButtons);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("fieldLabel"));
  auto *scanRow = new QHBoxLayout;
  scanRow->addWidget(status_, 1);
  scanRow->addWidget(stop_);
  scanRow->addWidget(rescan_);

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("waveStations"));
  table_->setColumnCount(10);
  table_->setHorizontalHeaderLabels(
      {tr("Station"), tr("Network"), tr("Channels"), tr("Rate"),
       tr("First day"), tr("Last day"), tr("Days with data"), tr("Files"),
       tr("Coordinates"), tr("Notes")});
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  ui::setEmptyHint(table_, tr("No stations yet.\nAdd the folder that holds "
                              "your miniSEED files, or drop it here."));

  problem_ = new QLabel(this);
  problem_->setObjectName(QStringLiteral("fieldLabel"));
  problem_->setWordWrap(true);
  coords_ = new QPushButton(tr("Type coordinates…"), this);
  xml_ = new QPushButton(tr("Load StationXML…"), this);
  skipped_ = new QPushButton(this);
  near_ = new QPushButton(tr("Find earthquakes near these…"), this);
  near_->setObjectName(QStringLiteral("waveFindNear"));
  auto *below = new QHBoxLayout;
  below->addWidget(coords_);
  below->addWidget(xml_);
  below->addWidget(near_);
  coverage_ = new QPushButton(tr("Station coverage…"), this);
  coverage_->setObjectName(QStringLiteral("waveCoverage"));
  coverage_->setToolTip(ui::helpText(
      tr("A map of what the selected stations cover (all of them when none "
         "is selected): the area within a distance of them and the "
         "earthquakes of the catalog that fall inside it.")));
  below->addWidget(coverage_);
  below->addStretch(1);
  below->addWidget(skipped_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro_);
  layout->addLayout(folderRow);
  layout->addLayout(scanRow);
  layout->addWidget(table_, 1);
  layout->addWidget(problem_);
  layout->addLayout(below);
  layout->addWidget(buttons);
  resize(980, 640);

  folders_->setToolTip(ui::helpText(
      tr("The waveform folders of this study. Each is scanned through all "
         "its sub-folders, except those whose name starts with _ or a dot.")));
  add_->setToolTip(ui::helpText(
      tr("Adds one or several folders of miniSEED files (plain or zipped) "
         "and scans them. Hold Ctrl or Shift to pick more than one. A folder "
         "of station folders (stations/CODE/…) is added once, as a whole; "
         "folders can also be dropped on this window.")));
  near_->setToolTip(ui::helpText(
      tr("Goes on to adding a case, listing only the earthquakes close to "
         "the stations selected in the table (all stations when none is "
         "selected).")));
  repoint_->setToolTip(ui::helpText(
      tr("Tells the study that the selected folder now lives somewhere else, "
         "for example on another drive. Files that did not change are not "
         "read again.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected folder out of the study. Nothing on disk is "
         "deleted.")));
  rescan_->setToolTip(ui::helpText(
      tr("Looks at the folders again. Only new or changed files are read.")));
  stop_->setToolTip(ui::helpText(
      tr("Stops the scan. What it has read so far is kept.")));
  table_->setToolTip(ui::helpText(
      tr("One row per station found in the files. Last day and Days with "
         "data are exact once the coverage has been read. Until then they "
         "are an estimate from the periods in the file names, marked ≈ "
         "(gaps are not known yet), or a dash when the names carry no "
         "period.")));
  coords_->setToolTip(ui::helpText(
      tr("For a station the station list does not know: type its latitude "
         "and longitude once. They are kept in the study and used for "
         "distances and for the Features calibration.")));
  xml_->setToolTip(ui::helpText(
      tr("Reads station coordinates from a StationXML file (as FDSN data "
         "centres provide) for the stations the station list does not "
         "know.")));
  skipped_->setToolTip(ui::helpText(
      tr("Lists the files that were not recognised as miniSEED, with the "
         "reason.")));

  connect(add_, &QPushButton::clicked, this, &WaveformsDialog::addFolder);
  connect(near_, &QPushButton::clicked, this, [this] {
    QStringList codes;
    const bool any = !table_->selectedItems().isEmpty();
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem *it = table_->item(row, 0);
      if (it && (!any || it->isSelected()) && !codes.contains(it->text()))
        codes << it->text();
    }
    findNear_ = codes;
    if (!codes.isEmpty())
      accept();
  });
  connect(coverage_, &QPushButton::clicked, this, [this] {
    QStringList codes;
    const bool any = !table_->selectedItems().isEmpty();
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem *it = table_->item(row, 0);
      if (it && (!any || it->isSelected()) && !codes.contains(it->text()))
        codes << it->text();
    }
    CoverageDialog dialog(ctx_, this, codes);
    dialog.exec();
  });
  connect(repoint_, &QPushButton::clicked, this, &WaveformsDialog::repoint);
  connect(remove_, &QPushButton::clicked, this, &WaveformsDialog::removeFolder);
  connect(rescan_, &QPushButton::clicked, this, &WaveformsDialog::rescan);
  connect(stop_, &QPushButton::clicked, scan_, &WaveScan::cancel);
  connect(coords_, &QPushButton::clicked, this,
          &WaveformsDialog::typeCoordinates);
  connect(xml_, &QPushButton::clicked, this, &WaveformsDialog::loadStationXml);
  connect(skipped_, &QPushButton::clicked, this, &WaveformsDialog::showSkipped);
  connect(folders_, &QListWidget::itemSelectionChanged, this,
          &WaveformsDialog::refresh);
  connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
    coords_->setEnabled(!table_->selectedItems().isEmpty());
  });
  connect(scan_, &WaveScan::updated, this, &WaveformsDialog::refresh);
  connect(ctx_.study, &study::StudyStore::changed, this,
          &WaveformsDialog::refresh);
  refresh();
}

bool WaveformsDialog::edit(const std::function<void(Study &)> &change) {
  QString error;
  if (ctx_.study->update(change, &error))
    return true;
  problem_->setProperty("auto", false);
  problem_->setText(tr("The study could not be saved: %1").arg(error));
  return false;
}

void WaveformsDialog::refresh() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const QString keepFolder =
      folders_->currentItem()
          ? folders_->currentItem()->data(Qt::UserRole).toString()
          : QString();
  {
    const QSignalBlocker block(folders_);
    folders_->clear();
    const QStringList away =
        study::unavailableWaveRoots(s, *WaveIndex::shared(s.dir));
    for (const study::WaveRoot &r : s.waveRoots) {
      const QString path = s.resolved(r.path);
      auto *item = new QListWidgetItem(
          !QDir(path).exists()
              ? tr("%1  (not found)").arg(path)
              : away.contains(QDir(path).path())
                    ? tr("%1  (holds nothing now: a drive that is not "
                         "connected? What was known of it is kept)")
                          .arg(path)
                    : path,
          folders_);
      item->setData(Qt::UserRole, r.id);
      if (r.id == keepFolder)
        folders_->setCurrentItem(item);
    }
  }
  const bool selected = folders_->currentItem() != nullptr;
  // Also while a scan runs: it is stopped first and started again after.
  repoint_->setEnabled(selected);
  remove_->setEnabled(selected);
  rescan_->setEnabled(!scan_->running() && !s.waveRoots.isEmpty());
  stop_->setVisible(scan_->running());

  const auto index = WaveIndex::shared(s.dir);
  const QString path = s.stationListPath();
  if (path != listedFrom_) {
    listedFrom_ = path;
    try {
      listed_ = analysis::loadStations(path);
    } catch (const std::exception &) {
      listed_.clear();
    }
  }
  const auto inList = [this](const QString &code) {
    for (const analysis::Station &st : listed_)
      if (st.code.compare(code, Qt::CaseInsensitive) == 0)
        return true;
    return false;
  };
  const QString keepStation =
      table_->currentRow() >= 0 && table_->item(table_->currentRow(), 0)
          ? table_->item(table_->currentRow(), 0)->text()
          : QString();
  const QList<study::WaveStation> stations = index->stations();
  int unknown = 0;
  {
    const QSignalBlocker block(table_);
    table_->setRowCount(int(stations.size()));
    for (int i = 0; i < int(stations.size()); ++i) {
      const study::WaveStation &w = stations[i];
      QStringList rates;
      for (double r : w.rates)
        rates << tr("%1 Hz").arg(r);
      QString coords = tr("unknown");
      bool known = true;
      if (inList(w.code)) {
        coords = tr("station list");
      } else {
        known = false;
        for (const study::ExtraStation &e : s.extraStations)
          if (e.code.compare(w.code, Qt::CaseInsensitive) == 0) {
            coords = e.source == QLatin1String("stationxml") ? tr("StationXML")
                                                              : tr("typed");
            known = true;
          }
      }
      QStringList notes;
      if (!known) {
        ++unknown;
        notes << tr("⚠ no coordinates");
      }
      if (w.mixedRates)
        notes << tr("⚠ mixed sample rates");
      if (!w.otherFolders.isEmpty())
        notes << tr("⚠ in a folder named %1")
                     .arg(w.otherFolders.join(QStringLiteral(", ")));
      if (w.sharedFiles)
        notes << tr("shares files with other stations");
      const QStringList cells = {
          w.code,
          w.network,
          w.channels.join(QStringLiteral(" ")),
          rates.join(QStringLiteral(", ")),
          dayText(w.first),
          // Exact once every file has been read; "≈" while some of it is an
          // estimate from the file names; a lower bound when even that is
          // missing.
          w.coverageKnown
              ? dayText(w.last - 1e-3)
              : w.extentEstimated ? tr("≈ %1").arg(dayText(w.last - 1e-3))
                                  : tr("≥ %1").arg(dayText(w.last)),
          w.coverageKnown
              ? QString::number(w.days)
              : w.extentEstimated ? tr("≈ %1").arg(w.days)
                                  : QStringLiteral("–"),
          QString::number(w.files),
          coords,
          notes.join(QStringLiteral(" · "))};
      for (int col = 0; col < int(cells.size()); ++col)
        table_->setItem(i, col, new QTableWidgetItem(cells[col]));
      // The notes can be longer than the column is wide.
      table_->item(i, 9)->setToolTip(notes.join(QLatin1Char('\n')));
      if (w.code == keepStation)
        table_->selectRow(i);
    }
    table_->resizeColumnsToContents();
  }
  coords_->setEnabled(!table_->selectedItems().isEmpty());
  const int nSkipped = int(index->skipped().size());
  skipped_->setText(tr("Skipped files (%1)…").arg(nSkipped));
  skipped_->setEnabled(nSkipped > 0);
  const int pending = index->pendingCoverage();
  const int estimated = index->estimatedFiles();
  QString status = scan_->detail();
  if (status.isEmpty() && !index->isEmpty())
    status = pending > 0
                 ? tr("%1 stations in %2 files; coverage not read yet for %3 "
                      "of them.")
                       .arg(stations.size())
                       .arg(index->fileCount())
                       .arg(pending)
                 : tr("%1 stations in %2 files; coverage read for all.")
                       .arg(stations.size())
                       .arg(index->fileCount());
  else if (scan_->running() && estimated > 0)
    status += QStringLiteral(" · ") +
              tr("%n file(s) shown by an estimate from their names until "
                 "then (≈)",
                 nullptr, estimated);
  status_->setText(status);
  if (unknown > 0) {
    problem_->setProperty("auto", true);
    problem_->setText(
        tr("⚠ %n station(s) are not in the station list. Select the row and "
           "type the coordinates, or load a StationXML file; without them "
           "the station cannot be offered for a case.",
           nullptr, unknown));
  } else if (problem_->property("auto").toBool()) {
    problem_->clear();
  }
}

void WaveformsDialog::addFolder() {
  addFolders(
      pickFolders(this, tr("Folders of waveforms"), ctx_.study->study().dir));
}

void WaveformsDialog::addFolders(const QStringList &paths) {
  if (paths.isEmpty())
    return;
  // No scan works on a list of folders that is being changed.
  const bool wasScanning = scan_->running();
  scan_->stopAndWait();
  Study::RootsAdded r;
  if (!edit([&](Study &s) { r = s.addWaveRoots(paths); })) {
    if (wasScanning)
      scan_->start();
    return;
  }
  QStringList notes;
  if (!r.covered.isEmpty())
    notes << tr("Already covered by a listed folder: %1")
                 .arg(r.covered.join(QStringLiteral(", ")));
  if (!r.replaced.isEmpty())
    notes << tr("Now covered by a folder that holds it, so taken off the "
                "list (its files are read again once): %1")
                 .arg(r.replaced.join(QStringLiteral(", ")));
  if (!notes.isEmpty()) {
    problem_->setProperty("auto", false);
    problem_->setText(notes.join(QStringLiteral("\n")));
  }
  if (!r.added.isEmpty() || wasScanning)
    scan_->start();
}

void WaveformsDialog::rescan() {
  // A folder that is there but lists nothing keeps what the index knows of
  // it (a drive that is not connected looks the same as files that were
  // deleted). Forgetting it is the user's word.
  const QStringList away = scan_->unavailable();
  bool forget = false;
  if (!away.isEmpty()) {
    const QString question =
        tr("These folders hold nothing now:\n\n%1\n\nIf a drive is just not "
           "connected, keep what the study knows of them: connect it and "
           "scan again. Forget their files only if they are really gone.\n\n"
           "Forget what the study knows of these folders?")
            .arg(away.join(QLatin1Char('\n')));
    forget = confirmForget
                 ? confirmForget(question)
                 : QMessageBox::question(this, tr("Scan again"), question,
                                         QMessageBox::Yes | QMessageBox::No,
                                         QMessageBox::No) == QMessageBox::Yes;
  }
  scan_->start(forget);
}

void WaveformsDialog::dragEnterEvent(QDragEnterEvent *e) {
  if (!foldersOf(e->mimeData()).isEmpty())
    e->acceptProposedAction();
}

void WaveformsDialog::dropEvent(QDropEvent *e) {
  const QStringList dropped = foldersOf(e->mimeData());
  if (dropped.isEmpty())
    return;
  addFolders(dropped);
  e->acceptProposedAction();
}

void WaveformsDialog::repoint() {
  if (!folders_->currentItem())
    return;
  const QString id = folders_->currentItem()->data(Qt::UserRole).toString();
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Where the folder is now"),
      ctx_.study->study().waveRootPath(id));
  if (path.isEmpty())
    return;
  const QString old = ctx_.study->study().waveRootPath(id);
  scan_->stopAndWait();
  if (edit([&](Study &s) {
        // The same step as "locate missing inputs": whatever else the study
        // names under the folder that moved follows it.
        s.relocate(old, path);
        for (study::WaveRoot &r : s.waveRoots)
          if (r.id == id && QDir::cleanPath(s.resolved(r.path)) !=
                                QDir::cleanPath(path))
            r.path = s.stored(path); // the old place is still there
      }))
    scan_->start();
  else
    scan_->start(); // not saved: what was stopped for the edit goes on
}

void WaveformsDialog::removeFolder() {
  if (!folders_->currentItem())
    return;
  const QString id = folders_->currentItem()->data(Qt::UserRole).toString();
  // A scan that is running would write the folder back into the index from
  // what it holds in memory: it is stopped first (and has saved), and goes
  // on afterwards with the folders that are left.
  const bool wasScanning = scan_->running();
  if (!scan_->stopAndWait()) {
    problem_->setProperty("auto", false);
    problem_->setText(tr("The scan is still stopping; try again in a "
                         "moment."));
    return;
  }
  if (!edit([&id](Study &s) {
        s.waveRoots.removeIf(
            [&id](const study::WaveRoot &r) { return r.id == id; });
      })) {
    if (wasScanning)
      scan_->start();
    return;
  }
  // The index forgets the folder at once, not only at the next scan.
  WaveIndex index = WaveIndex::load(ctx_.study->study().dir);
  if (index.roots.remove(id) > 0)
    index.save(ctx_.study->study().dir);
  if (wasScanning)
    scan_->start();
  refresh();
}

void WaveformsDialog::typeCoordinates() {
  const int row = table_->currentRow();
  if (row < 0 || !table_->item(row, 0))
    return;
  const QString code = table_->item(row, 0)->text();
  const QString network = table_->item(row, 1)->text();
  QDialog d(this);
  d.setWindowTitle(tr("Coordinates of %1").arg(code));
  auto *lat = new QDoubleSpinBox(&d);
  lat->setRange(-90, 90);
  lat->setDecimals(4);
  auto *lon = new QDoubleSpinBox(&d);
  lon->setRange(-180, 180);
  lon->setDecimals(4);
  for (const study::ExtraStation &e : ctx_.study->study().extraStations)
    if (e.code == code) {
      lat->setValue(e.lat);
      lon->setValue(e.lon);
    }
  auto *form = new QFormLayout;
  form->addRow(tr("Latitude (°N)"), lat);
  form->addRow(tr("Longitude (°E)"), lon);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  auto *l = new QVBoxLayout(&d);
  l->addLayout(form);
  l->addWidget(buttons);
  ui::setHelp(lat, tr("Latitude of the station in decimal degrees, north "
                      "positive."));
  ui::setHelp(lon, tr("Longitude of the station in decimal degrees, east "
                      "positive."));
  if (d.exec() != QDialog::Accepted)
    return;
  const double la = lat->value(), lo = lon->value();
  edit([&](Study &s) {
    s.extraStations.removeIf(
        [&code](const study::ExtraStation &e) { return e.code == code; });
    s.extraStations << study::ExtraStation{code, network, la, lo,
                                           QStringLiteral("typed")};
  });
}

void WaveformsDialog::loadStationXml() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("StationXML file"), ctx_.study->study().dir,
      tr("StationXML (*.xml);;All files (*)"));
  if (path.isEmpty())
    return;
  try {
    const QList<study::ExtraStation> read = study::readStationXml(path);
    const auto index = WaveIndex::shared(ctx_.study->study().dir);
    int taken = 0;
    edit([&](Study &s) {
      for (const study::ExtraStation &e : read) {
        // Only stations the folders hold; a whole network's list is not
        // copied into the study.
        if (!index->hasStation(e.code))
          continue;
        s.extraStations.removeIf([&e](const study::ExtraStation &x) {
          return x.code == e.code;
        });
        s.extraStations << e;
        ++taken;
      }
    });
    problem_->setProperty("auto", false);
    problem_->setText(
        tr("%1 of the %2 stations in the file are in the waveform folders "
           "and now have coordinates.")
            .arg(taken)
            .arg(read.size()));
  } catch (const std::exception &e) {
    problem_->setProperty("auto", false);
    problem_->setText(QString::fromUtf8(e.what()));
  }
}

void WaveformsDialog::showSkipped() {
  const Study &s = ctx_.study->study();
  QStringList lines;
  for (const study::SkippedFile &f : WaveIndex::shared(s.dir)->skipped())
    lines << QStringLiteral("%1\t%2")
                 .arg(QDir(s.waveRootPath(f.rootId)).filePath(f.rel), f.why);
  QDialog d(this);
  d.setWindowTitle(tr("Files that were skipped"));
  auto *text = new QPlainTextEdit(lines.join(QLatin1Char('\n')), &d);
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &d);
  connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::accept);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(text);
  l->addWidget(buttons);
  d.resize(760, 420);
  d.exec();
}

} // namespace skg
