# Developing sismokaos-gui

The README is the user manual. This file is for people changing the code.

## Build from source

Needs CMake 3.21+, a C++20 compiler (GCC 12+, Clang 15+, MSVC 2022) and
Qt 6.5+ with the Widgets, Network and LinguistTools components (Test for the
tests). The build type defaults to RelWithDebInfo; a Debug build is about ten
times slower on the feature extraction, whose chaos features are O(n²) per
window.

```sh
git submodule update --init --recursive   # XGBoost, built with the project
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/sismokaos-gui
```

Formatting is LLVM style with the repository's `.clang-format`.

## Layout

```
src/jobs/      Job, ProcessJob (QProcess + progress parsing), TaskJob (worker thread),
               JobModel, JobsPanel
src/tools/     ToolDesc, ToolRegistry: descriptors and resolution of external CLIs
src/tdvms/     the C++ port of tdvms-pull (library, no Widgets)
src/fx/        the C++ port of the feature extraction, after sismokaos-cli (library, no Widgets)
src/analysis/  the SCAP port: catalogs, faults, labelling, figures (library, QtGui only)
src/sfc/       the classification port: settings, dataset, split, models, tuning, run (library)
src/core/      Module interface, AppContext, ModuleRegistry, I18n
src/modules/   one directory per tab, each its own static library target
src/app/       MainWindow, UpdateCheck, main()
third_party/   vendored libraries (miniz, libsvm, toml++) with a PROVENANCE.md each,
               and the XGBoost submodule (XGBOOST-PROVENANCE.md beside it)
tests/         QtTest suites and their fixtures (tests/data)
translations/  sismokaos-gui_tr.ts, sismokaos-gui_en.ts
packaging/     desktop entry and icon; install.sh / install.ps1 live at the root
.github/       CI on every push; the release workflow on tags
```

## Adding a tab (module)

1. Create `src/modules/<name>/` with a `CMakeLists.txt` like
   `modules/placeholder/CMakeLists.txt`: a static library appended to
   `SKG_MODULE_TARGETS` and `SKG_TRANSLATED_TARGETS`. Add the directory to
   `src/modules/CMakeLists.txt`.
2. Subclass `skg::Module` (`src/core/module.h`): `id()`, `title()`,
   `description()`, `requiredTools()`, `createWidget()`. The widget gets an
   `AppContext` with the jobs model, the tool registry and the settings.
3. Register it in `src/app/main.cpp` with `modules.emplace<YourModule>()`.
   Tabs appear in registration order.

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
