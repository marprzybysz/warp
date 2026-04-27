#!/usr/bin/env bash
# Repo: --sync and -G

WARP_CACHE="${WARP_CACHE:-/var/cache/warp}"
WARP_INDEX_DIR="$WARP_CACHE/index"
WARP_PKG_CACHE="$WARP_CACHE/packages"

repo_sync() {
    local repo_url="${CFG_REPO:-https://repo.flow.org/core}"
    mkdir -p "$WARP_INDEX_DIR" "$WARP_PKG_CACHE"

    log_step "Syncing from $repo_url..."

    local index_url="$repo_url/INDEX"
    local dest="$WARP_INDEX_DIR/INDEX"

    if command -v curl &>/dev/null; then
        curl -fsSL --max-time "${CFG_TIMEOUT:-30}" -o "$dest" "$index_url" \
            || done_err "Cannot fetch INDEX from $index_url"
    elif command -v wget &>/dev/null; then
        wget -q --timeout="${CFG_TIMEOUT:-30}" -O "$dest" "$index_url" \
            || done_err "Cannot fetch INDEX from $index_url"
    else
        done_err "curl or wget required — install one of them"
    fi

    local count
    count=$(grep -c '^\[' "$dest" 2>/dev/null || echo 0)
    log_step "Fetched INDEX ($count packages)..." ok
}

repo_index_get() {
    local pkg="$1"
    local field="$2"
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX not found — run: warp --sync"

    awk -v pkg="[$pkg]" -v field="$field=" '
        /^\[/ { in_pkg = ($0 == pkg) }
        in_pkg && index($0, field) == 1 { print substr($0, length(field)+1); exit }
    ' "$index"
}

repo_install() {
    local pkg="$1"
    local skip_confirm="${2:-0}"
    [[ -z "$pkg" ]] && done_err "Provide a package name"

    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX not found — run: warp --sync"

    grep -q "^\[$pkg\]" "$index" || done_err "Package '$pkg' not found in repository"

    local version file sha256 repo_url
    version=$(repo_index_get "$pkg" "version")
    file=$(repo_index_get "$pkg" "file")
    sha256=$(repo_index_get "$pkg" "sha256")
    repo_url="${CFG_REPO:-https://repo.flow.org/core}"

    log_step "Found: $pkg $version" ok

    if db_exists "$pkg"; then
        local installed_ver
        installed_ver=$(grep '^version=' "$WARP_DB/$pkg/WARPINFO" 2>/dev/null | cut -d= -f2)
        if [[ "$installed_ver" == "$version" ]]; then
            log_step "$pkg $version already installed..." ok
            return 0
        fi
    fi

    local cached="$WARP_PKG_CACHE/$(basename "$file")"
    if [[ -f "$cached" ]]; then
        log_step "From cache: $(basename "$file")" ok
    else
        log_step "Downloading $pkg $version..."
        mkdir -p "$WARP_PKG_CACHE"

        local url="$repo_url/$file"
        if command -v curl &>/dev/null; then
            curl -fL --max-time "${CFG_TIMEOUT:-30}" \
                --progress-bar -o "$cached" "$url" 2>&1 | \
                _curl_progress "$pkg"
            [[ ${PIPESTATUS[0]} -ne 0 ]] && done_err "Download failed: $url"
        elif command -v wget &>/dev/null; then
            wget -q --show-progress --timeout="${CFG_TIMEOUT:-30}" \
                -O "$cached" "$url" || done_err "Download failed: $url"
        fi
    fi

    if [[ -n "$sha256" ]]; then
        log_step "Verifying checksum..."
        local actual
        actual=$(sha256sum "$cached" | cut -d' ' -f1)
        [[ "$actual" == "$sha256" ]] || {
            rm -f "$cached"
            done_err "SHA256 mismatch — corrupted file"
        }
        log_step "SHA256 OK..." ok
    fi

    install_warp_pkg "$cached" "$skip_confirm"
}

_curl_progress() {
    local pkg="$1"
    while IFS= read -r line; do
        if [[ "$line" =~ ([0-9]+)% ]]; then
            progress_bar "${BASH_REMATCH[1]}" "Downloading $pkg"
        fi
    done
    clear_progress
    log_step "Downloaded $pkg..." ok
}

repo_list_updates() {
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX not found — run: warp --sync"

    local updates=()
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        local installed_ver
        installed_ver=$(grep '^version=' "$pkg_dir/WARPINFO" 2>/dev/null | cut -d= -f2)
        local repo_ver
        repo_ver=$(repo_index_get "$name" "version" 2>/dev/null)
        [[ -z "$repo_ver" ]] && continue
        [[ "$repo_ver" == "$installed_ver" ]] && continue
        updates+=("$name|$installed_ver|$repo_ver")
    done

    if [[ ${#updates[@]} -eq 0 ]]; then
        echo "All packages up to date."
        return 0
    fi

    printf "%-25s %-15s %s\n" "PACKAGE" "INSTALLED" "AVAILABLE"
    printf "%-25s %-15s %s\n" "-------" "---------" "---------"
    for entry in "${updates[@]}"; do
        IFS='|' read -r pkg inst avail <<< "$entry"
        printf "%-25s %-15s %s\n" "$pkg" "$inst" "$avail"
    done
    return 1
}

repo_upgrade() {
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX not found — run: warp --sync"

    local upgraded=0
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        local installed_ver
        installed_ver=$(grep '^version=' "$pkg_dir/WARPINFO" 2>/dev/null | cut -d= -f2)
        local repo_ver
        repo_ver=$(repo_index_get "$name" "version" 2>/dev/null)
        [[ -z "$repo_ver" ]] && continue
        [[ "$repo_ver" == "$installed_ver" ]] && continue

        log_step "Upgrading $name: $installed_ver → $repo_ver..."
        remove_pkg "$name" 0
        repo_install "$name"
        db_log "upgrade" "$name" "$repo_ver"
        upgraded=$(( upgraded + 1 ))
    done

    echo ""
    if [[ $upgraded -eq 0 ]]; then
        echo "Nothing to upgrade."
    else
        echo "$upgraded package(s) upgraded."
        done_ok
    fi
}

repo_search() {
    local query="$1"
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX not found — run: warp --sync"
    [[ -z "$query" ]] && done_err "Provide a search query"

    local results
    results=$(grep -i "^\[$query\]\|description=.*$query\|name=.*$query" "$index" \
        | grep '^\[' | tr -d '[]' || true)

    if [[ -z "$results" ]]; then
        echo "No results for: $query"
        return 1
    fi

    printf "%-25s %-12s %s\n" "PACKAGE" "VERSION" "DESCRIPTION"
    printf "%-25s %-12s %s\n" "-------" "-------" "-----------"
    while IFS= read -r pkg; do
        local ver desc
        ver=$(repo_index_get "$pkg" "version")
        desc=$(repo_index_get "$pkg" "description")
        printf "%-25s %-12s %s\n" "$pkg" "${ver:-?}" "${desc:-}"
    done <<< "$results"
}
