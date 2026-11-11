# Developing sismokaos-gui

The README is the user manual. This file is for people changing the code.

## Build from source

Needs CMake 3.21+, a C++20 compiler (GCC 12+, Clang 15+, MSVC 2022) and
Qt 6.5+ with the Widgets, Network and LinguistTools components (Test for the
tests). The build type defaults to RelWithDebInfo; a Debug build is about ten
times slower on the feature extraction, whose chaos features are O(n²) per
window.

```sh
git submodule update --init --recursive   # XGBoost and ayzek, built with the project
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/sismokaos-gui
```

With Meson, Ninja, zig and Python on the PATH the build also compiles ayzek
and stages it in `build/ayzek/` (see *ayzek* below); without them the
application builds and the Early warning tab reports that ayzek was not
found.

Formatting is LLVM style with the repository's `.clang-format`.

## Layout

```
src/jobs/      Job, ProcessJob (QProcess + progress parsing), TaskJob (worker thread),
               JobModel, JobsPanel
src/text/      decoding catalog text (UTF-8, else Windows-1254) and restoring place names an
               event service damaged (QtCore only; data/ holds the GeoNames place list)
src/tools/     ToolDesc, ToolRegistry: descriptors and resolution of external CLIs
src/tdvms/     the C++ port of tdvms-pull (library, no Widgets); data/ holds the built-in
               station and earthquake lists (catalog.h), embedded as resources; guide.h
               is what the Data page's "get waveforms for a case" panel is made of
src/fx/        the C++ port of the feature extraction, after sismokaos-cli (library, no Widgets)
src/analysis/  the SCAP port: catalogs, faults, labelling, figures (library, QtGui only),
               and the regions port, after earthquake-clustering-eafz: regions (SOM, Mean
               Shift), regionmap, regionrun (data/ holds the Natural Earth countries outline)
src/sfc/       the classification port: settings, dataset, split, models, tuning, run (library)
src/cbi/       the catalog-based indicators port, after indicators-forecast: zones, Mc and
               homogenisation, Gardner-Knopoff, windows, indicators, table, figures, run
               (library; data/ holds the AFAD magnitude map; zones.h also finds the data
               files of data/faults, see "Data files")
src/ayzek/     the launcher logic for the bundled ayzek: command line, output parsing (not a port)
src/study/     studies (library, no Widgets): study.toml, cases, recipes, the open study
               (study.h, store.h); the waveform index (waveindex.h); the built-in list and
               catalog download (catalogs.h); running a recipe on a case (runs.h: layout,
               run.json, cell states, span coverage); what ran and what a study takes on
               disk (runinfo.h); the scan cache (scancache.h); what Compare reads
               (compare.h)
src/studyui/   widgets the study pages and the tool pages share: the map (geomap.h),
               station coverage (coveragemap.h), the study bar of the tool pages
               (studyinputs.h)
src/core/      Module interface, AppContext, Navigator, WorkQueue, ModuleRegistry, I18n,
               Theme, ui helpers (ui.h: help, file dialogs, the number format), shared
               defaults (defaults.h), settings keys (appsettings.h), paths for the C
               libraries (nativepath.h), KeepAwake
src/modules/   one directory per page, each its own static library target
src/app/       the skg_shell library (MainWindow, UpdateCheck, JobNotifier, diagnostics)
               and main()
data/faults/   the two fault data files shipped beside the program, with LICENSE.txt and
               PROVENANCE.md (not Qt resources)
third_party/   vendored libraries (miniz, libsvm, toml++) with a PROVENANCE.md each,
               and the XGBoost and ayzek submodules (*-PROVENANCE.md beside them)
packaging/     desktop entry and icon; ayzek/ holds the Meson driver and zig cross file;
               naturalearth/ the script that made src/analysis/data/countries.geojson;
               geonames/ the one that made src/text/data/places.tsv;
               efsm20/ the script that made data/faults/faults_tr.geojson;
               check_package.sh, which fails a package that lacks a data file or ayzek
tests/         QtTest suites and their fixtures (tests/data)
translations/  sismokaos-gui_tr.ts, sismokaos-gui_en.ts, GLOSSARY.md (the Turkish terms)
docs/          release notes of pre-releases
               install.sh / install.ps1 live at the root
.github/       CI on every push; the release workflow on tags
```

## Studies

`src/study` is the library, `src/modules/study` the pages (Cases, Runs,
Compare), their dialogs (adding a case, study setup, catalog download,
Waveforms, station coverage) and the header's study selector;
`AppContext::study` is the open study (`StudyStore`: every edit goes through
`update()` and is saved at once; check its result).

**study.toml** is described, as it is written, at the top of
`src/study/study.h`. Points that are easy to get wrong:

- Paths go through `Study::stored()` and `Study::resolved()`. A stored path
  may be relative, also with `../`, so never use a stored path directly.
- Waveform folders are `[[waveforms]]` entries (`id`, `path`). `data_root`
  and `waveform_roots` of older files are read and turned into such entries,
  and never written. `Study::addWaveRoots()` keeps the folders from
  overlapping; `waveformRoots()` and `addWaveformRoot()` are thin wrappers
  the tool pages use; `relocate()` re-points a folder and keeps its id.
- `missingInputs()` lists what is named but not on disk; the selector's
  locate dialog calls `relocate()` with the user's answer.
- Comments and unknown keys are dropped on save.

