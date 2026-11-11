#include "modules/study/casespage.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUrl>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QPointer>
#include <QFontMetrics>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <exception>
#include <memory>

#include "core/defaults.h"
#include "core/theme.h"
#include "core/ui.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casemap.h"
#include "modules/study/coveragedialog.h"
#include "modules/study/studydisk.h"
#include "modules/study/studysetup.h"
#include "modules/study/studyswitcher.h"
#include "modules/study/waveforms.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/catalogs.h"
#include "study/runs.h"
#include "tdvms/catalog.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "text/text.h"

namespace skg {

using study::Case;
using study::Study;

namespace {

constexpr int kSortRole = Qt::UserRole + 1;     // what a cell sorts by
constexpr int kCoverageRole = Qt::UserRole + 2; // 0..1 of the span, or < 0

// A cell that sorts by a number or a folded text of its own, not by what it
// shows ("10 km" before "9 km", "Şırnak" after "Siirt").
class SortItem : public QTableWidgetItem {
public:
  SortItem(const QString &text, const QVariant &key) : QTableWidgetItem(text) {
    setData(kSortRole, key);
  }
  bool operator<(const QTableWidgetItem &other) const override {
    const QVariant a = data(kSortRole), b = other.data(kSortRole);
    if (a.userType() == QMetaType::Double && b.userType() == QMetaType::Double)
      return a.toDouble() < b.toDouble();
    return QString::compare(a.toString(), b.toString()) < 0;
  }
};

// The Waveforms cell of the station table: a small bar of how much of the
// span has data, before the words that say the same.
class CoverageDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  static constexpr int kBar = 44, kGap = 8;
  void paint(QPainter *p, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    const QVariant v = index.data(kCoverageRole);
    if (!v.isValid() || v.toDouble() < 0) {
      QStyledItemDelegate::paint(p, option, index);
      return;
    }
    // The cell as the style draws it, without its text; then the bar, and
    // the text to the right of it.
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const QString words = opt.text;
    opt.text.clear();
    const QWidget *w = option.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
    const double f = qBound(0.0, v.toDouble(), 1.0);
    QRectF bar(option.rect.left() + 4, option.rect.center().y() - 3.0,
               kBar - 4, 7);
    const QColor ink = option.palette.color(
        option.state & QStyle::State_Selected ? QPalette::HighlightedText
                                              : QPalette::Text);
    p->save();
    p->setPen(ink);
    p->setFont(opt.font);
    const QRect textRect = option.rect.adjusted(kBar + kGap, 0, -4, 0);
    p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                opt.fontMetrics.elidedText(words, Qt::ElideRight,
                                           textRect.width()));
    p->setRenderHint(QPainter::Antialiasing);
    QColor frame = ink;
    frame.setAlphaF(0.45f);
    p->setPen(QPen(frame, 1));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(bar, 2, 2);
    if (f > 0) {
      QRectF fill = bar.adjusted(1, 1, -1, -1);
      fill.setWidth(std::max(1.5, fill.width() * f));
      QColor solid = ink;
      solid.setAlphaF(0.75f);
      p->setPen(Qt::NoPen);
      p->setBrush(solid);
      p->drawRoundedRect(fill, 1.5, 1.5);
    }
    p->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    if (index.data(kCoverageRole).isValid())
      s.rwidth() += kBar + kGap;
    return s;
  }
};

QString studyKey(const QString &dir) {
  return QString::fromLatin1(
      QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Sha1)
          .toHex()
          .left(16));
}

} // namespace

std::function<bool(QWidget *, const QString &, const QString &)>
    CasesPage::confirm =
        [](QWidget *parent, const QString &title, const QString &text) {
          return QMessageBox::question(parent, title, text) == QMessageBox::Yes;
        };

int CasesPage::runsMadeStale(const Study &before, const Study &after,
                             const QString &caseId) {
  const Case *c0 = before.findCase(caseId);
  const Case *c1 = after.findCase(caseId);
  if (!c0 || !c1)
    return 0;
  QStringList rows;
  for (const study::StationRef &st : c0->stations)
    rows << st.code;
  if (rows.isEmpty())
    rows << QString();
  int n = 0;
  for (const study::Recipe &r : before.recipes)
    for (const QString &station : std::as_const(rows)) {
      if (study::cellOf(before, *c0, station, r).state !=
          study::CellState::Done)
        continue;
      // A station that is unticked keeps its results on disk; it is the
      // runs that stay in the table and stop being current that count.
      bool kept = station.isEmpty();
      for (const study::StationRef &st : c1->stations)
        kept = kept || st.code == station;
      if (kept && study::cellOf(after, *c1, station, r).state !=
                      study::CellState::Done)
        ++n;
    }
  return n;
}

