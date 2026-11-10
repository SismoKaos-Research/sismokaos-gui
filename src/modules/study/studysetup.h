// Setting a study up without typing paths one at a time: the list of
// waveform folders (several picked at once, or dropped from the file
// manager), the dialog shown after "New study" (and later as "Study
// inputs…"), and downloading an earthquake catalog for the study from an
// FDSN event service.
#pragma once

#include <QDialog>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

#include "core/module.h"
#include "study/catalogs.h"
#include "study/study.h"
#include "tdvms/http.h"

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMimeData;
class QPushButton;
class QRadioButton;

namespace skg {

class Job;
namespace study {
class StudyStore;
}

// Several folders in one go. Qt's own file dialog is used, not the
// system's: the system dialogs of Windows, macOS and the Linux desktops pick
// one folder only, while Qt's looks and works the same on all three and
// picks several once its views are switched to extended selection. Tests
// replace it through the hook.
QStringList pickFolders(QWidget *parent, const QString &title,
                        const QString &startDir);
extern std::function<QStringList(QWidget *parent, const QString &title,
                                 const QString &startDir)>
    pickFoldersHook;
// The local folders among what was dropped (files count as their folder).
QStringList foldersOf(const QMimeData *mime);

// A list of folders with Add… and Remove; folders can be dropped on it.
class FolderList : public QWidget {
  Q_OBJECT
public:
  explicit FolderList(const QString &startDir, QWidget *parent = nullptr);
  QStringList folders() const;
  // Adds what is not listed yet, and not inside a listed folder; a listed
  // folder inside a new one leaves the list. `note()` says what happened.
  void addFolders(const QStringList &paths);
  QString note() const;
  QPushButton *addButton() const { return add_; }

signals:
  void changed();

protected:
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private:
  QString startDir_;
  QListWidget *list_;
  QPushButton *add_, *remove_;
  QLabel *note_;
};

// "Set up the study": which earthquake catalog, which waveform folders.
// Everything can be left as it is. Accepting saves the choices; the Cases
// page then scans the folders in the background.
class StudySetupDialog : public QDialog {
  Q_OBJECT
public:
  StudySetupDialog(study::StudyStore *store, QWidget *parent = nullptr);
  void accept() override;
  FolderList *folderList() const { return folders_; }

private:
  void chooseCatalog();
  void updateState();

  study::StudyStore *store_;
  QRadioButton *builtIn_, *own_;
  QLineEdit *catalog_;
  QPushButton *browse_;
  QComboBox *time_;
  QLabel *builtInNote_, *later_;
  FolderList *folders_;
};

// "Get the catalog for this study": the period and the area the cases need,
// the smallest magnitude and the service. Accepting only returns the
// request; the caller runs it (startCatalogFetch).
class CatalogFetchDialog : public QDialog {
  Q_OBJECT
public:
  CatalogFetchDialog(const study::Study &study, QWidget *parent = nullptr);
  study::CatalogQuery query() const;
  QString serviceId() const;

private:
  void updateState();

  QComboBox *service_;
  QLineEdit *url_;
  QDateEdit *from_, *to_;
  QDoubleSpinBox *minLat_, *maxLat_, *minLon_, *maxLon_, *minMag_;
  QLabel *note_, *problem_;
  QPushButton *ok_;
};

// Runs the request as a job with progress and Stop. When it ends well the
// file becomes the study's catalog (times in UTC). `done` is called on the
// GUI thread with what to tell the user (and whether it worked).
using HttpFactory = std::function<std::unique_ptr<tdvms::Http>()>;
Job *startCatalogFetch(AppContext ctx, const study::CatalogQuery &query,
                       const QString &serviceId,
                       const std::function<void(bool ok, const QString &text)>
                           &done = {},
                       HttpFactory http = {});

} // namespace skg
