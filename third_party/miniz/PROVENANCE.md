# miniz 3.0.2

Vendored copy of miniz, Rich Geldreich's public-domain deflate/inflate and ZIP
library. Used for reading the ZIP archives TDVMS delivers (member listing,
inflating a member to check for a no-data notice, streaming miniSEED members to
the coverage scanner).

- Source: https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  (release 3.0.2, the amalgamated `miniz.c` + `miniz.h`), fetched 2026-11-09.
- SHA-256 of the files as copied:
  - miniz.c `0fcdc9888cb3a29ca8f176bac087e5fe6c7258a6ab06b1c271c1e109a11d3740`
  - miniz.h `295d1a0041aea09609598c0f1f35c1977ca05ad662acbadcfdaac44c140af37b`
- Licence: MIT / Unlicense (see `LICENSE` in this directory).
- Local changes: none. Build options are set in `CMakeLists.txt` here
  (`MINIZ_NO_STDIO` off, time functions on).