CasesPage::CasesPage(AppContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx) {
  scan_ = new WaveScan(ctx_, this);
  stack_ = new QStackedWidget(this);

  // No study open.
  auto *empty = new QWidget(this);
  noStudy_ = new QLabel(empty);
  noStudy_->setObjectName(QStringLiteral("bigHint"));
  noStudy_->setAlignment(Qt::AlignCenter);
  noStudy_->setWordWrap(true);
  // New study and Open study are in the bar the window draws above every
  // study page; a second pair here would only be in the way.
  auto *emptyLayout = new QVBoxLayout(empty);
  emptyLayout->addStretch(2);
  emptyLayout->addWidget(noStudy_);
  emptyLayout->addStretch(3);
  stack_->addWidget(empty);

  // The study.
  auto *main = new QWidget(this);
  inputs_ = new QGroupBox(main);
  catalogLabel_ = new QLabel(inputs_);
  dataLabel_ = new QLabel(inputs_);
  catalog_ = new QLineEdit(inputs_);
  catalog_->setObjectName(QStringLiteral("studyCatalog"));
  catalog_->setReadOnly(true);
  waveSummary_ = new QLabel(inputs_);
  waveSummary_->setObjectName(QStringLiteral("waveSummary"));
  waveSummary_->setWordWrap(true);
  catalogBrowse_ = new QPushButton(inputs_);
  catalogBrowse_->setObjectName(QStringLiteral("catalogOwn"));
  catalogGet_ = new QPushButton(inputs_);
  catalogGet_->setObjectName(QStringLiteral("catalogGet"));
  catalogUpdate_ = new QPushButton(inputs_);
  catalogUpdate_->setObjectName(QStringLiteral("catalogUpdate"));
  setup_ = new QPushButton(inputs_);
  setup_->setObjectName(QStringLiteral("studyInputs"));
  scanJob_ = new QPushButton(inputs_);
  scanJob_->setObjectName(QStringLiteral("scanJob"));
  scanJob_->setFlat(true);
  scanJob_->hide();
  waveButton_ = new QPushButton(inputs_);
  waveButton_->setObjectName(QStringLiteral("waveformsButton"));
  catalogTime_ = new QComboBox(inputs_);
  catalogTime_->setObjectName(QStringLiteral("catalogTime"));
  catalogTime_->addItem(QString(), 0.0);
  catalogTime_->addItem(QString(), 3.0);
  catalogTime_->addItem(QString(), QVariant()); // "Other…"
  auto *grid = new QGridLayout(inputs_);
  grid->addWidget(catalogLabel_, 0, 0);
  catalogFacts_ = new QLabel(inputs_);
  catalogFacts_->setObjectName(QStringLiteral("catalogFacts"));
  catalogFacts_->setProperty("role", QStringLiteral("hint"));
  catalogFacts_->setWordWrap(true);
  auto *catalogColumn = new QVBoxLayout;
  catalogColumn->setSpacing(2);
  catalogColumn->addWidget(catalog_);
  catalogColumn->addWidget(catalogFacts_);
  grid->addLayout(catalogColumn, 0, 1);
  auto *catalogButtons = new QHBoxLayout;
  catalogButtons->addWidget(catalogUpdate_);
  catalogButtons->addWidget(catalogBrowse_);
  catalogButtons->addWidget(catalogGet_);
  catalogButtons->addWidget(catalogTime_);
  auto *waveButtons = new QHBoxLayout;
  waveButtons->addStretch(1);
  waveButtons->addWidget(scanJob_);
  waveButtons->addWidget(waveButton_);
  waveButtons->addWidget(setup_);
  grid->addLayout(catalogButtons, 0, 2);
  grid->addWidget(dataLabel_, 1, 0);
  grid->addWidget(waveSummary_, 1, 1);
  grid->addLayout(waveButtons, 1, 2);
  grid->setColumnStretch(1, 1);

  banner_ = new QLabel(main);
  banner_->setObjectName(QStringLiteral("banner"));
  banner_->setWordWrap(true);
  banner_->hide();
  reload_ = new QPushButton(main);
  reload_->setObjectName(QStringLiteral("reloadStudy"));
  reload_->hide();
  lockNote_ = new QLabel(main);
  lockNote_->setObjectName(QStringLiteral("lockNote"));
  lockNote_->setWordWrap(true);
  lockNote_->hide();

  add_ = new QPushButton(main);
  ui::setPrimary(add_);
  addRef_ = new QPushButton(main);
  remove_ = new QPushButton(main);
  summary_ = new QLabel(main);
  summary_->setObjectName(QStringLiteral("fieldLabel"));
  auto *bar = new QHBoxLayout;
  bar->addWidget(add_);
  bar->addWidget(addRef_);
  bar->addWidget(remove_);
  more_ = new QPushButton(main);
  more_->setObjectName(QStringLiteral("caseMore"));
  auto *moreMenu = new QMenu(more_);
  more_->setMenu(moreMenu);
  connect(moreMenu, &QMenu::aboutToShow, this,
          [this, moreMenu] { fillMenu(moreMenu); });
  bar->addWidget(more_);
  coverageButton_ = new QPushButton(main);
  coverageButton_->setObjectName(QStringLiteral("stationCoverage"));
  bar->addWidget(coverageButton_);
  filter_ = new QLineEdit(main);
  filter_->setObjectName(QStringLiteral("caseFilter"));
  filter_->setClearButtonEnabled(true);
  filter_->setMaximumWidth(260);
  bar->addWidget(filter_, 1);
  bar->addStretch(1);
  bar->addWidget(summary_);

  table_ = new QTableWidget(main);
  table_->setObjectName(QStringLiteral("caseTable"));
  table_->setColumnCount(9);
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  // Sorted by a click on a heading; in the study's order until then.
  table_->horizontalHeader()->setSortIndicatorClearable(true);
  table_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
  table_->setSortingEnabled(true);
  table_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(table_, &QWidget::customContextMenuRequested, this,
          [this](const QPoint &at) {
            QMenu menu(this);
            fillMenu(&menu);
            menu.exec(table_->viewport()->mapToGlobal(at));
          });
  connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this,
          [this] { saveTableState(); });
  connect(table_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
          [this] { saveTableState(); });

  auto *card = new QFrame(main);
  card->setObjectName(QStringLiteral("card"));
  detail_ = card;
  name_ = new QLineEdit(card);
  name_->setObjectName(QStringLiteral("caseTitle"));
  facts_ = new QLabel(card);
  facts_->setObjectName(QStringLiteral("fieldLabel"));
  facts_->setWordWrap(true);
  stationsTitle_ = new QLabel(card);
  stationsTitle_->setObjectName(QStringLiteral("cardTitle"));
  stationTable_ = new QTableWidget(card);
  stationTable_->setObjectName(QStringLiteral("stationTable"));
  stationTable_->verticalHeader()->hide();
  stationTable_->setShowGrid(false);
  stationTable_->setAlternatingRowColors(true);
  stationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  stationTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  stationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  stationTable_->horizontalHeader()->setStretchLastSection(true);
  stationTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                         Qt::AlignVCenter);
  stationTable_->setMinimumHeight(120);
  stationTable_->setItemDelegateForColumn(4,
                                          new CoverageDelegate(stationTable_));
  stationTable_->horizontalHeader()->setSectionsClickable(true);
  // A click on Distance or Waveforms orders the candidates by it; a second
  // click goes back to the ticked ones first.
  connect(stationTable_->horizontalHeader(), &QHeaderView::sectionClicked,
          this, [this](int column) {
            if (column != 2 && column != 4)
              return;
            stationSort_ = stationSort_ == column ? 0 : column;
            showCase();
          });
  tickNearest_ = new QPushButton(card);
  tickNearest_->setObjectName(QStringLiteral("tickNearest"));
  untickAll_ = new QPushButton(card);
  untickAll_->setObjectName(QStringLiteral("untickAll"));
  stationHint_ = new QLabel(card);
  stationHint_->setObjectName(QStringLiteral("stationHint"));
  stationHint_->setWordWrap(true);
  folder_ = new QPushButton(card);
  folder_->setObjectName(QStringLiteral("stationFolder"));
  folderClear_ = new QPushButton(card);
  getWave_ = new QPushButton(card);
  getWave_->setObjectName(QStringLiteral("getWaveforms"));
  next_ = new QPushButton(card);
  next_->setObjectName(QStringLiteral("nextRecipes"));
  auto *stationButtons = new QHBoxLayout;
  stationButtons->addWidget(tickNearest_);
  stationButtons->addWidget(untickAll_);
  stationButtons->addWidget(folder_);
  stationButtons->addWidget(folderClear_);
  stationButtons->addStretch(1);
  stationButtons->addWidget(getWave_);
  stationButtons->addWidget(next_);
  zoneLabel_ = new QLabel(card);
  zoneLabel_->setObjectName(QStringLiteral("fieldLabel"));
  zone_ = new QComboBox(card);
  zone_->addItem(QString(), int(study::Zone::Kind::Circle));
  zone_->addItem(QString(), int(study::Zone::Kind::Fault));
  radius_ = new QDoubleSpinBox(card);
  radius_->setObjectName(QStringLiteral("caseRadius"));
  radius_->setRange(1, 2000);
  radius_->setDecimals(0);
  spanLabel_ = new QLabel(card);
  spanLabel_->setObjectName(QStringLiteral("fieldLabel"));
  span_ = new QSpinBox(card);
  span_->setObjectName(QStringLiteral("caseSpan"));
  span_->setRange(1, 3650);
  // The detail of the selected case: what it is on the left, its stations on
  // the right.
  auto *about = new QVBoxLayout;
  about->setSpacing(8);
  about->addWidget(name_);
  about->addWidget(facts_);
  auto *zoneRow = new QHBoxLayout;
  zoneRow->addWidget(zoneLabel_);
  zoneRow->addWidget(zone_, 1);
  zoneRow->addWidget(radius_);
  about->addLayout(zoneRow);
  auto *spanRow = new QHBoxLayout;
  spanRow->addWidget(spanLabel_);
  spanRow->addWidget(span_, 1);
  about->addLayout(spanRow);
  about->addStretch(1);
  auto *stationsCol = new QVBoxLayout;
  stationsCol->setSpacing(8);
  stationsCol->addWidget(stationsTitle_);
  stationsCol->addWidget(stationTable_, 1);
  stationsCol->addWidget(stationHint_);
  stationsCol->addLayout(stationButtons);
  auto *cardLayout = new QHBoxLayout(card);
  cardLayout->setContentsMargins(16, 14, 16, 14);
  cardLayout->setSpacing(20);
  cardLayout->addLayout(about, 2);
  cardLayout->addLayout(stationsCol, 3);

  // The overview: every case on one map, beside the table.
  auto *mapCard = new QFrame(main);
  mapCard->setObjectName(QStringLiteral("card"));
  map_ = new CaseMap(ctx_.theme, mapCard);
  auto *mapLayout = new QVBoxLayout(mapCard);
  mapLayout->setContentsMargins(16, 12, 16, 12);
  mapLayout->addWidget(map_);

  auto *top = new QSplitter(Qt::Horizontal, main);
  top->addWidget(table_);
  top->addWidget(mapCard);
  top->setStretchFactor(0, 1);
  top->setStretchFactor(1, 1);
  top->setChildrenCollapsible(false);
  table_->setMinimumWidth(380);
  top->setSizes({640, 480});
  auto *split = new QSplitter(Qt::Vertical, main);
  split->addWidget(top);
  split->addWidget(card);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);
  split->setChildrenCollapsible(false);
  split->setSizes({390, 350});

  auto *mainLayout = new QVBoxLayout(main);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  auto *bannerRow = new QHBoxLayout;
  bannerRow->addWidget(banner_, 1);
  bannerRow->addWidget(reload_, 0, Qt::AlignTop);
  mainLayout->addLayout(bannerRow);
  mainLayout->addWidget(lockNote_);
  mainLayout->addWidget(inputs_);
  mainLayout->addLayout(bar);
  mainLayout->addWidget(split, 1);
  stack_->addWidget(main);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(stack_);

  connect(catalogBrowse_, &QPushButton::clicked, this,
          &CasesPage::chooseCatalog);
  connect(catalogGet_, &QPushButton::clicked, this, &CasesPage::getCatalog);
  connect(catalogUpdate_, &QPushButton::clicked, this,
          &CasesPage::updateBuiltIn);
  connect(setup_, &QPushButton::clicked, this, &CasesPage::openSetup);
  connect(waveButton_, &QPushButton::clicked, this, &CasesPage::openWaveforms);
  connect(catalogTime_, &QComboBox::activated, this, &CasesPage::offsetChosen);
  connect(folder_, &QPushButton::clicked, this,
          &CasesPage::chooseStationFolder);
  connect(folderClear_, &QPushButton::clicked, this,
          &CasesPage::clearStationFolder);
  connect(getWave_, &QPushButton::clicked, this, &CasesPage::getWaveforms);
  connect(next_, &QPushButton::clicked, this, [this] {
    if (ctx_.nav)
      ctx_.nav->show(QStringLiteral("runs"));
  });
  connect(stationTable_, &QTableWidget::itemSelectionChanged, this,
          &CasesPage::updateStationButtons);
  connect(scan_, &WaveScan::updated, this, [this] {
    updateWaveSummary();
    showCase();
  });
  connect(reload_, &QPushButton::clicked, this, [this] {
    QString error;
    reload_->hide();
    if (ctx_.study->reload(&error))
      showBanner(QString());
    else
      showBanner(tr("⚠ The study file could not be read again: %1").arg(error));
  });
  connect(scanJob_, &QPushButton::clicked, this, [this] {
    if (ctx_.nav)
      ctx_.nav->show(kNavJobsPanel);
  });
  connect(tickNearest_, &QPushButton::clicked, this, &CasesPage::tickNearest);
  connect(untickAll_, &QPushButton::clicked, this, &CasesPage::untickAll);
  connect(filter_, &QLineEdit::textChanged, this, &CasesPage::applyFilter);
  // Delete removes (after the question), Enter goes to the name to edit it,
  // Ctrl+F to the filter.
  table_->installEventFilter(this);
  auto *find = new QShortcut(QKeySequence::Find, this);
  find->setContext(Qt::WidgetWithChildrenShortcut);
  connect(find, &QShortcut::activated, this, [this] {
    filter_->setFocus(Qt::ShortcutFocusReason);
    filter_->selectAll();
  });
  setAcceptDrops(true);
  connect(add_, &QPushButton::clicked, this, &CasesPage::addFromCatalog);
  connect(addRef_, &QPushButton::clicked, this, &CasesPage::addReference);
  connect(remove_, &QPushButton::clicked, this, &CasesPage::removeCase);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &CasesPage::showCase);
  connect(coverageButton_, &QPushButton::clicked, this, [this] {
    CoverageDialog dialog(ctx_, this);
    dialog.exec();
  });
  // The map and the tables select each other.
  connect(map_, &CaseMap::caseClicked, this, [this](const QString &id) {
    for (int i = 0; i < table_->rowCount(); ++i)
      if (table_->item(i, 0)->data(Qt::UserRole).toString() == id) {
        table_->selectRow(i);
        table_->scrollToItem(table_->item(i, 0));
      }
  });
  connect(map_, &CaseMap::stationClicked, this, [this](const QString &code) {
    for (int i = 0; i < stationTable_->rowCount(); ++i)
      if (stationTable_->item(i, 1) &&
          stationTable_->item(i, 1)->text() == code) {
        stationTable_->selectRow(i);
        stationTable_->scrollToItem(stationTable_->item(i, 1));
      }
  });
  // Another page can send the user here with a case (and a station) to fix.
  if (ctx_.nav)
    connect(ctx_.nav, &Navigator::requested, this,
            [this](const QString &page, const QVariantMap &args) {
              if (page != QLatin1String("cases"))
                return;
              const QString id = args.value(QStringLiteral("case")).toString();
              const QString code =
                  args.value(QStringLiteral("station")).toString();
              // A filter must not hide the case the user is sent to.
              for (int i = 0; !id.isEmpty() && i < table_->rowCount(); ++i)
                if (table_->item(i, 0)->data(Qt::UserRole).toString() == id &&
                    table_->isRowHidden(i))
                  filter_->clear();
              for (int i = 0; !id.isEmpty() && i < table_->rowCount(); ++i)
                if (table_->item(i, 0)->data(Qt::UserRole).toString() == id) {
                  table_->selectRow(i);
                  table_->scrollToItem(table_->item(i, 0));
                }
              for (int i = 0; !code.isEmpty() && i < stationTable_->rowCount();
                   ++i)
                if (stationTable_->item(i, 1) &&
                    stationTable_->item(i, 1)->text() == code) {
                  stationTable_->selectRow(i);
                  stationTable_->scrollToItem(stationTable_->item(i, 1));
                }
            });
  connect(name_, &QLineEdit::editingFinished, this, &CasesPage::saveCase);
  connect(stationTable_, &QTableWidget::itemChanged, this,
          &CasesPage::saveCase);
  connect(zone_, &QComboBox::activated, this, &CasesPage::saveCase);
  connect(radius_, &QDoubleSpinBox::editingFinished, this,
          &CasesPage::saveCase);
  connect(span_, &QSpinBox::editingFinished, this, &CasesPage::saveCase);
  connect(ctx_.study, &study::StudyStore::changed, this, &CasesPage::refresh);
  connect(ctx_.theme, &Theme::changed, this, &CasesPage::showCase);

  retranslate();
  refresh();
}

