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
src/tdvms/     the C++ port of tdvms-pull (library, no Widgets)
src/fx/        the C++ port of the feature extraction, after sismokaos-cli (library, no Widgets)
src/analysis/  the SCAP port: catalogs, faults, labelling, figures (library, QtGui only),
               and the regions port, after earthquake-clustering-eafz: regions (SOM, Mean
               Shift), regionmap, regionrun (data/ holds the Natural Earth countries outline)
src/sfc/       the classification port: settings, dataset, split, models, tuning, run (library)
src/cbi/       the catalog-based indicators port, after indicators-forecast: zones, Mc and
               homogenisation, Gardner-Knopoff, windows, indicators, table, figures, run
               (library; data/ holds the EAFZ GeoJSON and the AFAD magnitude map)
src/ayzek/     the launcher logic for the bundled ayzek: command line, output parsing (not a port)
src/study/     studies: study.toml, cases, recipes, the open study; running a recipe on a
               case (runs.h: layout, run.json, cell states); what Compare reads (compare.h)
src/core/      Module interface, AppContext, ModuleRegistry, I18n, Theme, ui helpers
src/modules/   one directory per page, each its own static library target
src/app/       MainWindow, UpdateCheck, main()
third_party/   vendored libraries (miniz, libsvm, toml++) with a PROVENANCE.md each,
               and the XGBoost and ayzek submodules (*-PROVENANCE.md beside them)
packaging/     desktop entry and icon; ayzek/ holds the Meson driver and zig cross file;
               naturalearth/ the script that made src/analysis/data/countries.geojson
tests/         QtTest suites and their fixtures (tests/data)
translations/  sismokaos-gui_tr.ts, sismokaos-gui_en.ts
               install.sh / install.ps1 live at the root
.github/       CI on every push; the release workflow on tags
```

## Studies

`src/study` is the library, `src/modules/study` the three pages (Cases, Runs,
Compare) and the header's study selector; `AppContext::study` is the open
study. To let a study run another tool, add it in `src/study/runs.cpp`:
a tool name in `recipeTools()`, its defaults in `defaultRecipe()`, whether
it runs per station in `perStation()`, a `run<Tool>()` that writes into the
run folder and fills the `RunRecord` (inputs, outputs, fitted values), and
what blocks it in `cellOf()`. If Compare should draw it, add its table to
`seriesColumns()` and `seriesOf()` in `compare.cpp`. `tests/tst_study_runs`
runs recipes on the synthetic catalog and fabricated feature tables;
`tests/tst_study_pages` drives the pages (set `SKG_SHOTS=<dir>`, and
`SKG_SHOTS_THEME=dark`, to save a picture of each).

## Adding a page (module)

1. Create `src/modules/<name>/` with a `CMakeLists.txt` like
   `modules/placeholder/CMakeLists.txt`: a static library appended to
   `SKG_MODULE_TARGETS` and `SKG_TRANSLATED_TARGETS`. Add the directory to
   `src/modules/CMakeLists.txt`.
2. Subclass `skg::Module` (`src/core/module.h`): `id()`, `title()`,
   `description()`, `requiredTools()`, `createWidget()`. The widget gets an
   `AppContext` with the jobs model, the tool registry, the settings and the
   theme. `section()` says where it sits in the sidebar (Study, Tools, or
   Footer; Tools by default) and `iconId()` names its icon, by default
   `src/core/resources/icons/<id>.svg` (a 24x24 line drawing with
   `stroke="#000"`, recoloured at run time; a plain square when missing).
3. Register it in `src/app/main.cpp` with `modules.emplace<YourModule>()`.
   Pages appear in registration order within their section.

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
`:/i18n/`.

## Tests

Every library has a suite under `tests/` (`skg_test(<name> <sources>)` in
`tests/CMakeLists.txt`). The tdvms suites mirror the Python tests case by
case. The fx suites check the signal processing and chaos features against
values from the Python tool and sismokaos-cli (`tests/data/fx/reference.json`,
obspy-encoded miniSEED fixtures) and run the whole pipeline on a synthetic
station (`tests/data/fx/enz`). `tests/data/fx/*.json` record how the fixtures
were made. Tests run offscreen (`QT_QPA_PLATFORM=offscreen`).

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
