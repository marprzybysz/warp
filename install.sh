#!/usr/bin/env bash
# WARP bootstrap installer — build C++ binary and install
# Usage: sudo bash install.sh [PREFIX=/usr]

set -e

PREFIX="${PREFIX:-/usr}"
CONFDIR="${CONFDIR:-/etc/warp}"
MANDIR="${MANDIR:-$PREFIX/share/man/man1}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ "$EUID" -ne 0 ]]; then
    echo "ERROR: Run as root: sudo bash install.sh"
    exit 1
fi

echo "Building WARP — Warp Archive Repository Packager"
echo ""

# Check build tools
for cmd in cmake make pkg-config; do
    command -v "$cmd" &>/dev/null || { echo "ERROR: $cmd not found"; exit 1; }
done
for lib in libarchive libcurl; do
    pkg-config --exists "$lib" || { echo "ERROR: $lib dev package not found"; exit 1; }
done

BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_SYSCONFDIR=/etc \
    -DCMAKE_INSTALL_MANDIR="$PREFIX/share/man" \
    -DCMAKE_SILENT_MAKEFILE=ON

make -C "$BUILD_DIR" -j"$(nproc)"
make -C "$BUILD_DIR" install

echo "  $PREFIX/bin/warp"

# Config
mkdir -p "$CONFDIR/repos.d"
if [[ ! -f "$CONFDIR/warp.conf" ]]; then
    install -Dm644 "$SCRIPT_DIR/conf/warp.conf" "$CONFDIR/warp.conf"
    echo "  $CONFDIR/warp.conf"
else
    echo "  $CONFDIR/warp.conf (skipped — already exists)"
fi

# Default repo entry
if [[ ! -f "$CONFDIR/repos.d/1.conf" ]]; then
    echo "url=https://repo.flow.org/core" > "$CONFDIR/repos.d/1.conf"
    echo "  $CONFDIR/repos.d/1.conf"
fi

# Man page
[[ -f "$SCRIPT_DIR/man/warp.1" ]] && {
    mkdir -p "$MANDIR"
    install -Dm644 "$SCRIPT_DIR/man/warp.1" "$MANDIR/warp.1"
    echo "  $MANDIR/warp.1"
}

# Runtime dirs
mkdir -p /var/lib/warp/db /var/cache/warp/index /var/cache/warp/packages
echo "  /var/lib/warp/db"
echo "  /var/cache/warp"

echo ""
echo "Done! Run: warp --version"