bool CasesPage::eventFilter(QObject *o, QEvent *e) {
  if (o == table_ && e->type() == QEvent::KeyPress) {
    const int key = static_cast<QKeyEvent *>(e)->key();
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
      removeCase();
      return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
        detail_->isEnabled()) {
      name_->setFocus(Qt::ShortcutFocusReason);
      name_->selectAll();
      return true;
    }
  }
  return QWidget::eventFilter(o, e);
}

void CasesPage::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange) {
    retranslate();
    refresh();
  }
  QWidget::changeEvent(e);
}

void CasesPage::retranslate() {
  noStudy_->setText(
      tr("A study runs the same analyses on several earthquakes and puts "
         "the results side by side.\nStart a new study in an empty folder, "
         "or open an existing one."));
  inputs_->setTitle(tr("Study inputs"));
  catalogLabel_->setText(tr("Earthquake catalog"));
  dataLabel_->setText(tr("Waveforms"));
  catalog_->setPlaceholderText(tr("AFAD catalog export, CSV or .gz"));
  catalogBrowse_->setText(tr("Use my own file…"));
  catalogGet_->setText(tr("Get the catalog…"));
  catalogUpdate_->setText(tr("Update…"));
  setup_->setText(tr("Study inputs…"));
  waveButton_->setText(tr("Waveforms…"));
  catalogTime_->setItemText(0, tr("times in UTC"));
  catalogTime_->setItemText(1, tr("times in Turkey time (+3)"));
  catalogTime_->setItemText(2, tr("another offset…"));
  folder_->setText(tr("Choose folder…"));
  folderClear_->setText(tr("Use the study's folders"));
  add_->setText(tr("Add from catalog…"));
  addRef_->setText(tr("Add reference date…"));
  remove_->setText(tr("Remove"));
  reload_->setText(tr("Reload"));
  reload_->setToolTip(ui::helpText(
      tr("Reads the study file again as it is on disk now, with what was "
         "changed elsewhere. The change refused here is not kept.")));
  more_->setText(tr("More"));
  more_->setToolTip(ui::helpText(
      tr("What else can be done with the selected cases: duplicate one, "
         "change the span or the stations of several at once, get their "
         "waveforms together, and put back cases removed since the "
         "application was started. The same list opens with a right click "
         "on the table.")));
  filter_->setPlaceholderText(tr("filter cases… (Ctrl+F)"));
  ui::setHelp(filter_, tr("Shows only the cases whose name, place, date or "
                          "stations contain this text. Capitals and Turkish "
                          "letters do not matter."));
  scanJob_->setText(tr("Show the scan"));
  scanJob_->setToolTip(ui::helpText(
      tr("The waveform folders are being read. This opens the Jobs panel, "
         "where the scan shows how far it is and can be stopped.")));
  tickNearest_->setText(tr("Tick nearest with data"));
  tickNearest_->setToolTip(ui::helpText(
      tr("Ticks the three nearest stations that have waveforms in this "
         "case's span, in addition to those already ticked.")));
  untickAll_->setText(tr("Untick all"));
  untickAll_->setToolTip(ui::helpText(
      tr("Unticks every station of this case. Their results stay on disk "
         "and come back when a station is ticked again.")));
  coverageButton_->setText(tr("Station coverage…"));
  coverageButton_->setToolTip(ui::helpText(
      tr("A map of what your stations cover: the area within a distance of "
         "the stations you have waveforms of (or of the stations ticked in "
         "the cases), and which earthquakes of the catalog fall inside "
         "it.")));
  table_->setHorizontalHeaderLabels(
      {tr("Case"), tr("Type"), tr("Origin time (UTC)"), tr("M"), tr("Depth"),
       tr("Stations"), tr("Span"), tr("Data"), tr("Runs")});
  if (QTableWidgetItem *h = table_->horizontalHeaderItem(7))
    h->setToolTip(ui::helpText(
        tr("How many of the case's ticked stations have waveforms in its "
           "span, of how many are ticked.")));
  if (QTableWidgetItem *h = table_->horizontalHeaderItem(8))
    h->setToolTip(ui::helpText(
        tr("How many of the case's runs are finished and current, of how "
           "many the study's recipes make for it. The Runs page has the "
           "rest.")));
  ui::setEmptyHint(table_,
                   tr("No cases yet.\nAdd from catalog… picks an "
                      "earthquake, from the built-in list or your own "
                      "catalog."));
  stationsTitle_->setText(tr("STATIONS NEAR THE EPICENTRE"));
  zoneLabel_->setText(tr("Catalog zone"));
  zone_->setItemText(0, tr("Circle around the epicentre, radius"));
  zone_->setItemText(1, tr("Fault zone (East Anatolian), buffer"));
  radius_->setSuffix(tr(" km"));
  spanLabel_->setText(tr("Span"));
  span_->setSuffix(tr(" days"));

  ui::setHelp({catalogLabel_, catalog_},
              tr("The earthquake catalog of this study. Without a file of "
                 "its own the study uses the built-in list of large "
                 "earthquakes, which is enough to pick cases. The Indicators "
                 "and the Features calibration count small earthquakes and "
                 "need a full catalog: an AFAD export, or a download."));
  catalogBrowse_->setToolTip(ui::helpText(
      tr("Chooses a catalog file of your own: an AFAD export as CSV, plain "
         "or .gz. Cases are picked from it and the analyses read their "
         "events from it.")));
  catalogGet_->setToolTip(ui::helpText(
      tr("Downloads the earthquakes around this study's cases from an FDSN "
         "event service into the study folder and makes that file the "
         "study's catalog.")));
  catalogUpdate_->setToolTip(ui::helpText(
      tr("Brings the built-in lists up to date over the network: the "
         "stations from the AFAD portal and the large earthquakes from "
         "USGS.")));
  setup_->setToolTip(ui::helpText(
      tr("The catalog and the waveform folders of the study in one dialog, "
         "as when the study was created.")));
  ui::setHelp(catalogTime_,
              tr("Whether the times in the catalog file are UTC or Turkey "
                 "time (UTC+3). AFAD exports are UTC. A wrong choice shifts "
                 "every event by three hours."));
  ui::setHelp({dataLabel_, waveSummary_, waveButton_},
              tr("For the Features recipes: the folders holding the "
                 "waveforms you already have, and what was found in them. "
                 "The station of each file is read from its miniSEED "
                 "headers, so any folder layout works. Not needed for "
                 "Indicators."));
  folder_->setToolTip(ui::helpText(
      tr("For the selected, ticked station: use the files of one folder you "
         "choose, whatever the study's waveform folders hold. For data whose "
         "headers carry a different station code.")));
  folderClear_->setToolTip(ui::helpText(
      tr("Drops the folder chosen for the selected station, so its files "
         "are looked up in the study's waveform folders again.")));
  getWave_->setToolTip(ui::helpText(
      tr("Step 2: opens the Data page with this case's stations and span "
         "filled in, to download what is missing.")));
  next_->setToolTip(ui::helpText(
      tr("Step 3: goes to the Runs page, where recipes are made and run on "
         "the cases. Features recipes need the waveforms first; Indicators "
         "recipes do not.")));
  updateStationButtons();
  add_->setToolTip(ui::helpText(
      tr("Adds a case by picking an earthquake from the study's catalog and "
         "ticking the stations near it.")));
  addRef_->setToolTip(ui::helpText(
      tr("Adds a case with no earthquake: a date at a place, studied the "
         "same way so it can be read beside the earthquakes.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected cases out of the study (also the Delete key). "
         "Their result folders stay on disk, and More ▸ Put back brings a "
         "case back with its runs until the application is closed.")));
  ui::setHelp(name_, tr("The case's name, as shown in the tables and the "
                        "figures. Changes are saved at once. Renaming keeps "
                        "the case's folder and its finished runs."));
  stationTable_->setToolTip(ui::helpText(
      tr("Tick the stations this case is studied from. Each ticked station "
         "is a row of its own on the Runs and Compare pages. Waveforms says "
         "how many days of the span the study's waveform folders hold for "
         "the station; the small bar shows the same share. Click the "
         "Distance or Waveforms heading to order the stations by it.")));
  ui::setHelp({zoneLabel_, zone_},
              tr("Which catalog events count for this case in the Indicators "
                 "recipes: those within a circle around the epicentre, or "
                 "those within a buffer on both sides of the shipped East "
                 "Anatolian fault traces."));
  ui::setHelp(radius_, tr("Radius of the circle, or width of the buffer on "
                          "each side of the fault traces, in km. Larger "
                          "takes in more events."));
  ui::setHelp({spanLabel_, span_},
              tr("How many days before the event are studied. The Indicators "
                 "recipes use the catalog from one window before the span "
                 "up to, but not including, the event. Changing it marks "
                 "the case's finished runs stale."));
}

QString CasesPage::currentId() const {
  // The one selected case; with several selected there is none to show.
  const QStringList ids = selectedIds();
  return ids.size() == 1 ? ids.first() : QString();
}

QStringList CasesPage::selectedIds() const {
  QStringList ids;
  if (!table_->selectionModel())
    return ids;
  QList<int> rows;
  for (const QModelIndex &i : table_->selectionModel()->selectedRows())
    rows << i.row();
  std::sort(rows.begin(), rows.end());
  for (int row : std::as_const(rows))
    if (table_->item(row, 0) && !table_->isRowHidden(row))
      ids << table_->item(row, 0)->data(Qt::UserRole).toString();
  return ids;
}

const std::vector<analysis::Station> &CasesPage::stationList() {
  // The station list plus the study's own extra stations; read again only
  // when either changes.
  const Study &st = ctx_.study->study();
  QString key = st.stationListPath();
  for (const study::ExtraStation &e : st.extraStations)
    key += QStringLiteral("|%1:%2:%3").arg(e.code).arg(e.lat).arg(e.lon);
  if (key != stationsFrom_) {
    stationsFrom_ = key;
    try {
      stations_ = st.stations();
    } catch (const std::exception &) {
      stations_.clear(); // cases keep the stations they already name
    }
  }
  return stations_;
}

