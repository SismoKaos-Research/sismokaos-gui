#!/usr/bin/env python3
"""Builds ayzek's executable with Meson, for CMake (third_party/CMakeLists.txt).

    build.py --meson "CMD;ARG;..." --ninja NINJA --source DIR --build DIR
             --cross FILE --exe NAME

Configures the build directory on the first call (or after the cross file
changed) and runs ninja on the one target the GUI ships, app/NAME. The
binaries are stripped at link time (-s): the symbols of libc++ alone are
tens of megabytes.
"""
import argparse
import os
import subprocess
import sys


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--meson", required=True, help="command, ';'-separated")
    p.add_argument("--ninja", required=True)
    p.add_argument("--source", required=True)
    p.add_argument("--build", required=True)
    p.add_argument("--cross", required=True)
    p.add_argument("--exe", required=True)
    a = p.parse_args()
    meson = [x for x in a.meson.split(";") if x]
    stamp = os.path.join(a.build, "cross.ini.used")
    with open(a.cross, encoding="utf-8") as f:
        cross = f.read()
    configured = os.path.exists(os.path.join(a.build, "build.ninja"))
    if configured and os.path.exists(stamp):
        with open(stamp, encoding="utf-8") as f:
            configured = f.read() == cross
    if not configured:
        cmd = meson + ["setup", a.build, a.source, "--cross-file", a.cross,
                       "--buildtype=release", "-Dcpp_link_args=-s"]
        if os.path.exists(os.path.join(a.build, "build.ninja")):
            cmd.append("--wipe")
        r = subprocess.call(cmd)
        if r:
            return r
        with open(stamp, "w", encoding="utf-8") as f:
            f.write(cross)
    return subprocess.call([a.ninja, "-C", a.build, "app/" + a.exe])


if __name__ == "__main__":
    sys.exit(main())
