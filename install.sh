#!/bin/sh
# Installs or updates sismokaos-gui on Linux (x86_64 AppImage) and macOS
# (universal .app) from the GitHub releases. No other software is needed.
#
#   curl -fsSL https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.sh | sh
#
#   install.sh                 the latest release (a no-op when already current)
#   install.sh --version 0.2.0 that release
#   install.sh --force         reinstall the same version
#   install.sh --uninstall     remove everything the script installed
#   install.sh --wait-pid N --relaunch   used by the application's own updater
#
# Installs under $SISMOKAOS_HOME (default ~/.local/opt/sismokaos-gui on Linux,
# ~/Applications on macOS), a launcher under ~/.local/bin, a desktop entry on
# Linux, and a copy of this script as "update" beside the application. User
# data (settings, campaigns, results) is never touched.
set -eu

REPO="SismoKaos-Research/sismokaos-gui"
RAW="https://raw.githubusercontent.com/$REPO/main/install.sh"
WANT=""
FORCE=0
UNINSTALL=0
WAIT_PID=""
WAIT_SECONDS=60
RELAUNCH=0
DRY_RUN=0
while [ $# -gt 0 ]; do
  case "$1" in
    --version) WANT="${2#v}"; shift ;;
    --force) FORCE=1 ;;
    --uninstall) UNINSTALL=1 ;;
    --wait-pid) WAIT_PID="$2"; shift ;;
    --wait-seconds) WAIT_SECONDS="$2"; shift ;;
    --relaunch) RELAUNCH=1 ;;
    # Stops before anything is downloaded or changed (used by the tests).
    --dry-run) DRY_RUN=1 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "$1 is needed"; }
need curl

OS="$(uname -s)"
ARCH="$(uname -m)"
BIN_DIR="$HOME/.local/bin"
case "$OS" in
  Linux)
    [ "$ARCH" = "x86_64" ] || die "only x86_64 Linux builds are published (this is $ARCH); build from source instead"
    HOME_DIR="${SISMOKAOS_HOME:-$HOME/.local/opt/sismokaos-gui}"
    ASSET_SUFFIX="linux-x86_64.AppImage"
    APP="$HOME_DIR/sismokaos-gui.AppImage"
    SHA() { sha256sum "$1" | cut -d' ' -f1; }
    ;;
  Darwin)
    HOME_DIR="${SISMOKAOS_HOME:-$HOME/Applications}"
    ASSET_SUFFIX="macos-universal.zip"
    APP="$HOME_DIR/sismokaos-gui.app"
    SHA() { shasum -a 256 "$1" | cut -d' ' -f1; }
    ;;
  *) die "unsupported system: $OS (Windows uses install.ps1)" ;;
esac
VERSION_FILE="$HOME_DIR/VERSION"
UPDATER="$HOME_DIR/update"

if [ -n "$WAIT_PID" ]; then
  # Called from the running application: let it exit first.
  i=0
  while kill -0 "$WAIT_PID" 2>/dev/null && [ $i -lt "$WAIT_SECONDS" ]; do sleep 1; i=$((i + 1)); done
  # Still there (it was asked to stay open, or it is finishing its jobs):
  # replacing an application that runs would break it, so nothing is touched.
  if kill -0 "$WAIT_PID" 2>/dev/null; then
    die "sismokaos-gui (process $WAIT_PID) is still running after $WAIT_SECONDS s; nothing was changed. Close it and update again."
  fi
fi

if [ "$DRY_RUN" = 1 ]; then
  say "dry run: nothing downloaded, nothing changed (installation: $HOME_DIR)."
  exit 0
fi

if [ "$UNINSTALL" = 1 ]; then
  rm -rf "$APP" "$UPDATER" "$VERSION_FILE" "$BIN_DIR/sismokaos-gui"
  if [ "$OS" = Linux ]; then
    rm -f "$HOME/.local/share/applications/sismokaos-gui.desktop" \
          "$HOME/.local/share/icons/hicolor/scalable/apps/sismokaos-gui.svg"
    rmdir "$HOME_DIR" 2>/dev/null || true
  fi
  say "sismokaos-gui removed (user data kept)."
  exit 0
fi

if [ -z "$WANT" ]; then
  WANT="$(curl -fsSL -H 'Accept: application/vnd.github+json' "https://api.github.com/repos/$REPO/releases/latest" \
          | sed -n 's/.*"tag_name": *"v\{0,1\}\([^"]*\)".*/\1/p' | head -n1)"
  [ -n "$WANT" ] || die "could not read the latest release of $REPO"