void CasesPage::refresh() {
  const bool open = ctx_.study->isOpen();
  stack_->setCurrentIndex(open ? 1 : 0);
  if (!open)
    return;
  const Study &s = ctx_.study->study();
  const QStringList keep = selectedIds();
  updating_ = true;
  // What was removed belongs to the study it was removed from.
  if (removedFrom_ != s.dir) {
    removed_.clear();
    removedFrom_ = s.dir;
  }
  const bool noCatalog = s.catalog.isEmpty();
  const bool lostCatalog = !noCatalog && !QFileInfo::exists(s.catalogPath());
  if (noCatalog) {
    const study::BuiltInCatalog b = study::builtInCatalog();
    // "updated" only when it was: the list as shipped is as old as the
    // release, which the date of its last earthquake bounds from below.
    catalog_->setText(
        (b.refreshed ? tr("Built-in list: M ≥ %1 since %2, updated %3")
                     : tr("Built-in list: M ≥ %1 since %2, as shipped (to %3)"))
            .arg(b.minMagnitude, 0, 'f', 0)
            .arg(b.first.year())
            .arg(b.updated.toString(Qt::ISODate)));
  } else {
    catalog_->setText(s.catalogPath());
  }
  catalog_->setCursorPosition(0);
  // What the catalog holds, and whether that is enough for the tools.
  QString facts;
  if (!noCatalog && !lostCatalog) {
    const study::CatalogSummary cs =
        study::catalogSummary(s.catalogPath(), s.catalogUtcOffsetHours);
    if (cs.ok) {
      facts = tr("%1 events, %2 to %3, magnitudes from about %4")
                  .arg(cs.events)
                  .arg(cs.first.toString(Qt::ISODate),
                       cs.last.toString(Qt::ISODate))
                  .arg(cs.floor, 0, 'f', 1);
      if (cs.floor > defaults::kSparseCatalogMag)
        facts += QStringLiteral("\n") +
                 tr("⚠ Sparse: fine for choosing cases. The calibration of "
                    "the features only avoids earthquakes of that size and "
                    "larger, and the indicators need the small ones too: use "
                    "a full catalog (an AFAD export) for those.");
    }
  }
  catalogFacts_->setText(facts);
  catalogFacts_->setVisible(!facts.isEmpty());
  catalogUpdate_->setVisible(noCatalog);
  // The built-in list is in UTC; there is nothing to choose.
  catalogTime_->setVisible(!noCatalog);
  catalog_->setStyleSheet(
      lostCatalog ? QStringLiteral("color: %1;")
                        .arg(ctx_.theme->statusColor(QStringLiteral("serious"))
                                 .name())
                  : QString());
  add_->setEnabled(!lostCatalog);
  add_->setToolTip(ui::helpText(
      lostCatalog
          ? tr("The catalog file is not where the study says it is: %1. "
               "Choose it again above.")
                .arg(s.catalogPath())
          : noCatalog
                ? tr("Adds a case by picking an earthquake from the "
                     "built-in list of large earthquakes and ticking the "
                     "stations near it.")
                : tr("Adds a case by picking an earthquake from the study's "
                     "catalog and ticking the stations near it.")));
  // The offset: one of the two usual ones, or whatever the study says.
  while (catalogTime_->count() > 3)
    catalogTime_->removeItem(2);
  int offsetIndex = catalogTime_->findData(s.catalogUtcOffsetHours);
  if (offsetIndex < 0) {
    catalogTime_->insertItem(
        2,
        tr("times in UTC%1%2")
            .arg(s.catalogUtcOffsetHours < 0 ? QStringLiteral("−")
                                             : QStringLiteral("+"))
            .arg(std::abs(s.catalogUtcOffsetHours)),
        s.catalogUtcOffsetHours);
    offsetIndex = 2;
  }
  catalogTime_->setCurrentIndex(offsetIndex);
  updateWaveSummary();
  // Waveform folders that were never scanned, or that gained files since
  // (a download on the Data page): scan them, which reads only what is new.
  rescanIfChanged();
  // Filled in the study's order with sorting off, then put back in the
  // order the user chose (a row would move while it is being filled).
  table_->setSortingEnabled(false);
  table_->clearSelection();
  table_->setRowCount(int(s.cases.size()));
  int rows = 0;
  for (int i = 0; i < int(s.cases.size()); ++i) {
    const Case &c = s.cases[i];
    rows += int(c.stations.size());
    QStringList codes;
    int withData = 0;
    for (const study::StationRef &st : c.stations) {
      codes << st.code;
      withData += hasWaveformsIn(s, st, c.spanStart(), c.time) ? 1 : 0;
    }
    // The runs as the Runs page counts them: a row per station (one row
    // for a case without stations), a cell per recipe that applies.
    QStringList rowStations = codes;
    if (rowStations.isEmpty())
      rowStations << QString();
    int done = 0, total = 0;
    for (const study::Recipe &r : s.recipes)
      for (const QString &station : std::as_const(rowStations)) {
        const study::CellState state = study::cellOf(s, c, station, r).state;
        if (state == study::CellState::NotApplicable)
          continue;
        ++total;
        done += state == study::CellState::Done ? 1 : 0;
      }
    const bool noMag = std::isnan(c.magnitude), noDepth = std::isnan(c.depthKm);
    const QString data =
        c.stations.isEmpty()
            ? QStringLiteral("–")
            : (withData < int(c.stations.size()) ? QStringLiteral("⚠ ")
                                                 : QString()) +
                  tr("%1 of %2").arg(withData).arg(c.stations.size());
    const QString runs = total == 0 ? QStringLiteral("–")
                                    : tr("%1 of %2").arg(done).arg(total);
    const QList<QPair<QString, QVariant>> cells = {
        {c.name, text::fold(c.name)},
        {c.reference ? tr("reference") : tr("earthquake"),
         c.reference ? QStringLiteral("1") : QStringLiteral("0")},
        {c.time.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
         double(c.time.toSecsSinceEpoch())},
        {noMag ? QStringLiteral("–") : QString::number(c.magnitude, 'f', 1),
         noMag ? -1.0 : c.magnitude},
        {noDepth ? QStringLiteral("–") : tr("%1 km").arg(c.depthKm, 0, 'f', 0),
         noDepth ? -1.0 : c.depthKm},
        {codes.isEmpty() ? QStringLiteral("–")
                         : codes.join(QStringLiteral(", ")),
         double(codes.size())},
        {tr("%n day(s)", nullptr, c.spanDays), double(c.spanDays)},
        {data, c.stations.isEmpty()
                   ? -1.0
                   : double(withData) / double(c.stations.size())},
        {runs, total == 0 ? -1.0 : double(done) / double(total)}};
    for (int col = 0; col < int(cells.size()); ++col) {
      auto *item = new SortItem(cells[col].first, cells[col].second);
      item->setData(Qt::UserRole, c.id);
      table_->setItem(i, col, item);
    }
    table_->item(i, 0)->setToolTip(
        c.place.isEmpty() ? c.name : c.name + QStringLiteral(" · ") + c.place);
    table_->item(i, 5)->setToolTip(codes.join(QStringLiteral(", ")));
    table_->item(i, 7)->setToolTip(
        c.stations.isEmpty()
            ? tr("No station is ticked: only the Indicators recipes run for "
                 "this case.")
            : tr("%1 of the %2 ticked station(s) have waveforms in the span.")
                  .arg(withData)
                  .arg(c.stations.size()));
    table_->item(i, 8)->setToolTip(
        total == 0
            ? tr("No recipe yet: they are made on the Runs page.")
            : tr("%1 of %2 run(s) are finished and current.")
                  .arg(done)
                  .arg(total));
  }
  // Column widths and the order: as this study was left, else fitted.
  const QString stateKey =
      QStringLiteral("cases/table/") + studyKey(s.dir);
  if (tableStateOf_ != s.dir) {
    tableStateOf_ = s.dir;
    const QByteArray state = ctx_.settings->value(stateKey).toByteArray();
    if (state.isEmpty() || !table_->horizontalHeader()->restoreState(state)) {
      table_->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
      fitColumns();
    }
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSortIndicatorClearable(true);
    table_->horizontalHeader()->setSectionsClickable(true);
  } else if (!ctx_.settings->contains(stateKey)) {
    fitColumns();
  }
  table_->setSortingEnabled(true);
  // The same cases selected as before; the first one when there was none.
  bool any = false;
  for (int row = 0; row < table_->rowCount(); ++row)
    if (keep.contains(table_->item(row, 0)->data(Qt::UserRole).toString())) {
      table_->selectionModel()->select(
          table_->model()->index(row, 0),
          QItemSelectionModel::Select | QItemSelectionModel::Rows);
      if (!any)
        table_->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
      any = true;
    }
  if (!any && !s.cases.isEmpty())
    for (int row = 0; row < table_->rowCount(); ++row)
      if (table_->item(row, 0)->data(Qt::UserRole).toString() ==
          s.cases.first().id)
        table_->selectRow(row);
  summary_->setText(
      tr("%1 cases · %2 station rows").arg(s.cases.size()).arg(rows));
  updating_ = false;
  applyFilter();
  showCase();
  updateEditable();
}

void CasesPage::fitColumns() {
  // To their contents, but a long name or a long list of stations does not
  // push what the case still needs out of sight; the full text is in the
  // cell's tooltip.
  table_->resizeColumnsToContents();
  table_->setColumnWidth(0, std::min(table_->columnWidth(0), 190));
  table_->setColumnWidth(5, std::min(table_->columnWidth(5), 110));
  // A heading is never cut off: it is bold, carries the sort mark, and is
  // longer in some languages than any cell under it.
  for (int c = 0; c < table_->columnCount(); ++c) {
    const QString heading =
        table_->model()->headerData(c, Qt::Horizontal).toString();
    QFont bold = table_->horizontalHeader()->font();
    bold.setBold(true);
    const int need = QFontMetrics(bold).horizontalAdvance(heading) + 34;
    if (table_->columnWidth(c) < need)
      table_->setColumnWidth(c, need);
  }
}

bool CasesPage::editable() const {
  return ctx_.study->isOpen() && ctx_.study->holdsLock();
}

void CasesPage::updateEditable() {
  // Without the study's lock nothing can be saved: say why once, and do not
  // offer what would only fail.
  const bool can = editable();
  const bool open = ctx_.study->isOpen();
  lockNote_->setVisible(open && !can);
  if (open && !can) {
    lockNote_->setText(
        tr("⚠  Nothing can be changed in this study from here: its folder is "
           "not held by this window. Either the folder cannot be written "
           "(read-only media, a drive that is full or was unplugged), or the "
           "study is open in another window, which is where it is changed. "
           "It can still be looked at."));
    lockNote_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }")
            .arg(ctx_.theme->statusColor(QStringLiteral("serious")).name()));
  }
  for (QWidget *w : std::initializer_list<QWidget *>{
           addRef_, more_, catalogBrowse_, catalogGet_, catalogUpdate_,
           catalogTime_, setup_, waveButton_})
    w->setEnabled(can);
  if (!can) {
    add_->setEnabled(false);
    remove_->setEnabled(false);
    detail_->setEnabled(false);
  }
}

void CasesPage::saveTableState() {
  // Only what the user did: not the widths set while the table is filled.
  if (updating_ || !ctx_.study->isOpen() ||
      tableStateOf_ != ctx_.study->study().dir)
    return;
  ctx_.settings->setValue(QStringLiteral("cases/table/") +
                              studyKey(ctx_.study->study().dir),
                          table_->horizontalHeader()->saveState());
}

