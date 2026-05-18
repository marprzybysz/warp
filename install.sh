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
for cmd in g++ pkg-config; do
    command -v "$cmd" &>/dev/null || { echo "ERROR: $cmd not found"; exit 1; }
done
for lib in libarchive libcurl; do
    pkg-config --exists "$lib" || { echo "ERROR: $lib dev package not found"; exit 1; }
done

SRC_DIR="$SCRIPT_DIR/src"
SRCS=$(find "$SRC_DIR" -name '*.cpp' | tr '\n' ' ')
CXXFLAGS="-O2 -std=c++17 -DWARP_PATCH_NUM=\"manual\""
LIBS="$(pkg-config --cflags --libs libarchive libcurl)"

g++ $CXXFLAGS -I"$SRC_DIR" $SRCS $LIBS -o "$SCRIPT_DIR/warp"
install -Dm755 "$SCRIPT_DIR/warp" "$PREFIX/bin/warp"

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

# Shell completions
COMP_DIR="$SCRIPT_DIR/contrib/completions"
if [[ -f "$COMP_DIR/warp.fish" ]]; then
    install -Dm644 "$COMP_DIR/warp.fish" "$PREFIX/share/fish/vendor_completions.d/warp.fish"
    echo "  $PREFIX/share/fish/vendor_completions.d/warp.fish"
fi
if [[ -f "$COMP_DIR/warp.zsh" ]]; then
    install -Dm644 "$COMP_DIR/warp.zsh" "$PREFIX/share/zsh/site-functions/_warp"
    echo "  $PREFIX/share/zsh/site-functions/_warp"
fi

# Runtime dirs
mkdir -p /var/lib/warp/db /var/cache/warp/index /var/cache/warp/packages
echo "  /var/lib/warp/db"
echo "  /var/cache/warp"

# System library scan — register pre-existing libs so dep resolution works
echo ""
echo "Scanning system libraries..."
_warp_scan_count=0
declare -A _warp_seen=()
while IFS= read -r _line; do
    _soname=$(awk '{print $1}' <<< "$_line")
    [[ "$_soname" == lib*.so* ]] || continue
    _name=$(sed 's/^lib//; s/\.so.*//' <<< "$_soname")
    [[ -z "$_name" ]] && continue
    [[ -n "${_warp_seen[$_name]:-}" ]] && continue
    _warp_seen[$_name]=1
    if [[ "$_soname" =~ \.so\.([0-9][0-9.]*) ]]; then
        _ver="${BASH_REMATCH[1]}"
    else
        _ver="system"
    fi
    mkdir -p "/var/lib/warp/db/$_name"
    cat > "/var/lib/warp/db/$_name/WARPINFO" <<EOF
name=$_name
version=$_ver
installed=$(date +%Y-%m-%dT%H:%M:%S)
source=system
EOF
    echo "system" > "/var/lib/warp/db/$_name/SOURCE"
    _warp_scan_count=$(( _warp_scan_count + 1 ))
done < <(ldconfig -p 2>/dev/null | tail -n +2)
touch /var/lib/warp/.initialized
echo "  Registered $_warp_scan_count system libraries"

echo ""
echo "Done! Run: warp --version"
