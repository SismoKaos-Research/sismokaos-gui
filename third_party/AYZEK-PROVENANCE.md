# ayzek (git submodule at `third_party/ayzek`)

- Source: https://github.com/hogib/ayzek, tag `v1.0.0`, "the method freeze"
  (commit 3e430642d8e37e81310160a1a057e30b88656e63, 2026-10-30), added
  2026-11-10. Nothing in the submodule is modified. Archived at Zenodo,
  DOI 10.5281/zenodo.23672495; `third_party/ayzek/CITATION.cff` says how to
  cite it.
- Licence: GPL-3.0-or-later (`third_party/ayzek/LICENSE`). ayzek is not
  linked into sismokaos-gui (also GPL-3.0-or-later since 0.3.0; MIT up to
  0.2.1): it is built as its own executable and
  shipped beside the application together with its licence, its citation
  file and its model weights (`models/`, about 4 MB), and the GUI starts it
  as a separate process. Its source travels with this repository as the
  submodule, so a release of the GUI carries ayzek's source as well.
- Used for: the Early warning tab, a launcher for ayzek's replay and live
  modes. The pipeline itself (detection, association, Pd magnitude,
  location) stays in ayzek; the method is frozen at this tag and the
  held-out protocol refers to it, so it is deliberately not ported.
- Build: `third_party/CMakeLists.txt` (option `SKG_BUNDLE_AYZEK`) drives
  Meson with zig as the compiler (`packaging/ayzek/`), because ayzek needs
  C++23 and zig carries a recent clang and libc++ for every release
  platform. The result is staged in `<build>/ayzek/`.
- Fetch after cloning: `git submodule update --init --recursive`.
- To move to a newer ayzek: `git -C third_party/ayzek fetch --tags && git -C
  third_party/ayzek checkout vX.Y.Z`, then `git add third_party/ayzek`,
  update the tag, commit and date here, and check `app/ayzek.cpp`'s options
  against `src/ayzek/launcher.cpp`.