void CasesPage::applyFilter() {
  const QString needle = text::fold(filter_->text().trimmed());
  int shown = 0;
  for (int row = 0; row < table_->rowCount(); ++row) {
    QString all;
    for (int col = 0; col < table_->columnCount(); ++col)
      if (const QTableWidgetItem *it = table_->item(row, col))
        all += it->text() + QLatin1Char(' ') + it->toolTip() +
               QLatin1Char(' ');
    const bool match = needle.isEmpty() || text::fold(all).contains(needle);
    table_->setRowHidden(row, !match);
    shown += match ? 1 : 0;
  }
  if (!needle.isEmpty() && ctx_.study->isOpen())
    summary_->setText(tr("%1 of %2 cases shown")
                          .arg(shown)
                          .arg(ctx_.study->study().cases.size()));
  else if (ctx_.study->isOpen()) {
    int rows = 0;
    for (const Case &c : ctx_.study->study().cases)
      rows += int(c.stations.size());
    summary_->setText(tr("%1 cases · %2 station rows")
                          .arg(ctx_.study->study().cases.size())
                          .arg(rows));
  }
}

void CasesPage::showCase() {
  if (updating_ || !ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const Case *c = s.findCase(currentId());
  const int chosen = int(selectedIds().size());
  detail_->setEnabled(c != nullptr && editable());
  remove_->setEnabled(chosen > 0 && editable());
  name_->setPlaceholderText(
      chosen > 1 ? tr("%n cases selected: More has what can be done with "
                      "them together",
                      nullptr, chosen)
                 : QString());
  if (!c) {
    updating_ = true;
    name_->clear();
    facts_->clear();
    map_->setStudy(s, {}); // every case, none selected
    stationTable_->setRowCount(0);
    stationHint_->clear();
    updating_ = false;
    updateStationButtons();
    return;
  }
  const QString keepStation = selectedStation();
  updating_ = true;
  name_->setText(c->name);
  const QString when =
      c->time.toString(QStringLiteral("yyyy-MM-dd HH:mm")) + tr(" UTC");
  const QString where = QStringLiteral("%1°N, %2°E")
                            .arg(c->lat, 0, 'f', 3)
                            .arg(c->lon, 0, 'f', 3);
  facts_->setText(
      c->reference ? tr("Reference date · %1 · %2").arg(when, where)
                   : tr("M %1 · %2 · depth %3 km · %4%5")
                         .arg(c->magnitude, 0, 'f', 1)
                         .arg(when)
                         .arg(c->depthKm, 0, 'f', 0)
                         .arg(where, c->place.isEmpty()
                                         ? QString()
                                         : QStringLiteral(" · ") + c->place));
  // Candidates: the nearest stations, and whatever the case already uses.
  // The case's own stations come first.
  candidates_ = c->stations;
  QStringList ticked;
  for (const study::StationRef &st : c->stations)
    ticked << st.code;
  for (const study::StationRef &o : study::nearestStations(
           stationList(), c->lat, c->lon, defaults::kStationRadiusKm))
    if (!ticked.contains(o.code))
      candidates_ << o;
  const QDateTime from = c->spanStart(), to = c->time;
  // How much of the span each has, for the bar and for ordering by it.
  const auto index = study::WaveIndex::shared(s.dir);
  QHash<QString, double> share;
  for (const study::StationRef &st : std::as_const(candidates_)) {
    double f = -1.0; // nothing to draw: an own folder, or nothing known
    if (st.dataDir.isEmpty() && index->hasStation(st.code)) {
      const study::SpanData d = study::spanData(*index, st.code, from, to);
      f = d.spanDays > 0 ? double(d.days) / double(d.spanDays) : 0.0;
    }
    share.insert(st.code, f);
  }
  if (stationSort_ == 2)
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const study::StationRef &a, const study::StationRef &b) {
                       return a.distanceKm < b.distanceKm;
                     });
  else if (stationSort_ == 4)
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [&share](const study::StationRef &a,
                              const study::StationRef &b) {
                       return share.value(a.code) > share.value(b.code);
                     });
  fillStationTable(stationTable_, candidates_, ticked,
                   [&s, from, to](const study::StationRef &st) {
                     return waveformCell(s, st, from, to);
                   });
  for (int i = 0; i < stationTable_->rowCount(); ++i)
    if (QTableWidgetItem *cell = stationTable_->item(i, 4)) {
      const double f = share.value(candidates_[i].code, -1.0);
      if (f >= 0)
        cell->setData(kCoverageRole, f);
      cell->setToolTip(cell->text());
    }
  stationTable_->horizontalHeader()->setSortIndicatorShown(stationSort_ != 0);
  if (stationSort_ != 0)
    stationTable_->horizontalHeader()->setSortIndicator(
        stationSort_,
        stationSort_ == 2 ? Qt::AscendingOrder : Qt::DescendingOrder);
  stationTable_->resizeColumnToContents(4);
  for (int i = 0; i < stationTable_->rowCount(); ++i)
    if (stationTable_->item(i, 1)->text() == keepStation) {
      const QSignalBlocker block(stationTable_);
      stationTable_->selectRow(i);
    }
  int without = 0, outside = 0;
  for (const study::StationRef &st : c->stations) {
    if (!hasWaveformsOf(s, st))
      ++without;
    else if (!hasWaveformsIn(s, st, from, to))
      ++outside;
  }
  QStringList hints;
  if (c->stations.isEmpty())
    hints << tr("⚠ No station is ticked: only the Indicators recipes run for "
                "this case. Tick the stations it is studied from.");
  if (without > 0)
    hints << tr("⚠ %n ticked station(s) have no waveforms in the study's "
                "folders. Add the folder under Waveforms…, or get the data.",
                nullptr, without);
  if (outside > 0)
    hints << tr("⚠ %n ticked station(s) have waveforms, but none in this "
                "case's span: a run on them cannot start. Untick them, or "
                "get their data for the span.",
                nullptr, outside);
  stationHint_->setText(hints.join(QLatin1Char('\n')));
  stationHint_->setVisible(!stationHint_->text().isEmpty());
  zone_->setCurrentIndex(c->zone.kind == study::Zone::Kind::Fault ? 1 : 0);
  radius_->setValue(c->zone.kind == study::Zone::Kind::Fault
                        ? c->zone.bufferKm
                        : c->zone.radiusKm);
  span_->setValue(c->spanDays);
  CaseMapSpec spec;
  spec.selectedId = c->id;
  spec.candidates = candidates_;
  spec.hasData = [&s](const study::StationRef &st) {
    return hasWaveformsOf(s, st);
  };
  map_->setStudy(s, spec);
  updating_ = false;
  updateStationButtons();
  updateEditable();
}

QString CasesPage::selectedStation() const {
  const int row = stationTable_->currentRow();
  if (row < 0 || stationTable_->selectedItems().isEmpty() ||
      !stationTable_->item(row, 1))
    return {};
  return stationTable_->item(row, 1)->text();
}

void CasesPage::updateStationButtons() {
  const Case *c = ctx_.study->isOpen()
                      ? ctx_.study->study().findCase(currentId())
                      : nullptr;
  const QString code = selectedStation();
  const study::StationRef *st = nullptr;
  if (c)
    for (const study::StationRef &x : c->stations)
      if (x.code == code)
        st = &x;
  folder_->setEnabled(st != nullptr);
  folderClear_->setEnabled(st && !st->dataDir.isEmpty());
  getWave_->setEnabled(c && !c->stations.isEmpty() && ctx_.nav);
  next_->setEnabled(c && ctx_.nav);
  tickNearest_->setEnabled(c != nullptr);
  untickAll_->setEnabled(c && !c->stations.isEmpty());
  // The next step follows the sidebar: Data (2) while a ticked station has
  // no waveforms, Runs (3) once they all have.
  bool lacking = false;
  if (c)
    for (const study::StationRef &x : c->stations)
      lacking = lacking || !hasWaveformsIn(ctx_.study->study(), x,
                                           c->spanStart(), c->time);
  getWave_->setText(lacking ? tr("Next: get waveforms →")
                            : tr("Get more waveforms…"));
  next_->setText(lacking || !c ? tr("Go on to recipes →")
                               : tr("Next: recipes →"));
  ui::setPrimary(getWave_, lacking);
  ui::setPrimary(next_, c && !lacking);
}

void CasesPage::rescanIfChanged() {
  const Study &s = ctx_.study->study();
  if (s.waveRoots.isEmpty() || scan_->running())
    return;
  // What the folders looked like when they were last scanned is remembered
  // between sessions, so opening the application does not scan for nothing.
  const QString key =
      QStringLiteral("waveforms/scanned/") +
      QString::fromLatin1(QCryptographicHash::hash(s.dir.toUtf8(),
                                                   QCryptographicHash::Sha1)
                              .toHex());
  QString stamp;
  for (const study::WaveRoot &r : s.waveRoots)
    stamp += r.id + QLatin1Char('=') +
             study::folderStamp(s.resolved(r.path)) + QLatin1Char(';');
  const auto index = study::WaveIndex::shared(s.dir);
  bool missing = false;
  for (const study::WaveRoot &r : s.waveRoots)
    missing = missing || !index->roots.contains(r.id);
  // An exact scan that was cut short (the application was closed while it
  // ran) is taken up again: once per study in a session, and not against a
  // Stop pressed in this one.
  const bool resume = !missing && index->pendingCoverage() > 0 &&
                      !scan_->stoppedByUser() && !resumed_.contains(s.dir);
  if (!missing && ctx_.settings->value(key).toString() == stamp && !resume)
    return;
  resumed_.insert(s.dir);
  ctx_.settings->setValue(key, stamp);
  scan_->start();
}

void CasesPage::updateWaveSummary() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  QString text;
  if (s.waveRoots.isEmpty()) {
    text = tr("No waveform folder yet. Add the folder that holds your "
              "miniSEED files, or download with the Data page.");
  } else {
    const auto index = study::WaveIndex::shared(s.dir);
    int lost = 0;
    for (const study::WaveRoot &r : s.waveRoots)
      if (!QDir(s.resolved(r.path)).exists())
        ++lost;
    text = tr("%n folder(s)", nullptr, int(s.waveRoots.size())) +
           QStringLiteral(" · ") +
           tr("%n station(s) found", nullptr, int(index->stations().size()));
    if (lost > 0)
      text += QStringLiteral(" · ") +
              tr("⚠ %n folder(s) not found (moved?)", nullptr, lost);
    const int skipped = int(index->skipped().size());
    if (skipped > 0)
      text += QStringLiteral(" · ") + tr("%n file(s) skipped", nullptr, skipped);
    if (!scan_->status().isEmpty())
      text += QStringLiteral(" · ") + scan_->status();
    else if (index->pendingCoverage() > 0)
      text += QStringLiteral(" · ") +
              (index->estimatedFiles() == index->pendingCoverage()
                   ? tr("coverage estimated from the file names; not read "
                        "exactly yet")
                   : tr("coverage not read yet"));
  }
  waveSummary_->setText(text);
  scanJob_->setVisible(scan_->running() && ctx_.nav);
}

