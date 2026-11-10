// Importing waveforms the user already has: the background scan of the
// study's waveform folders, and the dialog that lists the folders and shows
// what was found in them before any of it is used.
#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <vector>

#include "analysis/events.h"
#include "core/module.h"
#include "study/study.h"
#include "study/waveindex.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace skg {

class Job;

// Scans every waveform folder of the open study as jobs: first which
// stations are there (fast), then the exact coverage. The index file in the
// study folder is rewritten as the scan goes; `updated` says when.
class WaveScan : public QObject {
  Q_OBJECT
public:
  WaveScan(AppContext ctx, QObject *parent = nullptr);

  bool running() const { return !job_.isNull(); }
  // Does nothing while a scan is running or no study is open. A folder that
  // is there but lists nothing (a drive that is not connected) keeps what
  // the index knows of it, unless `forgetEmpty` is set: only when the user
  // said its files are gone.
  void start(bool forgetEmpty = false);
  void cancel();
  // Stops a running scan and waits (the event loop keeps turning) until it
  // has ended and saved, at most `timeoutMs`. For changing the study's
  // waveform folders: no scan works on a list that is being edited. Not a
  // stop "by the user": start() may be called again right after. True when
  // no scan is running at the end.
  bool stopAndWait(int timeoutMs = 20000);
  // The waveform folders that are on disk but hold nothing now, while the
  // index knows files of them (absolute paths).
  QStringList unavailable() const;
  // Whether the last scan was stopped with cancel(): such a scan is not
  // taken up again behind the user's back.
  bool stoppedByUser() const { return stoppedByUser_; }
  // "Reading coverage: 12%, about 9 min left" while running, or how the last
  // scan ended. `detail` is the same with the amounts and the speed.
  QString status() const { return status_; }
  QString detail() const { return detail_; }

  // How many files the exact scan reads at once: the setting, or what suits
  // the machine when it is 0 (automatic).
  static int workers(const QSettings *settings);

signals:
  void updated(); // the index changed, or the status did

private:
  // What a running scan job may ask about, from its own thread: the ids of
  // the folders the study lists right now. A folder taken off the list
  // while the scan runs must not come back into the index with the scan's
  // next save.
  struct Live;
  void run(study::ScanPass pass);
  void studyChanged();
  QString rootsKey() const; // ids and paths of the study's folders

  AppContext ctx_;
  QPointer<Job> job_;
  QString status_, detail_;
  QElapsedTimer sinceUpdate_;
  bool stoppedByUser_ = false;
  bool halting_ = false;     // stopAndWait(): do not go on to the next pass
  bool again_ = false;       // the folders changed under the scan: once more
  bool forgetEmpty_ = false; // of the scan that is running
  QString scannedRoots_;     // rootsKey() when the running scan started
  std::shared_ptr<Live> live_;
};

class WaveformsDialog : public QDialog {
  Q_OBJECT
public:
  WaveformsDialog(AppContext ctx, WaveScan *scan, QWidget *parent = nullptr);
  // The stations selected when "Find earthquakes near these…" closed the
  // dialog; empty otherwise.
  QStringList findNear() const { return findNear_; }
  // Adds the folders (none twice, none inside another) and scans them.
  void addFolders(const QStringList &paths);
  // "Scan again" asks before it forgets a folder that is there but lists
  // nothing; the answer comes from here (a message box unless a test sets
  // it). The text names the folders.
  std::function<bool(const QString &question)> confirmForget;

protected:
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private:
  void refresh();
  void addFolder();
  void repoint();
  void removeFolder();
  void rescan();
  void typeCoordinates();
  void loadStationXml();
  void showSkipped();
  bool edit(const std::function<void(study::Study &)> &change);

  AppContext ctx_;
  WaveScan *scan_;
  std::vector<analysis::Station> listed_; // the station list alone
  QString listedFrom_;
  QLabel *intro_, *status_, *problem_;
  QListWidget *folders_;
  QPushButton *add_, *repoint_, *remove_, *rescan_, *stop_;
  QTableWidget *table_;
  QPushButton *coords_, *xml_, *skipped_, *near_;
  QPushButton *coverage_; // what the selected stations cover, on a map
  QStringList findNear_;
};

} // namespace skg
