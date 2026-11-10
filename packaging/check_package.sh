#!/usr/bin/env bash
# Fails when a package lacks what the program needs beside it at run time.
#
#   check_package.sh <dir> <ayzek executable name>
#
# <dir> is the directory that holds data/ and ayzek/ in the package:
#   Linux    <prefix>/share/sismokaos-gui
#   macOS    sismokaos-gui.app/Contents/Resources
#   Windows  the folder of sismokaos-gui.exe
# Every file must exist and be non-empty. Used by ci.yml and release.yml;
# the Windows packaging step of release.yml repeats the list in PowerShell,
# so a file added here must be added there.
set -u
dir="${1:?usage: check_package.sh <dir> <ayzek executable name>}"
ayzek="${2:?usage: check_package.sh <dir> <ayzek executable name>}"
missing=0
for f in \
    data/eafz.geojson \
    data/faults_tr.geojson \
    data/LICENSE.txt \
    data/PROVENANCE.md \
    "ayzek/$ayzek" \
    ayzek/LICENSE; do
  if [ -s "$dir/$f" ]; then
    echo "ok       $dir/$f"
  else
    echo "MISSING  $dir/$f"
    missing=1
  fi
done
if [ "$missing" -ne 0 ]; then
  echo "The package is incomplete: files the program needs at run time are missing."
  exit 1
fi