fi
HAVE=""
[ -f "$VERSION_FILE" ] && HAVE="$(cat "$VERSION_FILE")"
if [ "$HAVE" = "$WANT" ] && [ "$FORCE" = 0 ]; then
  say "sismokaos-gui $WANT is already installed in $HOME_DIR."
  [ "$RELAUNCH" = 1 ] && exec "$BIN_DIR/sismokaos-gui"
  exit 0
fi

ASSET="sismokaos-gui-$WANT-$ASSET_SUFFIX"
BASE="https://github.com/$REPO/releases/download/v$WANT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
say "Downloading $ASSET ..."
curl -fSL --progress-bar -o "$TMP/$ASSET" "$BASE/$ASSET" || die "no such release asset: $BASE/$ASSET"
curl -fsSL -o "$TMP/SHA256SUMS" "$BASE/SHA256SUMS" || die "no SHA256SUMS in release v$WANT"
EXPECTED="$(grep " $ASSET\$" "$TMP/SHA256SUMS" | cut -d' ' -f1)"
[ -n "$EXPECTED" ] || die "$ASSET is not listed in SHA256SUMS"
[ "$(SHA "$TMP/$ASSET")" = "$EXPECTED" ] || die "checksum mismatch for $ASSET; not installed"

mkdir -p "$HOME_DIR" "$BIN_DIR"
case "$OS" in
  Linux)
    chmod +x "$TMP/$ASSET"
    # Beside the old one first, then in its place in one step: a failure on
    # the way (a full disk) leaves the installed version as it was.
    rm -f "$APP.new"
    cp "$TMP/$ASSET" "$APP.new" || { rm -f "$APP.new"; die "could not write $APP.new; the installed version is unchanged"; }
    chmod +x "$APP.new"
    mv -f "$APP.new" "$APP"
    ln -sfn "$APP" "$BIN_DIR/sismokaos-gui"
    mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/scalable/apps"
    curl -fsSL -o "$HOME/.local/share/icons/hicolor/scalable/apps/sismokaos-gui.svg" \
      "https://raw.githubusercontent.com/$REPO/v$WANT/packaging/sismokaos-gui.svg" || true
    cat > "$HOME/.local/share/applications/sismokaos-gui.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=sismokaos-gui
Comment=Seismic feature extraction, classification and AFAD/TDVMS downloads
Exec=$APP
Icon=sismokaos-gui
Categories=Science;Geoscience;Qt;
Terminal=false
StartupWMClass=sismokaos-gui
EOF
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
    ;;
  Darwin)
    need ditto
    # Unpacked beside the old bundle, then swapped in: the old one is only
    # removed once the new one is in place, and comes back if the swap fails.
    NEW="$HOME_DIR/.sismokaos-gui.new"
    OLD="$HOME_DIR/.sismokaos-gui.old"
    rm -rf "$NEW" "$OLD"
    ditto -x -k "$TMP/$ASSET" "$NEW" || { rm -rf "$NEW"; die "could not unpack $ASSET; the installed version is unchanged"; }
    [ -d "$NEW/sismokaos-gui.app" ] || { rm -rf "$NEW"; die "$ASSET holds no sismokaos-gui.app; the installed version is unchanged"; }
    if [ -e "$APP" ]; then
      mv "$APP" "$OLD" || { rm -rf "$NEW"; die "could not move the installed version aside; it is unchanged"; }
    fi
    if ! mv "$NEW/sismokaos-gui.app" "$APP"; then
      [ -e "$OLD" ] && mv "$OLD" "$APP"
      rm -rf "$NEW"
      die "could not put the new version in place; the installed version was put back"
    fi
    rm -rf "$NEW" "$OLD"
    # The bundle is not notarised; without this Gatekeeper refuses a download.
    xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true
    ln -sfn "$APP/Contents/MacOS/sismokaos-gui" "$BIN_DIR/sismokaos-gui"
    ;;
esac

# Keep this script beside the application as the updater.
if [ -f "$0" ] && [ "$(basename "$0")" != "sh" ]; then
  cp "$0" "$UPDATER"
else
  curl -fsSL -o "$UPDATER" "$RAW" || true
fi
[ -f "$UPDATER" ] && chmod +x "$UPDATER"
printf '%s\n' "$WANT" > "$VERSION_FILE"

say "sismokaos-gui $WANT installed in $HOME_DIR."
case ":$PATH:" in *":$BIN_DIR:"*) ;; *) say "Note: $BIN_DIR is not on your PATH; run $APP directly or add it." ;; esac
say "Update later with: $UPDATER"
[ "$RELAUNCH" = 1 ] && exec "$BIN_DIR/sismokaos-gui"
exit 0