void CasesPage::showBanner(const QString &text) {
  banner_->setStyleSheet(
      QStringLiteral("QLabel { background: %1; color: %2; border: 1px solid "
                     "%3; border-radius: 6px; padding: 6px 10px; }")
          .arg(ctx_.theme->color(QStringLiteral("surface2")).name(),
               ctx_.theme->color(QStringLiteral("text")).name(),
               // A problem is framed as one; a plain note is not.
               (text.contains(QStringLiteral("⚠"))
                    ? ctx_.theme->statusColor(QStringLiteral("serious"))
                    : ctx_.theme->color(QStringLiteral("accent")))
                   .name()));
  banner_->setText(text);
  banner_->setVisible(!text.isEmpty());
}

bool CasesPage::apply(const std::function<void(Study &)> &edit) {
  QString error;
  if (ctx_.study->update(edit, &error)) {
    if (banner_->property("saveError").toBool()) {
      banner_->setProperty("saveError", false);
      showBanner(QString());
    }
    reload_->hide();
    return true;
  }
  banner_->setProperty("saveError", true);
  const bool elsewhere = ctx_.study->changedOnDisk();
  showBanner(elsewhere
                 ? tr("⚠ The study file was changed on disk since this window "
                      "read it (by another window, or by hand), so the change "
                      "was not saved over it. Reload to see the study as it "
                      "is now, then make the change again.")
                 : tr("⚠ The study could not be saved, so the change was not "
                      "kept: %1")
                       .arg(error));
  reload_->setVisible(elsewhere);
  refresh(); // back to what this window knows
  return false;
}

void CasesPage::saveCase() {
  if (updating_ || !ctx_.study->isOpen())
    return;
  const QString id = currentId();
  if (id.isEmpty())
    return;
  const QString name = name_->text().trimmed();
  const QList<study::StationRef> stations =
      tickedStations(stationTable_, candidates_);
  const bool fault = zone_->currentIndex() == 1;
  const double km = radius_->value();
  const int span = span_->value();
  const auto edit = [&](Study &s) {
    for (Case &c : s.cases) {
      if (c.id != id)
        continue;
      if (!name.isEmpty())
        c.name = name;
      // A ticked station keeps what the case already knew about it (its
      // data folder).
      QList<study::StationRef> next;
      for (study::StationRef st : stations) {
        for (const study::StationRef &old : c.stations)
          if (old.code == st.code)
            st.dataDir = old.dataDir;
        next << st;
      }
      c.stations = next;
      c.zone.kind =
          fault ? study::Zone::Kind::Fault : study::Zone::Kind::Circle;
      (fault ? c.zone.bufferKm : c.zone.radiusKm) = km;
      c.spanDays = span;
    }
  };
  // Nothing changed (a field lost focus): nothing to save or to ask.
  Study after = ctx_.study->study();
  edit(after);
  if (after.toToml() == ctx_.study->study().toToml())
    return;
  const int stale = runsMadeStale(ctx_.study->study(), after, id);
  if (stale > 0 &&
      !confirm(this, tr("Change the case"),
               tr("This change makes %n finished run(s) of this case stale; "
                  "they would have to be run again. Change it anyway?",
                  nullptr, stale))) {
    showCase(); // put the fields back
    return;
  }
  apply(edit);
}

void CasesPage::addFromCatalog() { addNear({}); }