**The waveform index** (`study/waveindex.h`) is `waveforms.index.json` beside
`study.toml`: a version (2), then folder id, then path relative to that
folder, then the file (size, mtime, pass, channels with their `spans` and,
until the exact pass has read the file, an `estimate`). `scanRoots()` brings
folders up to date for a pass (`scanRoot()` is one folder);
`WaveIndex::shared()` is the cached, thread-safe read; `chooseWaveFiles()`
is what a Features run reads for a case's span, with the files it took on an
estimate named in `uncertain` (`waveFilesFor()` is the same list alone).
`WaveScan` (`modules/study/waveforms.h`) runs the two passes as jobs and
saves as it goes. The scanner reuses `tdvms::MseedScanner` and
`fx::parseHeader`; do not add a third miniSEED reader, and do not add a
decompression library: the zips go through miniz.

- **Two passes.** `ScanPass::Identity` is the quick one: the zip directory
  and the first record of each file or member (the inflate is stopped after
  512 bytes), plus an estimate from the name when `periodInFileName()` finds
  `_ddMMyyyy_HHmmss_ddMMyyyy_HHmmss` and the first record starts inside that
  period or up to a day before it. `ScanPass::Coverage` streams every record
  header through the scanner and clears the estimate. `WaveFile::exact()`
  and `estimated()` tell them apart; anything that reads coverage must cope
  with all three states (exact, estimated, neither) the way `daysCovered()`
  and `filesFor()` do, and say which it showed.
- **Workers.** `ScanOptions::workers` files are read at once, each by its own
  thread streaming one file; the calling thread alone touches the index and
  calls the hooks (`progress` per file, `bytes` a few times a second,
  `checkpoint` every 50 files or 5 s). A stop request reaches a reader inside
  a file within `pollMs` and one chunk; an interrupted file keeps its earlier
  entry. The order is one over all folders: `firstStations` (the stations
  ticked in cases), then station by station. `defaultScanWorkers()` is
  `clamp(cores - 2, 1, 6)`; the setting is `waveforms/scanWorkers` (0 =
  automatic), read through `WaveScan::workers()`.
