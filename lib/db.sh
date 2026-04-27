#!/usr/bin/env bash
# Baza danych pakietów — /var/lib/warp/db/

WARP_DB="${WARP_DB:-/var/lib/warp/db}"
WARP_LOG="${WARP_LOG:-/var/lib/warp/warp.log}"

db_init() {
    mkdir -p "$WARP_DB"
}

db_exists() {
    [[ -d "$WARP_DB/$1" ]]
}

db_save() {
    local name="$1"
    local version="${2:-unknown}"
    local source="${3:-warp}"
    local files_list="${4:-}"

    mkdir -p "$WARP_DB/$name"
    cat > "$WARP_DB/$name/WARPINFO" <<EOF
name=$name
version=$version
installed=$(date +%Y-%m-%dT%H:%M:%S)
source=$source
EOF
    echo "$source" > "$WARP_DB/$name/SOURCE"
    if [[ -n "$files_list" ]]; then
        echo "$files_list" > "$WARP_DB/$name/FILES"
    fi
}

db_get_info() {
    local name="$1"
    [[ -f "$WARP_DB/$name/WARPINFO" ]] && cat "$WARP_DB/$name/WARPINFO"
}

db_get_version() {
    local name="$1"
    grep '^version=' "$WARP_DB/$name/WARPINFO" 2>/dev/null | cut -d= -f2
}

db_get_files() {
    local name="$1"
    [[ -f "$WARP_DB/$name/FILES" ]] && cat "$WARP_DB/$name/FILES"
}

db_get_deps() {
    local name="$1"
    [[ -f "$WARP_DB/$name/DEPS" ]] && cat "$WARP_DB/$name/DEPS"
}

db_remove() {
    local name="$1"
    rm -rf "${WARP_DB:?}/$name"
}

db_list() {
    [[ -d "$WARP_DB" ]] || return
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        local version
        version=$(grep '^version=' "$pkg_dir/WARPINFO" 2>/dev/null | cut -d= -f2)
        printf "%-30s %s\n" "$name" "${version:-unknown}"
    done
}

db_list_names() {
    [[ -d "$WARP_DB" ]] || return
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        basename "$pkg_dir"
    done
}

db_owner() {
    local file="$1"
    [[ -d "$WARP_DB" ]] || return
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -f "$pkg_dir/FILES" ]] || continue
        if grep -qxF "$file" "$pkg_dir/FILES" 2>/dev/null; then
            basename "$pkg_dir"
            return 0
        fi
    done
    return 1
}

db_log() {
    local action="$1"
    local pkg="$2"
    local version="${3:-}"
    mkdir -p "$(dirname "$WARP_LOG")"
    printf "%s  %-10s  %-25s  %s\n" \
        "$(date +%Y-%m-%dT%H:%M:%S)" "$action" "$pkg" "${version:-}" \
        >> "$WARP_LOG"
}