void CasesPage::addNear(const QStringList &stations) {
  try {
    // A large catalog takes a moment to read: say so while it does.
    showBanner(tr("Reading the earthquake catalog…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    struct Restore {
      ~Restore() { QApplication::restoreOverrideCursor(); }
    };
    std::unique_ptr<Restore> busy = std::make_unique<Restore>();
    AddFromCatalogDialog dialog(ctx_.study->study(), this, stations,
                                ctx_.settings);
    busy.reset();
    showBanner(QString());
    // "Add and keep open" adds through the page while the dialog stays.
    dialog.setAdder(
        [this](const QList<Case> &cases) { return addCases(cases); });
    if (dialog.exec() != QDialog::Accepted || dialog.results().isEmpty())
      return;
    addCases(dialog.results());
  } catch (const std::exception &e) {
    showBanner(tr("⚠ The earthquake catalog could not be read: %1")
                   .arg(QString::fromUtf8(e.what())));
  }
}

void CasesPage::addReference() {
  ReferenceDateDialog dialog(ctx_.study->study(), this);
  if (dialog.exec() != QDialog::Accepted || !dialog.result())
    return;
  Case c = *dialog.result();
  QString id;
  if (!apply([&](Study &s) {
        c.id = s.freeCaseId(c.name);
        id = c.id;
        s.cases << c;
      }))
    return;
  for (int i = 0; i < table_->rowCount(); ++i)
    if (table_->item(i, 0)->data(Qt::UserRole).toString() == id)
      table_->selectRow(i);
}

bool CasesPage::addCases(const QList<Case> &cases) {
  if (cases.isEmpty() || !ctx_.study->isOpen())
    return false;
  QStringList ids, replaced;
  const auto edit = [&](Study &s) {
    ids.clear();
    replaced.clear();
    for (Case c : cases) {
      Case *old = nullptr;
      for (Case &x : s.cases)
        if (!c.id.isEmpty() && x.id == c.id)
          old = &x;
      if (old) {
        // The same case with a new name, span and stations: its folder,
        // its zone and what its stations knew (their own folders) stay.
        for (study::StationRef &st : c.stations)
          for (const study::StationRef &was : old->stations)
            if (was.code == st.code)
              st.dataDir = was.dataDir;
        old->name = c.name;
        old->spanDays = c.spanDays;
        old->stations = c.stations;
        replaced << old->id;
        ids << old->id;
      } else {
        c.id = s.freeCaseId(c.name);
        ids << c.id;
        s.cases << c;
      }
    }
  };
  Study after = ctx_.study->study();
  edit(after);
  int stale = 0;
  for (const QString &id : std::as_const(replaced))
    stale += runsMadeStale(ctx_.study->study(), after, id);
  if (stale > 0 &&
      !confirm(this, tr("Replace the cases"),
               tr("Replacing makes %n finished run(s) of these cases stale; "
                  "they would have to be run again. Replace them anyway?",
                  nullptr, stale)))
    return false;
  if (!apply(edit))
    return false;
  // The new ones selected, the last of them shown.
  table_->clearSelection();
  for (int row = 0; row < table_->rowCount(); ++row)
    if (ids.contains(table_->item(row, 0)->data(Qt::UserRole).toString())) {
      table_->selectionModel()->select(
          table_->model()->index(row, 0),
          QItemSelectionModel::Select | QItemSelectionModel::Rows);
      table_->scrollToItem(table_->item(row, 0));
    }
  return true;
}

int CasesPage::staleAfter(const std::function<void(Study &)> &edit,
                          const QStringList &ids) const {
  Study after = ctx_.study->study();
  edit(after);
  int stale = 0;
  for (const QString &id : ids)
    stale += runsMadeStale(ctx_.study->study(), after, id);
  return stale;
}

void CasesPage::removeCase() {
  if (!ctx_.study->isOpen())
    return;
  const QStringList ids = selectedIds();
  if (ids.isEmpty())
    return;
  const Study &now = ctx_.study->study();
  QStringList names;
  int finished = 0;
  for (const QString &id : ids) {
    const Case *c = now.findCase(id);
    if (!c)
      continue;
    names << c->name;
    QStringList rows;
    for (const study::StationRef &st : c->stations)
      rows << st.code;
    if (rows.isEmpty())
      rows << QString();
    for (const study::Recipe &r : now.recipes)
      for (const QString &station : std::as_const(rows))
        finished += study::cellOf(now, *c, station, r).state ==
                            study::CellState::Done
                        ? 1
                        : 0;
  }
  if (!confirm(this, tr("Remove cases"),
               tr("Remove %1 from the study?").arg(names.join(QStringLiteral(", "))) +
                   QStringLiteral("\n\n") +
                   (finished > 0
                        ? tr("%n finished run(s) of them leave the tables. "
                             "Their folders stay on disk.",
                             nullptr, finished)
                        : tr("Their folders, if any, stay on disk.")) +
                   QLatin1Char(' ') +
                   tr("Until the application is closed, More ▸ Put back "
                      "brings a removed case back with its runs.")))
    return;
  QList<Removed> gone;
  for (int i = 0; i < int(now.cases.size()); ++i)
    if (ids.contains(now.cases[i].id))
      gone.prepend(Removed{now.cases[i], i});
  if (!apply([&ids](Study &s) {
        s.cases.removeIf([&ids](const Case &x) { return ids.contains(x.id); });
      }))
    return;
  removed_ = gone + removed_;
  showBanner(tr("Removed: %1. To bring a case back with its runs: More ▸ "
                "Put back.")
                 .arg(names.join(QStringLiteral(", "))));
}

QStringList CasesPage::removedCases() const {
  QStringList names;
  for (const Removed &r : removed_)
    names << r.c.name;
  return names;
}

bool CasesPage::restoreRemoved(int index) {
  if (index < 0 || index >= int(removed_.size()) || !ctx_.study->isOpen())
    return false;
  const Removed back = removed_[index];
  QString id;
  if (!apply([&](Study &s) {
        Case c = back.c;
        // Its own id again, so its folder and runs are its own again; only
        // when another case took the id meanwhile does it get a new one.
        if (s.findCase(c.id))
          c.id = s.freeCaseId(c.name);
        id = c.id;
        s.cases.insert(qBound(0, back.position, int(s.cases.size())), c);
      }))
    return false;
  removed_.removeAt(index);
  showBanner(QString());
  for (int row = 0; row < table_->rowCount(); ++row)
    if (table_->item(row, 0)->data(Qt::UserRole).toString() == id)
      table_->selectRow(row);
  return true;
}

bool CasesPage::setSpanOf(const QStringList &ids, int days) {
  if (ids.isEmpty() || days < 1 || !ctx_.study->isOpen())
    return false;
  const auto edit = [&](Study &s) {
    for (Case &c : s.cases)
      if (ids.contains(c.id))
        c.spanDays = days;
  };
  const int stale = staleAfter(edit, ids);
  if (stale > 0 &&
      !confirm(this, tr("Change the span"),
               tr("This change makes %n finished run(s) of the selected "
                  "cases stale; they would have to be run again. Change it "
                  "anyway?",
                  nullptr, stale)))
    return false;
  return apply(edit);
}

bool CasesPage::applyStationRule(const QStringList &ids, int n, double km) {
  if (ids.isEmpty() || !ctx_.study->isOpen())
    return false;
  const std::vector<analysis::Station> list = stationList();
  const auto edit = [&](Study &s) {
    for (Case &c : s.cases) {
      if (!ids.contains(c.id))
        continue;
      const QList<study::StationRef> near =
          study::nearestStations(list, c.lat, c.lon, km);
      const QStringList keep =
          stationsToStartWith(s, near, c.spanStart(), c.time, n);
      QList<study::StationRef> next;
      for (study::StationRef st : near) {
        if (!keep.contains(st.code))
          continue;
        for (const study::StationRef &was : std::as_const(c.stations))
          if (was.code == st.code)
            st.dataDir = was.dataDir;
        next << st;
      }
      c.stations = next;
    }
  };
  const int stale = staleAfter(edit, ids);
  if (stale > 0 &&
      !confirm(this, tr("Change the stations"),
               tr("This change makes %n finished run(s) of the selected "
                  "cases stale; they would have to be run again. Change it "
                  "anyway?",
                  nullptr, stale)))
    return false;
  return apply(edit);
}

QString CasesPage::duplicateCase(const QString &id) {
  const Case *src =
      ctx_.study->isOpen() ? ctx_.study->study().findCase(id) : nullptr;
  if (!src)
    return {};
  Case copy = *src;
  copy.name = tr("%1 (copy)").arg(src->name);
  QString newId;
  if (!apply([&](Study &s) {
        copy.id = s.freeCaseId(copy.name);
        newId = copy.id;
        int at = int(s.cases.size());
        for (int i = 0; i < int(s.cases.size()); ++i)
          if (s.cases[i].id == id)
            at = i + 1;
        s.cases.insert(at, copy);
      }))
    return {};
  for (int row = 0; row < table_->rowCount(); ++row)
    if (table_->item(row, 0)->data(Qt::UserRole).toString() == newId)
      table_->selectRow(row);
  return newId;
}

void CasesPage::tickNearest() {
  const Case *c = ctx_.study->isOpen()
                      ? ctx_.study->study().findCase(currentId())
                      : nullptr;
  if (!c)
    return;
  // The nearest three with data in the span, among those not ticked yet.
  QList<study::StationRef> open;
  QList<study::StationRef> byDistance = candidates_;
  std::stable_sort(byDistance.begin(), byDistance.end(),
                   [](const study::StationRef &a, const study::StationRef &b) {
                     return a.distanceKm < b.distanceKm;
                   });
  for (const study::StationRef &st : std::as_const(byDistance))
    if (std::none_of(c->stations.begin(), c->stations.end(),
                     [&st](const study::StationRef &x) {
                       return x.code == st.code;
                     }))
      open << st;
  QStringList add;
  for (const study::StationRef &st : std::as_const(open))
    if (add.size() < 3 &&
        hasWaveformsIn(ctx_.study->study(), st, c->spanStart(), c->time))
      add << st.code;
  if (add.isEmpty()) {
    showBanner(tr("No further station near this case has waveforms in its "
                  "span. Get the data first (Next: get waveforms), or add "
                  "the folder under Waveforms…."));
    return;
  }
  {
    const QSignalBlocker block(stationTable_);
    for (int i = 0; i < stationTable_->rowCount(); ++i)
      if (QTableWidgetItem *use = stationTable_->item(i, 0))
        if (add.contains(use->data(Qt::UserRole).toString()))
          use->setCheckState(Qt::Checked);
  }
  saveCase();
}

void CasesPage::untickAll() {
  {
    const QSignalBlocker block(stationTable_);
    for (int i = 0; i < stationTable_->rowCount(); ++i)
      if (QTableWidgetItem *use = stationTable_->item(i, 0))
        use->setCheckState(Qt::Unchecked);
  }
  saveCase();
}

void CasesPage::fillMenu(QMenu *menu) {
  menu->clear();
  const QStringList ids = selectedIds();
  const int n = int(ids.size());
  QAction *dup = menu->addAction(tr("Duplicate"));
  dup->setEnabled(n == 1);
  connect(dup, &QAction::triggered, this,
          [this, ids] { duplicateCase(ids.value(0)); });
  QAction *span = menu->addAction(tr("Change the span…"));
  span->setEnabled(n > 0);
  connect(span, &QAction::triggered, this, [this, ids] {
    const Case *first = ctx_.study->study().findCase(ids.value(0));
    bool ok = false;
    const int days = QInputDialog::getInt(
        this, tr("Change the span"),
        tr("Days before the event, for the %n selected case(s):", nullptr,
           int(ids.size())),
        first ? first->spanDays : defaults::kSpanDays, 1, 3650, 1, &ok);
    if (ok)
      setSpanOf(ids, days);
  });
  QAction *rule = menu->addAction(tr("Choose the stations by a rule…"));
  rule->setEnabled(n > 0);
  connect(rule, &QAction::triggered, this, [this, ids] {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Choose the stations by a rule"));
    auto *count = new QSpinBox(&dialog);
    count->setRange(0, 20);
    count->setValue(3);
    auto *km = new QDoubleSpinBox(&dialog);
    km->setRange(10, 1000);
    km->setDecimals(0);
    km->setValue(defaults::kStationRadiusKm);
    km->setSuffix(tr(" km"));
    auto *form = new QFormLayout;
    form->addRow(tr("Nearest stations with data in the span"), count);
    form->addRow(tr("Within"), km);
    auto *note = new QLabel(
        tr("Each of the %n selected case(s) gets the stations nearest to "
           "it that have waveforms in its own span; when none has, the "
           "nearest whose data is still to be got. What is ticked now is "
           "replaced.",
           nullptr, int(ids.size())),
        &dialog);
    note->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(buttons);
    ui::setHelp(count, tr("How many stations each case gets."));
    ui::setHelp(km, tr("Only stations within this distance of the case's "
                       "epicentre are considered."));
    ui::polishPage(&dialog);
    if (dialog.exec() == QDialog::Accepted)
      applyStationRule(ids, count->value(), km->value());
  });
  QAction *get = menu->addAction(
      tr("Get waveforms for the %n selected case(s)…", nullptr, std::max(n, 1)));
  get->setEnabled(n > 0 && ctx_.nav);
  connect(get, &QAction::triggered, this, [this, ids] { getWaveformsFor(ids); });
  menu->addSeparator();
  QAction *disk = menu->addAction(tr("The study on disk…"));
  connect(disk, &QAction::triggered, this,
          [this] { showStudyOnDisk(ctx_, this); });
  QMenu *back = menu->addMenu(tr("Put back a removed case"));
  back->setEnabled(!removed_.isEmpty());
  for (int i = 0; i < int(removed_.size()); ++i) {
    QAction *a = back->addAction(removed_[i].c.name);
    connect(a, &QAction::triggered, this, [this, i] { restoreRemoved(i); });
  }
}

namespace {
// A study folder, or its file: the window opens those, wherever they are
// dropped.
bool isStudy(const QString &path) {
  const QFileInfo fi(path);
  return fi.isDir() ? QFileInfo::exists(QDir(path).filePath(Study::kFileName))
                    : fi.fileName() == Study::kFileName;
}
} // namespace

void CasesPage::dragEnterEvent(QDragEnterEvent *e) {
  e->ignore(); // unless taken below, it is the window's
  if (!e->mimeData()->hasUrls())
    return;
  bool any = false;
  for (const QUrl &u : e->mimeData()->urls()) {
    if (!u.isLocalFile())
      continue;
    if (isStudy(u.toLocalFile()))
      return; // a study: not an input of this one
    any = true;
  }
  if (any && ctx_.study->isOpen())
    e->acceptProposedAction();
}

void CasesPage::dropEvent(QDropEvent *e) {
  QStringList paths;
  for (const QUrl &u : e->mimeData()->urls()) {
    if (!u.isLocalFile())
      continue;
    if (isStudy(u.toLocalFile())) {
      e->ignore();
      return;
    }
    paths << u.toLocalFile();
  }
  if (paths.isEmpty())
    return;
  e->acceptProposedAction();
  handleDrop(paths);
}

void CasesPage::handleDrop(const QStringList &paths) {
  if (!ctx_.study->isOpen())
    return;
  if (!editable()) {
    showBanner(tr("⚠ Nothing was taken: this study cannot be changed from "
                  "this window."));
    return;
  }
  QStringList folders, catalogs, xml, other;
  for (const QString &path : paths) {
    const QFileInfo fi(path);
    const QString name = fi.fileName().toLower();
    if (fi.isDir())
      folders << fi.absoluteFilePath();
    else if (name.endsWith(QLatin1String(".xml")))
      xml << fi.absoluteFilePath();
    else if (name.endsWith(QLatin1String(".csv")) ||
             name.endsWith(QLatin1String(".csv.gz")) ||
             name.endsWith(QLatin1String(".gz")) ||
             name.endsWith(QLatin1String(".txt")))
      catalogs << fi.absoluteFilePath();
    else
      other << fi.fileName();
  }
  QStringList said;
  if (!catalogs.isEmpty()) {
    const QString path = catalogs.first();
    const double offset = ctx_.study->study().catalogUtcOffsetHours;
    study::CatalogSummary cs;
    try {
      cs = study::catalogSummary(path, offset);
    } catch (const std::exception &) {
      cs.ok = false;
    }
    if (!cs.ok || cs.events == 0) {
      said << tr("⚠ %1 could not be read as an earthquake catalog (an AFAD "
                 "export as CSV, plain or .gz).")
                  .arg(QFileInfo(path).fileName());
    } else if (confirm(
                   this, tr("Earthquake catalog"),
                   tr("Use %1 as this study's earthquake catalog?")
                           .arg(QFileInfo(path).fileName()) +
                       QStringLiteral("\n\n") +
                       tr("%1 events, %2 to %3, magnitudes from about %4.")
                           .arg(cs.events)
                           .arg(cs.first.toString(Qt::ISODate),
                                cs.last.toString(Qt::ISODate))
                           .arg(cs.floor, 0, 'f', 1) +
                       (cs.floor > defaults::kSparseCatalogMag
                            ? QLatin1Char(' ') +
                                  tr("That is sparse: enough to choose "
                                     "cases, not for the indicators or the "
                                     "calibration of the features.")
                            : QString()))) {
      if (apply([&path](Study &s) { s.catalog = s.stored(path); }))
        said << tr("The catalog is now %1.").arg(QFileInfo(path).fileName());
    }
  }
  if (!folders.isEmpty() &&
      confirm(this, tr("Waveform folders"),
              tr("Add %n folder(s) to this study's waveform folders? They "
                 "are then read for the stations and days they hold.",
                 nullptr, int(folders.size())) +
                  QStringLiteral("\n\n") + folders.join(QLatin1Char('\n')))) {
    Study::RootsAdded added;
    if (apply([&](Study &s) { added = s.addWaveRoots(folders); }))
      said << (added.added.isEmpty()
                   ? tr("The study already lists those folders.")
                   : tr("%n waveform folder(s) added; they are being read.",
                        nullptr, int(added.added.size())));
  }
  for (const QString &path : std::as_const(xml)) {
    try {
      const QList<study::ExtraStation> read = study::readStationXml(path);
      if (read.isEmpty()) {
        said << tr("⚠ %1 names no station with coordinates.")
                    .arg(QFileInfo(path).fileName());
        continue;
      }
      if (!confirm(this, tr("Station coordinates"),
                   tr("Take the coordinates of the %n station(s) in %1 into "
                      "this study? Stations the built-in list does not have "
                      "can then be ticked for a case.",
                      nullptr, int(read.size()))
                       .arg(QFileInfo(path).fileName())))
        continue;
      if (apply([&read](Study &s) {
            for (const study::ExtraStation &e : read) {
              s.extraStations.removeIf([&e](const study::ExtraStation &x) {
                return x.code == e.code;
              });
              s.extraStations << e;
            }
          }))
        said << tr("%n station(s) now have coordinates from %1.", nullptr,
                   int(read.size()))
                    .arg(QFileInfo(path).fileName());
    } catch (const std::exception &e) {
      said << QStringLiteral("⚠ ") + QString::fromUtf8(e.what());
    }
  }
  if (!other.isEmpty())
    said << tr("⚠ Not used: %1. A catalog is a .csv or .gz file, station "
               "coordinates a StationXML (.xml) file, waveforms a folder.")
                .arg(other.join(QStringLiteral(", ")));
  showBanner(said.join(QLatin1Char(' ')));
}

void CasesPage::chooseCatalog() {
  const QString path = ui::pickFile(
      this, ctx_.settings, QStringLiteral("cases/catalog"),
      tr("Earthquake catalog"), tr("AFAD catalog (*.csv *.gz);;All files (*)"),
      ctx_.study->study().dir);
  if (path.isEmpty())
    return;
  apply([&path](Study &s) { s.catalog = s.stored(path); });
}

void CasesPage::offsetChosen(int index) {
  const Study &now = ctx_.study->study();
  double hours = catalogTime_->itemData(index).toDouble();
  if (!catalogTime_->itemData(index).isValid()) {
    bool ok = false;
    hours = QInputDialog::getDouble(
        this, tr("Catalog time"),
        tr("Hours the catalog's times are ahead of UTC:"),
        now.catalogUtcOffsetHours, -12.0, 14.0, 1, &ok);
    if (!ok) {
      refresh();
      return;
    }
  }
  const double old = now.catalogUtcOffsetHours;
  if (hours == old) {
    refresh();
    return;
  }
  int quakes = 0;
  for (const Case &c : now.cases)
    quakes += c.reference ? 0 : 1;
  // With cases in the study, their times were read with the old offset:
  // they are taken from the catalog again so they stay the same events.
  QHash<QString, QDateTime> times;
  int lost = 0;
  if (quakes > 0) {
    if (!confirm(this, tr("Catalog time"),
                 tr("The times of the %n earthquake case(s) were read from "
                    "the catalog with the old setting. They are read again "
                    "with the new one, which moves them by %1 hours and "
                    "makes their finished runs stale. Change it?",
                    nullptr, quakes)
                     .arg(old - hours))) {
      refresh();
      return;
    }
    try {
      const std::vector<analysis::Event> events =
          analysis::loadEvents(now.catalogPath(), hours);
      for (const Case &c : now.cases) {
        if (c.reference)
          continue;
        const double want = double(c.time.toSecsSinceEpoch()) +
                            (old - hours) * 3600.0;
        const analysis::Event *hit = nullptr;
        for (const analysis::Event &e : events)
          if (std::abs(e.epoch - want) < 2.0 &&
              std::abs(e.lat - c.lat) < 1e-3 && std::abs(e.lon - c.lon) < 1e-3)
            hit = &e;
        if (hit)
          times.insert(c.id, study::caseFromEvent(*hit).time);
        else
          ++lost;
      }
    } catch (const std::exception &) {
      lost = quakes; // no catalog to read them from
    }
  }
  if (apply([&](Study &s) {
        s.catalogUtcOffsetHours = hours;
        for (Case &c : s.cases)
          if (times.contains(c.id))
            c.time = times.value(c.id);
      }) &&
      lost > 0)
    showBanner(tr("⚠ %n earthquake case(s) were not found in the catalog "
                  "at the shifted time and keep the time they had. Check "
                  "them against the catalog.",
                  nullptr, lost));
}

void CasesPage::openWaveforms() {
  WaveformsDialog dialog(ctx_, scan_, this);
  dialog.exec();
  updateWaveSummary();
  showCase();
  // "Find earthquakes near these…": on to picking a case near the stations
  // that were selected there.
  if (!dialog.findNear().isEmpty())
    addNear(dialog.findNear());
}

void CasesPage::openSetup() {
  StudySetupDialog dialog(ctx_.study, this);
  dialog.exec();
}

void CasesPage::getCatalog() {
  CatalogFetchDialog dialog(ctx_.study->study(), this);
  if (dialog.exec() != QDialog::Accepted)
    return;
  QPointer<CasesPage> self(this);
  if (startCatalogFetch(ctx_, dialog.query(), dialog.serviceId(),
                        [self](bool ok, const QString &text) {
                          if (self)
                            self->showBanner((ok ? QString()
                                                 : QStringLiteral("⚠ ")) +
                                             text);
                        }))
    showBanner(tr("Downloading the catalog; progress is in the Jobs panel."));
}

void CasesPage::updateBuiltIn() {
  QPointer<CasesPage> self(this);
  Job *job = ctx_.jobs->add(new TaskJob(
      tr("catalog refresh"), QStringLiteral("tdvms"),
      tr("stations from the portal, M5+ events from USGS"),
      [](TaskContext &t) -> int {
        try {
          auto http = tdvms::makeQtHttp();
          const auto [stations, events] = tdvms::refreshCatalogs(*http);
          t.log(QStringLiteral("%1: %2; %3: %4")
                    .arg(tdvms::stationsPath())
                    .arg(stations)
                    .arg(tdvms::eventsPath())
                    .arg(events));
          return 0;
        } catch (const std::exception &e) {
          t.log(QString::fromUtf8(e.what()));
          return 1;
        }
      }));
  catalogUpdate_->setEnabled(false);
  connect(job, &Job::stateChanged, this, [self, job] {
    const Job::State st = job->state();
    if (!self || st == Job::State::Running || st == Job::State::Queued)
      return;
    self->catalogUpdate_->setEnabled(true);
    if (st == Job::State::Done) {
      self->showBanner(QString());
      self->refresh();
    } else {
      self->showBanner(tr("⚠ The built-in lists could not be updated: %1")
                           .arg(job->log().isEmpty() ? QString()
                                                     : job->log().last()));
    }
  });
}

void CasesPage::chooseStationFolder() {
  const QString id = currentId();
  const QString code = selectedStation();
  if (id.isEmpty() || code.isEmpty())
    return;
  const QString path = ui::pickFolder(
      this, ctx_.settings, QStringLiteral("cases/stationFolder"),
      tr("Folder with the waveforms of %1").arg(code),
      ctx_.study->study().dir);
  if (path.isEmpty())
    return;
  apply([&](Study &s) {
    for (Case &c : s.cases)
      if (c.id == id)
        for (study::StationRef &st : c.stations)
          if (st.code == code)
            st.dataDir = s.stored(path);
  });
}

void CasesPage::clearStationFolder() {
  const QString id = currentId();
  const QString code = selectedStation();
  if (id.isEmpty() || code.isEmpty())
    return;
  apply([&](Study &s) {
    for (Case &c : s.cases)
      if (c.id == id)
        for (study::StationRef &st : c.stations)
          if (st.code == code)
            st.dataDir.clear();
  });
}

void CasesPage::getWaveforms() {
  const Case *c = ctx_.study->study().findCase(currentId());
  if (!c || !ctx_.nav)
    return;
  // What the Data page needs to plan the downloads: see DEVELOPING.
  QStringList codes;
  for (const study::StationRef &st : c->stations)
    codes << st.code;
  QVariantMap args;
  args.insert(QStringLiteral("stations"), codes);
  args.insert(QStringLiteral("start"),
              c->spanStart().toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("end"), c->time.toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("case"), c->name);
  args.insert(QStringLiteral("cases"), QStringList{c->name});
  args.insert(QStringLiteral("requests"),
              QVariantList{QVariantMap{
                  {QStringLiteral("case"), c->name},
                  {QStringLiteral("stations"), codes},
                  {QStringLiteral("start"),
                   c->spanStart().toUTC().toString(Qt::ISODate)},
                  {QStringLiteral("end"),
                   c->time.toUTC().toString(Qt::ISODate)}}});
  // Where the case is, for the Data page's map.
  args.insert(QStringLiteral("lat"), c->lat);
  args.insert(QStringLiteral("lon"), c->lon);
  args.insert(QStringLiteral("zone_km"),
              c->zone.kind == study::Zone::Kind::Circle ? c->zone.radiusKm
                                                        : 0.0);
  // The network, where the waveform folders already show it.
  for (const study::WaveStation &w :
       study::WaveIndex::shared(ctx_.study->study().dir)->stations())
    if (codes.contains(w.code) && !w.network.isEmpty()) {
      args.insert(QStringLiteral("network"), w.network);
      break;
    }
  ctx_.nav->show(QStringLiteral("tdvms"), args);
}

void CasesPage::getWaveformsFor(const QStringList &ids) {
  if (!ctx_.nav || !ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  // One case: exactly what its own button sends.
  if (ids.size() == 1 && ids.first() == currentId()) {
    getWaveforms();
    return;
  }
  QStringList codes, names;
  QDateTime start, end;
  QVariantList requests; // what each case needs: its stations over its span
  const Case *only = nullptr;
  for (const QString &id : ids) {
    const Case *c = s.findCase(id);
    if (!c)
      continue;
    only = names.isEmpty() ? c : nullptr;
    names << c->name;
    QStringList own;
    for (const study::StationRef &st : c->stations) {
      own << st.code;
      if (!codes.contains(st.code))
        codes << st.code;
    }
    if (!own.isEmpty())
      requests << QVariantMap{
          {QStringLiteral("case"), c->name},
          {QStringLiteral("stations"), own},
          {QStringLiteral("start"),
           c->spanStart().toUTC().toString(Qt::ISODate)},
          {QStringLiteral("end"), c->time.toUTC().toString(Qt::ISODate)}};
    if (!start.isValid() || c->spanStart() < start)
      start = c->spanStart();
    if (!end.isValid() || c->time > end)
      end = c->time;
  }
  if (names.isEmpty())
    return;
  if (codes.isEmpty()) {
    showBanner(tr("The selected cases have no ticked station to get "
                  "waveforms for."));
    return;
  }
  // The union of their stations over the period from the earliest span
  // start to the latest event: see DEVELOPING for the keys.
  QVariantMap args;
  args.insert(QStringLiteral("stations"), codes);
  args.insert(QStringLiteral("start"), start.toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("end"), end.toUTC().toString(Qt::ISODate));
  args.insert(QStringLiteral("case"), names.join(QStringLiteral(", ")));
  args.insert(QStringLiteral("cases"), names);
  // The whole period above is only the frame: each station is wanted for
  // the spans of the cases that tick it, no more.
  args.insert(QStringLiteral("requests"), requests);
  if (only) {
    args.insert(QStringLiteral("lat"), only->lat);
    args.insert(QStringLiteral("lon"), only->lon);
    args.insert(QStringLiteral("zone_km"),
                only->zone.kind == study::Zone::Kind::Circle
                    ? only->zone.radiusKm
                    : 0.0);
  }
  for (const study::WaveStation &w :
       study::WaveIndex::shared(s.dir)->stations())
    if (codes.contains(w.code) && !w.network.isEmpty()) {
      args.insert(QStringLiteral("network"), w.network);
      break;
    }
  ctx_.nav->show(QStringLiteral("tdvms"), args);
}

} // namespace skg
