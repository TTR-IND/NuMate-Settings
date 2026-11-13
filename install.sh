#!/bin/sh
# Build and install this tree's numate-settings.
#
# The NuMATE installer fetches a GitHub release and runs
#   make && sudo make install prefix=/usr/local
# This script does the same against the sources next to it, so a
# working-copy Interface page can be tested without a release.
#
# Usage:
#   ./install.sh                 build + install to PREFIX (/usr/local)
#   PREFIX=$HOME/.local ./install.sh
#   ./install.sh --uninstall     remove the files this script installed
#
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX=${PREFIX:-/usr/local}
BINDIR=$PREFIX/bin
DATADIR=$PREFIX/share
BIN=$BINDIR/numate-settings
PKGS="gtk+-3.0 glib-2.0 gio-2.0 gio-unix-2.0 libmatemixer mate-desktop-2.0 libxml-2.0 xrandr xcursor libnm x11"

log()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

run_install()
{
	if [ -w "$1" ] || [ "$(id -u)" -eq 0 ]; then
		install "$@"
	else
		sudo install "$@"
	fi
}

run_rm()
{
	if [ -w "$(dirname -- "$1")" ] || [ "$(id -u)" -eq 0 ]; then
		rm -f "$1"
	else
		sudo rm -f "$1"
	fi
}

uninstall()
{
	run_rm "$BIN"
	run_rm "$DATADIR/numate-settings/style.css"
	run_rm "$DATADIR/applications/numate-settings.desktop"
	run_rm "$DATADIR/doc/numate-settings/LICENSE"
	run_rm "$DATADIR/doc/numate-settings/THIRD-PARTY.md"
	if [ "$(id -u)" -eq 0 ]; then
		rmdir "$DATADIR/doc/numate-settings" 2>/dev/null || true
		rmdir "$DATADIR/numate-settings" 2>/dev/null || true
	else
		sudo rmdir "$DATADIR/doc/numate-settings" 2>/dev/null || true
		sudo rmdir "$DATADIR/numate-settings" 2>/dev/null || true
	fi
	log "numate-settings removed from $PREFIX"
	exit 0
}

if [ "${1:-}" = "--uninstall" ] || [ "${1:-}" = "uninstall" ]; then
	uninstall
fi

command -v pkg-config >/dev/null 2>&1 || die "pkg-config is required"
command -v make >/dev/null 2>&1 || die "make is required"

missing=
for m in $PKGS; do
	pkg-config --exists "$m" 2>/dev/null || missing="$missing $m"
done
if [ -n "$missing" ]; then
	die "pkg-config modules missing:$missing"
fi

log "building numate-settings in $ROOT"
make -C "$ROOT"

pkill -x numate-settings 2>/dev/null || true

log "installing to $PREFIX"
if [ -w "$BINDIR" ] || [ "$(id -u)" -eq 0 ]; then
	make -C "$ROOT" install prefix="$PREFIX"
else
	sudo make -C "$ROOT" install prefix="$PREFIX"
fi

if [ -x "$BIN" ]; then
	log "installed $BIN"
else
	die "install finished but $BIN is missing"
fi

case ":$PATH:" in
	*:"$BINDIR":*) ;;
	*) warn "$BINDIR is not on PATH — axiom-panel will not find numate-settings" ;;
esac

log "done. launch with: $BIN"
