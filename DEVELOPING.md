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
src/tools/     ToolDesc, ToolRegistry: descriptors and resolution of external CLIs
src/tdvms/     the C++ port of tdvms-pull (library, no Widgets); data/ holds the built-in
               station and earthquake lists (catalog.h), embedded as resources
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
               run.json, cell states); what Compare reads (compare.h)
src/studyui/   widgets the study pages and the tool pages share: the map (geomap.h),
               station coverage (coveragemap.h), the study bar of the tool pages
               (studyinputs.h)
src/core/      Module interface, AppContext, Navigator, ModuleRegistry, I18n, Theme,
               ui helpers, shared defaults (defaults.h)
src/modules/   one directory per page, each its own static library target
src/app/       MainWindow, UpdateCheck, main()
data/faults/   the two fault data files shipped beside the program, with LICENSE.txt and
               PROVENANCE.md (not Qt resources)
third_party/   vendored libraries (miniz, libsvm, toml++) with a PROVENANCE.md each,
               and the XGBoost and ayzek submodules (*-PROVENANCE.md beside them)
packaging/     desktop entry and icon; ayzek/ holds the Meson driver and zig cross file;
               naturalearth/ the script that made src/analysis/data/countries.geojson;
               efsm20/ the script that made data/faults/faults_tr.geojson;
               check_package.sh, which fails a package that lacks a data file or ayzek
tests/         QtTest suites and their fixtures (tests/data)
translations/  sismokaos-gui_tr.ts, sismokaos-gui_en.ts
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
`study.toml`: a version, then folder id, then path relative to that folder,
then the file (size, mtime, pass, channels with their spans). `scanRoot()`
brings one folder up to date for a pass (identity or coverage);
`WaveIndex::shared()` is the cached, thread-safe read; `waveFilesFor()` is
what a Features run reads for a case's span. `WaveScan`
(`modules/study/waveforms.h`) runs the two passes as jobs and saves as it
goes. The scanner reuses `tdvms::MseedScanner` and `fx::parseHeader`; do not
add a third miniSEED reader.

**Catalogs** (`study/catalogs.h`): `builtInEvents()` and `pickingEvents()`
for picking cases, `queryForStudy()` and `fetchCatalog()` for the download.
`fetchCatalog()` takes the `tdvms::Http` to use, so tests pass a fake one
(`CatalogFetchDialog` takes an `HttpFactory`). Its messages are in the
translation context `skg::study::CatalogText`.

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
  `app_version`, `started`, `finished`, `ok`, `error`, `settings`, `inputs`,
  `fitted`, `outputs`. A record whose signature does not start with the tool
  name predates the per-tool signatures and reads as stale.
- Reasons shown to the user go through `RunText::tr()` (context
  `skg::study::RunText`; `skg::study::CompareText` in `compare.cpp`) and name
  the path checked and where to put it right. `Cell::fix` (`Fix::Cases`,
  `Recipe`, `RunFirst`) drives the button beside the reason.
- `sha256Cached()` is keyed on size and mtime; use it for anything asked on
  every refresh.

**Tests.** `tests/tst_study` and `tst_study_portable` cover the study file
and moving a study; `tst_study_waveindex` the scanner and the index;
`tst_study_runs` runs recipes on the synthetic catalog, fabricated feature
tables and the fx test waveforms; `tst_study_setup` the setup dialog, the
built-in list and the download through a fake service; `tst_study_pages`,
`tst_guided` and `tst_maps` drive the pages. Hooks the tests replace:
`CasesPage::confirm` and `RunsPage::confirm` (the yes/no questions),
`pickFoldersHook` in `studysetup.h` (the folder picker),
`StudySwitcher::setupAfterCreate` and `showPage`. Compare and the coverage
dialog load on a worker thread: wait on `loading()` before asserting.

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
| `tdvms` (the Data page) | `stations` (QStringList), `start`, `end` (ISO UTC; the page adds a day to `end`, whole days are fetched), optional `network`, `case` (name shown in the bar), `lat`, `lon`, `zone_km` (drawn on the map) |
| `cases` | `case`, `station` are sent by the Runs page; the Cases page does not read them yet |
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
   of "Build from source") before configuring.

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

The release workflow refuses a tag that does not match the CMake version,
builds and tests on Linux, Windows and macOS, and publishes the AppImage,
the Windows zip, the macOS zip, `SHA256SUMS` and the install scripts. The
install scripts and the in-app update check find releases by these asset
names, so the names are part of the contract. A failed tag publishes nothing;
fix, bump the patch version and tag again.