- **The scan cache** (`study/scancache.h`): exact results per user, one small
  JSON file per waveform file under `<cache>/path/` (keyed by real path, size
  and mtime) and `<cache>/name/` (name, size, mtime to two seconds, and the
  first record of the file, for data that moved), each written with
  `QSaveFile`, so several running applications share it without a lock.
  Every result carries `ScanCache::fingerprint()` of the file it was made
  from (the whole file up to 1 MiB, else its two ends and six blocks; the
  end of a zip is its directory with every member's checksum), and is only
  handed out for a file with the same fingerprint, so a file rewritten in
  place or a look-alike elsewhere is scanned, not served. Results by name
  are one file per fingerprint (`<key>-<print>.json`). The folders are made
  one thread at a time with retries and a rename is retried: on the
  Windows CI the first results of several workers were lost, most likely
  while they made the same missing parent folders at once. A result
  that still cannot be written is counted (`ScanStats::unfiled`, a line in
  the job log, a `qWarning`); the scan does not depend on it.
  `scanRoots()` asks it before reading a file, in either pass, and files
  what the exact pass read. It is off (`ScanCache::sharedDir()` empty) until
  `main()` sets it, so tests do not touch the user's cache; a test that
  wants it calls `ScanCache::setSharedDir()` with a temporary folder and
  clears it in `cleanup()`. With `--settings <ini>` the application keeps
  its cache in `<ini>.scan-cache/`.
- **Runs and estimates.** A Features run records the files it read
  (`waveform_files`, one `name|size|time` line each) and whether some were
  taken on an estimate (`waveforms_estimated`). `waveFilesChanged()` decides
  staleness from those: uncertain files prove nothing either way.
- **Timing a folder.** `tst_study_scan scanBenchmark` is skipped unless
  `SKG_SCAN_BENCH_DATA` (a folder, only read) and `SKG_SCAN_BENCH_OUT` (a
  scratch folder) are set; `SKG_SCAN_BENCH_MAKE=6x150` first fills an empty
  data folder with six archives of 150 MB shaped like the TDVMS ones. It
  prints the quick pass, the exact pass with one and with several workers,
  and a scan served from the cache. Use an optimised build
  (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`); the figures of a debug build say
  little about a release.

**Catalogs** (`study/catalogs.h`): `builtInEvents()` and `pickingEvents()`
for picking cases, `queryForStudy()` and `fetchCatalog()` for the download.
`fetchCatalog()` takes the `tdvms::Http` to use, so tests pass a fake one
(`CatalogFetchDialog` takes an `HttpFactory`). Its messages are in the
translation context `skg::study::CatalogText`. `catalogSummary()` says what
a catalog file holds (events, dates, and the magnitude floor: the magnitude
5% of the events lie at or below), remembered per file by size and time;
`cellOf()` and `runWarnings()` compare the floor with what a tool needs
(`defaults::kSparseCatalogMag`, `kMcFloorMargin`).

**Text** (`src/text`, QtCore only; `fx` and `tdvms` link it): `text::decode()`
reads CSV bytes as UTF-8, or as Windows-1254 when they are not valid UTF-8
(never Latin-1), and `fx::lines()` goes through it, so every catalog reader
does. `text::repairPlace()` puts back the letters the USGS service replaces
by "?" in place names, from `data/places.tsv` (GeoNames, made by
`packaging/geonames/make_places.py`): exactly one place near the earthquake
must fit. It is applied where names come in (`tdvms::loadEvents()`,
`analysis::loadEvents()`, `parseFdsnText()`) and when a study file is read
(`Study::fromToml()` repairs a case's `place` and its name; the id stays).
Everything the application writes is UTF-8.

**Runs** (`study/runs.h`). To let a study run another tool, add it in
`src/study/runs.cpp`: a tool name in `recipeTools()`, its defaults in
`defaultRecipe()`, whether it runs per station in `perStation()`, a
`run<Tool>()` that writes into the folder it is given and fills the
`RunRecord` (inputs, outputs, fitted values), its line in `runSignature()`,
and what blocks it in `cellOf()`. Give every new setting a readable name and
help in `modules/study/settinghelp.cpp` (`settingInfo(tool, key)`), which
the recipe dialog and the Runs page read. If Compare should draw it, add its
table to `seriesColumns()` and `seriesOf()` in `compare.cpp`.

- `runCell()` works in `<folder>.tmp` and puts it in the folder's place on
  success; a failure is kept as `<folder>.failed`. Never write into the run
  folder directly.
- `run.json` (`RunRecord`): `tool`, `recipe`, `case`, `station`, `revision`,
  `case_signature` (the tool's own line from `runSignature()`, starting with
  the tool name), `catalog_sha256`, `upstream` (classification: when its
  features run finished), `waveforms` (features: a stamp of the files read),
  `waveform_files` and `waveforms_estimated` (features: the files themselves,
  and whether some were chosen on an estimate), `app_version`, `started`,
  `finished`, `ok`, `error`, `error_reason` (a failure with a known remedy:
  `no_data`, `little_data`, `gappy_data`, `events`, from
  `fx::CalibrationError`), `warnings` (what the run was made under, from
  `runWarnings()`), `settings`, `inputs`, `fitted`, `outputs`. A record whose signature does not start with the tool
  name predates the per-tool signatures and reads as stale.
- Reasons shown to the user go through `RunText::tr()` (context
  `skg::study::RunText`; `skg::study::CompareText` in `compare.cpp`) and name
  the path checked and where to put it right. `Cell::fix` (`Fix::Cases`,
  `Recipe`, `RunFirst`) drives the button beside the reason, also of a
  failed cell whose `error_reason` is known. A cell that is blocked says so
  even when its last run failed.
- `Cell::warnings` is not a reason to stop: of a finished run it is what its
  record says, of a cell still to run what `runWarnings()` says now. The
  Runs page shows ⚠ and the tooltip, Compare the same beside the scores
  (`ScoreRow::warnings`, a `warnings` column in the exported table).
- A Features cell is blocked when `chooseWaveFiles()` finds no file for the
  case's span, with the station's dates from `spanData()`; `SpanData::thin()`
  (under `defaults::kMinSpanCoverage` of the span's days) is a warning.
- **Progress.** A tool that has phases reports one fraction for the whole
  run that never goes back. `fx::RunHooks::overall` gives it for the feature
  runs (`fx::RunProgress`: fraction, phase k of n, name, a line of text with
  the time the phase has left); the shares of the phases are
  `kWeightUnpack`, `kWeightCalibrate` and `kWeightExtract` in
  `fx/engine.cpp`. The archives are unpacked once per run
  (`prepareInputs()`, handed to both passes through
  `PreprocessOptions::prepared`). `sfc::runSteps()` maps the models inside a
  step into the step's share. A page shows `overall`, never the per-phase
  `progress`.
- **Kept calibrations** (`runs.h`, "Calibrations are kept"):
  `<run folder>.calibration/<key>.json`, written from the
  `fx::RunHooks::calibrated` hook; the key and its parts are built in
  `calibrationKey()` in `runs.cpp`. A setting that enters the calibration
  must be added there, or a stale calibration would be reused.
  `RunHooks::freshCalibration` forces a new one; `run.json` carries
  `calibration_key` and `calibration_reused`.
- **The queue.** The Runs page starts every cell through `enqueue()` and
  `pump()`: at most `RunsPage::atOnce()` run (setting `runs/atOnce`,
  `defaults::kRunsAtOnce`), the rest wait in `queue_` and are drawn as
  Queued (`MatrixCell::queued`, `place`, `waitsFor`). A batch adds what
  becomes runnable on each `pump()`; `dequeue()` and Stop put a cell back.
- `sha256Cached()` is keyed on size and mtime; use it for anything asked on
  every refresh.

**Tests.** `tests/tst_study` and `tst_study_portable` cover the study file
and moving a study; `tst_study_waveindex` the scanner and the index;
`tst_study_scan` the estimates, the parallel scan, a stop inside a member,
the scan cache and the scan settings;
`tst_study_runs` runs recipes on the synthetic catalog, fabricated feature
tables and the fx test waveforms; `tst_study_setup` the setup dialog, the
built-in list and the download through a fake service; `tst_study_pages`,
`tst_guided` and `tst_maps` drive the pages. Hooks the tests replace:
`CasesPage::confirm` and `RunsPage::confirm` (the yes/no questions),
`pickFoldersHook` in `studysetup.h` (the folder picker),
`StudySwitcher::setupAfterCreate` and `showPage`. Compare and the coverage
dialog load on a worker thread: wait on `loading()` before asserting.

### The lock, and a file changed on disk

`StudyStore` (`study/store.h`) holds a `QLockFile`, `.study.lock`, in the
study folder while a study is open. A second store on the same folder is
refused with who holds it; a lock left by a crash on the same computer is
taken over; one from another computer is only opened after a question.
`holdsLock()` is false for a folder that takes no lock file (read-only
media): a page must then start nothing and change nothing, and say why (the
Cases, Runs and Data pages each have a reason line for it). `update()`
refuses to save over a `study.toml` that something else wrote since it was
read; `changedOnDisk()` and `reload(QString *error)` are for the banner and
its *Reload* button.

### Waveform folders, again

- Folders are compared as the file system sees them: `study::sameFolder(a,
  b)` and `folderInside(child, parent)` (`study/study.h`) ask the file
  system for folders that exist (so letter case on a case-ignoring volume
  and links are handled) and compare resolved text otherwise. Never compare
  folder texts yourself.
- Root ids are never reused: `study.toml` keeps `waveforms_last_id`, the
  highest number ever given, because the index still holds a removed root's
  files under its id until a scan drops them.
- A root that exists but lists nothing, or cannot be read, keeps its index
  entries and is named in `ScanStats::unavailable` (an unplugged drive's
  mount point). `ScanOptions::forgetEmptyFolders` drops them; the dialog
  sets it only after asking.
- Roots can change under a running scan: `WaveScan` listens to the study and
  restarts on the new list, and the dialog calls `WaveScan::stopAndWait()`
  before it adds, re-points or removes a folder.
- File times are compared with `study::sameFileTime(stored, onDisk)`, which
  allows for the two-second times of FAT and exFAT. The scan cache follows a
  file to another drive by name, size and a content fingerprint;
  `ScanCache::identityLine(path)` gives `name|size|fingerprint` for a stamp
  that must survive a move (the fingerprint is kept in memory after the
  first read, so a repeated call costs a stat).

### Spans and coverage

`study::spanData(index, station, from, to)` (`study/runs.h`) is the one
answer to "what does the study have of this station in this span": the days
with data, whether that is exact or estimated, the station's whole range,
and the notes "none in the span" and "little of the span". The Cases page,
the Runs cells and the Data page's panel all call it; do not count days
anywhere else. `spanCalendarDays(from, to)` is the number of UTC days a span
touches (365 days from 14:00 to 14:00 touch 366), the denominator
everywhere.

### What ran, and what a study takes on disk

`study/runinfo.h` only reads run folders and the index:

- `activity(study)`: every attempt, latest first, with outcome, error and
  duration (the Runs page's *Activity* tab; the diagnostics can use it).
- `studyOnDisk(study)`, `folderBytes()`, `cleanUp(study, paths, …)`: the
  sizes per result folder, which of them are leftovers, and their removal.
  `cleanUp` refuses anything that is not a leftover of the run layout.
- `LiveRuns`: a process-wide registry of the run folders in use, so the
  clean-up can never take a folder from under a run.
- `unpackedBytes(file)`, `peakNeed(needs, atOnce)`, `freeBytesAt(dir)`: what
  the zip archives of a run unpack to, the most that is in use at once for a
  given number of runs at a time, and the free space, for the check before a
  run.

On the page side, `RunsPage::menuFor(...)` builds the context menu of a
cell, a row name or a column heading (tests call it instead of sending a
right click), `RunWords` (`modules/study/runtext.h`) words durations and
sizes, `skg::showStudyOnDisk(ctx, parent)` (`modules/study/studydisk.h`)
opens the disk dialog, and `SeriesPlot::readout` is the guide's text on the
Compare plot. The page reports how many cells wait through
`reportQueue()` into `ctx.queue` (see "Shell services").

### What a run reads, and when it is stale

- **Signatures are versioned.** `runSignature()` in `study/runs.cpp` writes
  `indicators|v2|…`, `features|v2|…` and `classification|v2|…`. A record
  without `|v2|` is stale with a reason of its own per tool (what changed in
  that tool's results). When a change alters what a tool computes, raise the
  version and word the reason; do not let old and new numbers stand as
  equally current.
- **The span is half-open**, `[span start, case time)`: `runFeatures` sets
  `fx::Config::startEpoch` and `endEpoch` (JSON `from`/`to`), and
  `preprocessDirectory` leaves out data before the first and at or after the
  second, cutting a record that straddles an end. `spanCalendarDays()`
  describes the same span.
- **One sensor.** `fx::Config::channels` names the sensor family (`HH`,
  `00.HH`); empty takes the only one, else the one with most data and a
  warning. Records are indexed as `RecordRef` (32 bytes, with a channel
  number into the run's `ChannelTable`), not with a string per record.
  `study::sensorChoices(study)` fills the recipe dialog's editable drop-down
  for the `channels` key from the index.
- **Inputs are identified by content.** The run stamp and the calibration
  key use `ScanCache::identityLine(path, onlyIfKnown)`: name, size and a
  content fingerprint, never the file's time. With `onlyIfKnown` nothing is
  read, which is how the matrix is drawn without touching a file; a file
  this session has not read yet is then taken as unchanged until a scan or
  a run reads it.
- **The mainshock** is found by `eventOfCase` (same place and magnitude, or
  the largest event within 15 h, 1° and 1 magnitude unit); failing that,
  a large event in the 15 h before the case's time cuts the span and warns.
- **Classification** takes `win_sec` from the metadata the features run
  wrote (`<stem>_run_metadata.json`), failing clearly without it, and drops
  a window that ends after the event.
- **Folders of a live run.** `runCell` registers its folders in `LiveRuns`
  for as long as it works, and refuses to run when another live process
  holds the study's lock. Staging is a fresh `.unzipped-XXXXXX` per call
  holding a lock file; `fx::stagingInUse(dir)` says whether a staging folder
  belongs to a run that is alive, and both the next run and `cleanUp` leave
  such a folder alone, as they do a finished run that waits in `.tmp` to be
  put in place (`RunHooks::renameFolder` is the test seam for a swap that
  fails).
- **Stopping and quitting.** A stop during any phase leaves nothing
  (`fx::Stopped`). `~TaskJob` waits for its thread however long; the main
  window's `waitForStoppingJobs()` shows the *Stopping…* window while it
  does, and `quitWithoutWaiting` (a hook, so tests can observe it) is what
  *Quit now* calls after ten seconds. One libsvm fit cannot be interrupted.

### Adding cases

`AddFromCatalogDialog` returns `results()`, a list of cases, and
`setAdder(fn)` lets *Add and keep open* hand them over while the dialog
stays open; it takes the settings to remember its filters per study.
`Duplicates` says what happens to an earthquake that is already a case, and
`stationsToStartWith()` is the rule that pre-ticks stations (nearest N with
data in the span, else nearest N). Text filters and sort keys go through
`text::fold()` / `text::foldedContains()` (`src/text/text.h`), which fold
case, accents and the four Turkish i's without a locale; use them for any
new search box.

### Downloads for a case

`tdvms/guide.h` holds the pieces with no widgets: `portalNetwork()` (the
source rule: a station the portal lists with an instrument goes to AFAD),
the requests for a set of days on a grid of `chunkDays`,
`studyDownloadsRoot(studyDir)` (`<study>/downloads/campaigns/waveforms`,
the layout the campaign library recognises), `RequestProgress`,
`estimateBytes()` and `retryFailed()`. `CaseDownloads`
(`modules/tdvms/casedownloads.h`) is the panel: `setCase(…)` for one case,
`setSpans(perStation)` for several (each station the days of the spans of
the cases that tick it); its `Hooks` replace the real run in tests.
`TdvmsWidget::guide()` gives the panel to a test.

- `HttpResult::ok()` now needs a 2xx status and an empty error: a connection
  that drops after the headers, or a body shorter than its Content-Length,
  carries an error. Check `ok()`, never the status alone.
- `tdvms::maskAddress()` / `maskAddresses(text)` mask mailbox and slot
  addresses; everything the Data page shows or logs goes through them.
- `tdvms::naiveUtc(text)` reads a zone-less stamp as UTC (see "Dates").
- `tdvms::kCompleteSuffix` marks the run's own "complete" note, which the
  page drops when the main window announces the end of the job anyway.

## Adding a page (module)

1. Create `src/modules/<name>/` with a `CMakeLists.txt` like
   `modules/placeholder/CMakeLists.txt`: a static library appended to
   `SKG_MODULE_TARGETS` and `SKG_TRANSLATED_TARGETS`. Add the directory to
   `src/modules/CMakeLists.txt`.
2. Subclass `skg::Module` (`src/core/module.h`): `id()`, `title()`,
   `description()`, `requiredTools()`, `createWidget()`. The widget gets an
   `AppContext` with the jobs model, the tool registry, the settings, the
   theme, the study store (`study`; it may hold no open study) and the
   navigator (`nav`, null in some tests). `section()` says where it sits in
   the sidebar (Study, Tools, or Footer; Tools by default) and `iconId()`
   names its icon, by default
   `src/core/resources/icons/<id>.svg` (a 24x24 line drawing with
   `stroke="#000"`, recoloured at run time; a plain square when missing).
3. Register it in `src/app/main.cpp` with `modules.emplace<YourModule>()`.
   Pages appear in registration order within their section. The pages of
   the Study section are numbered as steps (1 · Cases, 2 · Data, 3 · Runs,
   4 · Compare), and the main window draws the New/Open bar on them while no
   study is open, so a page need not.

The look lives in `src/core`: `theme.{h,cpp}` holds the light and dark colour
tokens and builds the palette and the style sheet from
`resources/theme.qss` (`@token` names are substituted); nothing is written
at run time. `ui.h` has the shared helpers: `ui::setPrimary(button)` for the
page's one main action, `ui::setEmptyHint(view, text)` for the hint inside
an empty table (call it from `retranslate()`), and `ui::polishPage()`, which
the main window applies to every page. Draw with `ctx.theme->color("text")`
and `seriesColors()` in custom-painted widgets so both themes work, and
repaint on `Theme::changed`.

**Hover help.** Every option a user can set carries a tooltip. Give it with
`ui::setHelp(field, tr("…"))` from `retranslate()` (or at the end of a
dialog's constructor), after the field is in its layout: the same text goes
on the field, on its label (the row label of a `QFormLayout`, also through a
holder widget or a nested row, or a `QLabel` whose buddy is the field;
`ui::buddyLabel()` makes one for a plain row) and into What's This, wrapped
as rich text. `ui::setHelp({a, b}, text)` covers the parts of one option (a
path and its "…" button). Buttons and item views take
`setToolTip(ui::helpText(tr("…")))`. Write one or two plain sentences: what
the option does, its unit, the default, what a larger value changes; take
the meaning from the code that reads the option, not from its name. Recipe
settings are explained in one table, `modules/study/settinghelp.cpp`
(readable name and help per key), used by the recipe dialog and the Runs
page. `tst_help` builds every page and dialog and fails, listing them, when
a spin box, combo box, check box, radio button or editable text field has no
tooltip or its label has none.

**Navigation.** A page sends the user to another page with
`ctx.nav->show("<module id>", args)` (`Navigator` in `core/module.h`). The
main window switches the page; a page that takes arguments connects to
`Navigator::requested` itself and looks for its own id. In use:

| target | args |
|---|---|
| `tdvms` (the Data page) | `stations` (QStringList), `start`, `end` (ISO UTC; whole UTC days are fetched), optional `network`, `case` (name shown), `lat`, `lon`, `zone_km` (drawn on the map; absent for several cases), `cases` (QStringList of names) and `requests`: a list of `{case, stations, start, end}`, one entry per case, from which each station gets the days of the spans of the cases that tick it. With a study open the page shows the panel of `CaseDownloads`; without one, the bar |
| `cases` | `case` (id) and `station` (code), sent by the Runs page and the Data page: the Cases page selects them, clearing a filter that hides the case |
| `runs`, `compare` | no arguments |
| `kNavJobsPanel` (`"@jobs"`) | opens the jobs panel instead of a page; `job` is the row to select |

**Shared defaults.** A starting value more than one page offers lives in
`core/defaults.h` (`skg::defaults`: station radius, zone radius, fault
buffer, centre, span, risk label, figure dpi, `figureLanguage()`), so two
pages cannot disagree. `ui::persistSplitter()` keeps a splitter's sizes.

**Following the study.** A tool page that can take its inputs from the open
study carries a `StudyInputsBar` (`studyui/studyinputs.h`): fill the fields
from it on `inputsChanged()`, and answer `recipeRequested()` with
`saveRecipe(settings)` if the page's settings make a recipe.

**Maps.** Use `GeoMap` (`studyui/geomap.h`) for any map: fill a `GeoContent`
with `GeoMarker`s (earthquake, reference, station with its state, dot),
zones and coverage discs, and react to a click on a marker. Marker ids are
`case:<id>`, `station:<code>` and `event:<n>`; `showBelowKm` hides detail
until the view is narrow enough. `GeoLegend` draws the key under a map.
`caseMapContent()` (`modules/study/casemap.h`) builds the content for a
study's cases; `stationCoverage()` and `CoverageMap` (`studyui/coveragemap.h`)
do the coverage. Station states have their own glyphs: never tell two things
apart by colour alone. The fault backdrop is `cbi::shippedFaults()`, labelled
with `cbi::shippedFaultsLabel()` and credited with `shippedFaultsCredit()`;
it is a hazard model's fault sources and must not be called an active fault
map.

To look at every page without touching your own settings:
`sismokaos-gui --settings /tmp/try.ini --screenshots /tmp/shots` (set
`ui/theme=dark` or `ui/language=tr` in the INI; `QT_QPA_PLATFORM=offscreen`
works).

User-visible strings go through `tr()`; widgets re-read them in
`changeEvent(QEvent::LanguageChange)`. Labels built in a loop can keep their
source string with `QT_TR_NOOP` and re-translate from it.

## Shell services

What the main window provides, so that a page does not build its own:

- **`skg_shell`** is the library of `src/app` without `main()`
  (`MainWindow`, `UpdateCheck`, `JobNotifier`, the diagnostics), so tests
  can link it (`tst_shell`, `tst_update`).
- **The work queue.** `ctx.queue` (`WorkQueue` in `core/module.h`) counts
  what waits outside the jobs table: `set(owner, waiting)`, `waiting()`,
  `changed()`. The status strip, the end-of-work notification and the quit
  question read it. A page that queues work before it becomes a job (the
  Runs page) must report its count whenever it changes, and 0 when its study
  closes or the page goes away; otherwise the notifier waits for ever.
- **Notifications.** `JobNotifier` (`app/notifier.h`) announces the end of a
  busy period (jobs plus the queue), and at once a long job that fails while
  others go on. A page does not notify by itself: runs are ordinary jobs.
  `KeepAwake` (`core/keepawake.h`) holds the computer awake while jobs run:
  `SetThreadExecutionState` on Windows, a `caffeinate` child on macOS, a
  `systemd-inhibit` child on Linux, and nothing where that is missing.
- **File dialogs.** `ui::pickFolder`, `ui::pickFile` and `ui::pickSaveFile`
  (`core/ui.h`; parent, settings, key, …) remember the last folder per key;
  `ui::dialogFolder(settings, key, …)` gives it for a dialog of your own.
  Use them for every file dialog. The tool pages still call `QFileDialog`
  directly and are to be moved over.
- **Numbers.** `ui::pinNumberFormat()` (called in `main()`) sets the default
  locale to C and resets `LC_NUMERIC`, so numbers have a decimal point in
  both languages and in every file, and the vendored libraries (XGBoost,
  libsvm) parse as they expect. A comma typed into a `QDoubleSpinBox` is
  turned into a point by an application-wide filter. Do not format a number
  for a file with a locale.
- **Qt's own translation** (`qtbase_tr.qm`, for the standard buttons) is
  embedded at build time under `:/qt-i18n/` (`src/core/CMakeLists.txt`), so
  no deploy tool has to copy it. `packaging/check_package.sh` warns when a
  build has none; `SKG_REQUIRE_QT_I18N=1` makes that a failure.
- **Diagnostics.** `app/diagnostics.h` builds the text of *Help → Copy
  diagnostics*: version, system, Qt, language, theme, data files, the size
  of the open study, recent failures. Home becomes `~`, addresses
  `<address>`, the study's place `<study>`. Anything added there must be
  scrubbed the same way. `.github/ISSUE_TEMPLATE/beta-report.md` asks for
  it.
- **Settings keys** shared by more than one file are named in
  `core/appsettings.h`.
- **The window itself**: `MainWindow::openDropped()` opens a dropped study
  folder (a page that accepts drops must let a study folder pass),
  `confirmQuit` is the hook tests use for the quit question, and the first
  steps card and the no-study bar are drawn by the window on the Study
  pages.

## Paths, dates and text: three rules

These three were each a real fault that the tests of one platform could not
see.

**Never `toLocal8Bit()` a path.** The vendored miniz decodes file names as
UTF-8 on Windows, and the C libraries want something else again, so a path
with letters outside the system's code page (`C:\Users\Oğuz\…`) could not
be opened. `core/nativepath.h` has `minizPath(path)` for every
`mz_zip_reader_init_file` and the like (UTF-8 on Windows, with the `\\?\`
form for long paths, the file system's encoding elsewhere) and
`cLibraryPath(path, &ok)` for `fopen()`-style libraries such as libsvm
(ASCII, else the short 8.3 name, else the ANSI bytes). Everything else goes
through `QFile`. `tst_paths` runs the whole chain (zip scan, unpacking, the
TDVMS archive inspection, study, index, scan cache, feature table, catalog,
model files) inside a folder named "İstasyon ğüşöç ı"; add to it when you
add a way to open a file.

**A time with no zone is UTC.** Never parse with `QDateTime::fromString` and
then `toUTC()` or `setTimeZone(UTC)`: the first reads the string in the
computer's zone, the second moves times that fall in a local daylight-saving
gap. Build the time from its date and time parts with `QTimeZone::UTC`:
`tdvms::naiveUtc()` in the download code and, in the analysis code,
`fx::parseIsoUtc()` (`fx/csv.h`; the two do the same job in libraries that do
not depend on each other). The catalog's `catalog_utc_offset_hours` is
applied once, in `analysis::loadEvents`, and every tool reads the catalog
through it: a Features run hands the calibration a UTC copy written from
those events, never the original file. Tests that touch times must pass in
any zone; the time-sensitive suites are also registered a second time with
`TZ=Europe/Istanbul` (the `<name>_istanbul` entries in
`tests/CMakeLists.txt`), since the CI machines run in UTC. The variable has
no effect on Windows, where those entries run in the system's zone.

**Text from outside.** Catalog files are decoded with `text::decode()`
(UTF-8, else Windows-1254), never Latin-1. Place names an event service
damaged ("Do?anyol") are restored with `text::repairPlace(place, lat, lon)`
from `src/text/data/places.tsv` (generator in `packaging/geonames/`,
provenance beside the file). Compare and search text with `text::fold()`. Std `toupper`/`tolower`
depend on the locale and break on the Turkish i; use Qt's.

## Porting a tool

A ported tool is a library under `src/<tool>/` with no Widgets in it and its
own QtTest suite, plus a module under `src/modules/<tool>/` that drives it.
Long work is wrapped as `TaskJob` work (`jobs/taskjob.h`): it gets a
`TaskContext` for logging, progress and cooperative cancellation and runs in
its own thread, so it may block on the network. Keep file formats compatible
with the original tool so the two can share data; say in the header comment
which original module each file ports.

Include every standard and Qt header a file uses. The CI compilers (GCC 12,
MSVC 2022, Apple Clang) and Qt 6.8 pull in fewer headers transitively than a
rolling-release desktop does, and the first release failed on exactly that.

## Translations

```sh
cmake --build build --target update_translations   # lupdate: sources -> translations/*.ts
```

Then fill the `unfinished` entries in `translations/sismokaos-gui_tr.ts`
(Qt Linguist or a text editor). The `.qm` files are built and embedded at
`:/i18n/`. Library code with no QObject declares a context with
`Q_DECLARE_TR_FUNCTIONS` (as `RunText` in `study/runs.cpp`). Keep the terms
the Turkish file already uses for the study pages ("Vaka", "Tarif", "Süre").
`lupdate` rewrites the whole file, so two branches that both run it conflict:
see "Working in parallel branches".

## Tests

Every library has a suite under `tests/` (`skg_test(<name> <sources>)` in
`tests/CMakeLists.txt`). The tdvms suites mirror the Python tests case by
case. The fx suites check the signal processing and chaos features against
values from the Python tool and sismokaos-cli (`tests/data/fx/reference.json`,
obspy-encoded miniSEED fixtures) and run the whole pipeline on a synthetic
station (`tests/data/fx/enz`). `tests/data/fx/*.json` record how the fixtures
were made. Tests run offscreen (`QT_QPA_PLATFORM=offscreen`), use synthetic
data only and never the network (a fake `Http` stands in).

`tst_help` builds every page and dialog and fails when an option has no
hover help; a new dialog must be added to it.

**Written to pass on every platform.** The suites run on Linux, Windows and
macOS in CI, and each of these has failed there once:

- Wait for a state with `QTRY_VERIFY`/`QTRY_COMPARE`; never assume a layout,
  a queued signal or a worker has finished.
- Do not assert geometry right after `show()`, or a ratio that only holds
  for one window shape; check what holds for any size and print the sizes in
  the failure message.
- Do not assert exact counts that depend on timing or on file times.
- A test must pass in any time zone (see "Paths, dates and text"); the
  time-sensitive suites have a second registration under
  `TZ=Europe/Istanbul`.
- Anything that opens a file gets a case in `tst_paths`, which works inside
  a folder with Turkish letters in its name.
- Questions a page asks are answered through a hook (`RunsPage::confirm`,
  `CasesPage::confirm`, `MainWindow::confirmQuit`, `pickFoldersHook`), and a
  menu is built through a function (`RunsPage::menuFor`), so no test waits
  on a modal dialog.
- Free space, the HTTP layer and the download run have fakes; no test
  touches the network, a real mailbox or the user's settings.

**Under load.** QtTest kills a test function after five minutes. The
training-heavy suites (`tst_sfc_*`, `tst_study_runs`) pass in seconds on a
quiet machine but can hit that limit when several builds run at once. Run
them serially and raise the limit:
`QTEST_FUNCTION_TIMEOUT=1500000 ctest --test-dir build --timeout 1400`.

**Screenshots.** `SKG_SHOTS=<dir>` (and `SKG_SHOTS_THEME=dark`) makes
`tst_study_pages`, `tst_study_setup`, `tst_maps`, `tst_help` and
`tst_analysis_regions` save a picture of what they build;
`sismokaos-gui --settings <ini> --screenshots <dir>` saves every page of the
real application. A screenshot must not show a mailbox address or a personal
folder. The Data page shows the mailbox it reads from the tdvms config
folder, whatever `--settings` says, so crop its pictures to the area you
mean or mask the address before a picture leaves your machine, and never
commit one that shows either.

## Data files

The fault files in `data/faults` are data under their own licences, not part
of the program, and are deliberately not Qt resources: `eafz.geojson` (GEM
GAF-DB, CC BY-SA 4.0), which defines the EAFZ catalog zone, and
`faults_tr.geojson` (EFSM20, CC BY 4.0), the fault layer the maps draw.
CC BY-SA material may not be relicensed under "GPL version 3 or any later
version", so the file ships beside the program as an aggregate.

- `cbi::dataFile(name)` finds one: `$SKG_DATA_DIR`, `<program dir>/data`,
  `<program dir>/../share/sismokaos-gui/data`,
  `<program dir>/../Resources/data`, then the source tree (compiled in as
  `SKG_SOURCE_DATA_DIR`, which is why a source build always finds them). A
  missing file throws with every place searched.
- They are installed to `share/sismokaos-gui/data` on Linux (root
  `CMakeLists.txt`), and copied into `Contents/Resources/data` on macOS and
  `data\` beside the executable on Windows (`src/app/CMakeLists.txt`).
- `packaging/check_package.sh <dir> <ayzek executable>` fails when a package
  lacks one of them or ayzek. CI and the release workflow run it; the Windows
  packaging step repeats the list in PowerShell, so a file added to one must
  be added to the other.
- `eafz.geojson` is pinned by its SHA-256 in `tst_cbi_core`: changing it
  changes every EAFZ indicator result.
- To regenerate the EFSM20 clip: `packaging/efsm20/clip_efsm20.py`; it writes
  the provenance record beside the output.

The built-in station and earthquake lists (`src/tdvms/data`) and the
countries outline (`src/analysis/data`) are embedded resources; each folder
has a `PROVENANCE.md`.

## Working in parallel branches

Several people (or agents) can work at once if the merges are kept trivial:

1. Each stream works on its own local branch, in its own worktree and its
   own build directory, started from the same commit of `main`.
2. Each stream owns files. A change outside them is kept small and named in
   the hand-over.
3. No stream touches `translations/*.ts`, `README.md` or `DEVELOPING.md`:
   they conflict in every branch. Strings still go through `tr()` and options
   still get `ui::setHelp()`. Translations and documentation are done in one
   pass after everything has landed.
4. The first finished branch is fast-forwarded into `main`. Every later one
   is rebased onto the new `main`, rebuilt, run through the whole `ctest`,
   and then fast-forwarded. `main` only ever moves by fast-forward.
5. A fresh worktree has empty submodules: initialise them (the first command
   of "Build from source") before configuring, or copy `third_party/` from a
   tree that has them and delete the `.git` link files inside the copy.
6. A worktree made by a tool may start at the last pushed commit, not at the
   local `main`: `git reset --hard <main's commit>` before starting.
7. Two streams that each pass alone can still fail together (two helpers of
   the same name, say). Nothing is merged on the strength of a branch's own
   test run: the rebased tree is built and tested as a whole first.
8. Four builds at once overload a laptop and tests then hit QtTest's time
   limit; see "Under load".

## ayzek

ayzek (`third_party/ayzek`, GPL-3.0-or-later, `third_party/AYZEK-PROVENANCE.md`)
is not ported and not linked: the Early warning tab starts it as a process.
It is C++23 (`std::print`, `std::expected`, `std::mdspan`), beyond GCC 12,
MSVC 2022 and Apple Clang 16 in CI, so `third_party/CMakeLists.txt` builds
it with **zig** as the compiler (a recent clang and libc++, one static
binary per target) through its own Meson build:

- `packaging/ayzek/cross.ini.in` is the Meson cross file CMake writes per
  target; `zigwrap.py` runs `zig cc`/`c++`/`ar` with the target and drops the
  two linker flags Meson adds that zig's linker rejects; `build.py` runs
  `meson setup` once and then `ninja app/ayzek`.
- Targets: `x86_64-linux-musl` (Linux), `x86_64-windows-gnu` (Windows), and
  on macOS one build per `CMAKE_OSX_ARCHITECTURES` entry joined with `lipo`
  and ad-hoc signed again. All were built on Linux with zig 0.17.1; CI pins
  that version (`mlugg/setup-zig`) and installs Meson with pip (brew on
  macOS).
- `SKG_BUNDLE_AYZEK` is ON when meson, ninja, zig and python3 are found,
  OFF otherwise (`-DSKG_BUNDLE_AYZEK=ON` fails loudly if one is missing).
  The staged `build/ayzek/` holds the executable, `models/`, `LICENSE`,
  `CITATION.cff` and the provenance note; it is installed to
  `share/sismokaos-gui/ayzek` on Linux, copied beside the executable on
  Windows and into `Contents/Resources/ayzek` on macOS (`src/app/CMakeLists.txt`).
- At run time the tool registry looks for `ayzek/<exe>` beside the
  application, in `../share/sismokaos-gui`, in `../Resources`, in
  `$SKG_BUNDLED_TOOLS_DIR` and, for a source build, in the build tree
  (`bundledToolDirs` in `src/tools/toolregistry.cpp`).

To move to a newer ayzek: check out its tag in the submodule, `git add
third_party/ayzek`, update `AYZEK-PROVENANCE.md`, and compare
`app/ayzek.cpp`'s options with `src/ayzek/launcher.cpp` and the tab's form.

## Releasing

1. Set `project(sismokaos-gui VERSION X.Y.Z ...)` in `CMakeLists.txt` and
   commit.
2. `git tag -a vX.Y.Z -m "..." && git push origin main vX.Y.Z`.

**A pre-release** is a tag with a suffix, `vX.Y.Z-beta.N`: the number must
match the CMake version, and the workflow hands the suffix to the build
(`-DSKG_VERSION_SUFFIX=-beta.N`), so the application reports
`X.Y.Z-beta.N`. It is published as a GitHub pre-release, which is never "the
latest release": the install scripts and the update check of a stable
version keep offering the last stable one. A beta is installed by its
version (`install.sh --version X.Y.Z-beta.N`), and
`UpdateCheck::isNewer` offers it a later pre-release of the same version or
its final release. Write its notes in `docs/RELEASE-NOTES-<version>.md` and
paste them into the release.

The release workflow refuses a tag that does not match the CMake version,
builds and tests on Linux, Windows and macOS, and publishes the AppImage,
the Windows zip, the macOS zip, `SHA256SUMS` and the install scripts. The
install scripts and the in-app update check find releases by these asset
names, so the names are part of the contract. A failed tag publishes nothing;
fix, bump the patch version and tag again.
