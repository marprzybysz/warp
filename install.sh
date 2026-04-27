#!/usr/bin/env bash
# WARP bootstrap installer — run as root after extracting tarball
# Usage: sudo bash install.sh

set -e

PREFIX="${PREFIX:-/usr}"
CONFDIR="${CONFDIR:-/etc/warp}"
MANDIR="${MANDIR:-$PREFIX/share/man/man1}"
LIBDIR="${LIBDIR:-$PREFIX/lib/warp}"
DBDIR="${DBDIR:-/var/lib/warp/db}"
CACHEDIR="${CACHEDIR:-/var/cache/warp}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$EUID" -ne 0 ]]; then
    echo "ERROR: Run as root: sudo bash install.sh"
    exit 1
fi

echo "Installing WARP — Warp Archive Repository Packager"
echo ""

# Binary
install -Dm755 "$SCRIPT_DIR/warp.sh" "$PREFIX/bin/warp"
echo "  $PREFIX/bin/warp"

# Libs
mkdir -p "$LIBDIR"
for f in "$SCRIPT_DIR"/lib/*.sh; do
    install -Dm644 "$f" "$LIBDIR/$(basename "$f")"
    echo "  $LIBDIR/$(basename "$f")"
done

# Config
mkdir -p "$CONFDIR"
if [[ ! -f "$CONFDIR/warp.conf" ]]; then
    install -Dm644 "$SCRIPT_DIR/conf/warp.conf" "$CONFDIR/warp.conf"
    echo "  $CONFDIR/warp.conf"
else
    echo "  $CONFDIR/warp.conf (skipped — already exists)"
fi

# Man page
if [[ -f "$SCRIPT_DIR/man/warp.1" ]]; then
    mkdir -p "$MANDIR"
    install -Dm644 "$SCRIPT_DIR/man/warp.1" "$MANDIR/warp.1"
    echo "  $MANDIR/warp.1"
fi

# DB and cache dirs
mkdir -p "$DBDIR" "$CACHEDIR/index" "$CACHEDIR/packages"
echo "  $DBDIR"
echo "  $CACHEDIR"

# Patch warp.sh to use installed lib path
sed -i "s|source \"\$SCRIPT_DIR/lib/|source \"$LIBDIR/|g" "$PREFIX/bin/warp"

echo ""
echo "Done! Run: warp --version"
