#!/usr/bin/env python3
"""zig as a C/C++ cross compiler for Meson, for building ayzek.

    zigwrap.py ZIG MODE TARGET ARGS...      MODE is cc, c++ or ar

ayzek needs C++23 (std::print, std::expected, std::mdspan), which the CI
compilers lack; zig carries a recent clang and libc++ and links a static
binary for every platform the GUI is released on. Meson adds two linker flags
zig's linker rejects (-Wl,--fatal-warnings, -Wl,-O1), as ayzek's own
cross/zig-* scripts note; they are dropped here. Everything else passes through.
"""
import os
import subprocess
import sys

DROP = {"-Wl,--fatal-warnings", "-Wl,-O1", "-Wl,--as-needed"}


def main() -> int:
    if len(sys.argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    zig, mode, target = sys.argv[1:4]
    args = [a for a in sys.argv[4:] if a not in DROP]
    cmd = [zig, mode]
    if mode != "ar":
        cmd += ["-target", target]
    cmd += args
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
