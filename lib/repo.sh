#!/usr/bin/env bash
# Repo: --sync and -G

WARP_CACHE="${WARP_CACHE:-/var/cache/warp}"
WARP_INDEX_DIR="$WARP_CACHE/index"
WARP_PKG_CACHE="$WARP_CACHE/packages"

# Guard against circular deps across recursive repo_install calls
declare -A _WARP_INSTALLING=()

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

    # Short-circuit: already installed at this version
    local version
    version=$(repo_index_get "$pkg" "version")
    if db_exists "$pkg"; then
        local installed_ver
        installed_ver=$(grep '^version=' "$WARP_DB/$pkg/WARPINFO" 2>/dev/null | cut -d= -f2)
        if [[ "$installed_ver" == "$version" ]]; then
            log_step "$pkg $version already installed..." ok
            return 0
        fi
    fi

    # Guard against circular dependency loops
    if [[ -n "${_WARP_INSTALLING[$pkg]:-}" ]]; then
        warn "Circular dependency detected: $pkg — skipping"
        return 0
    fi
    _WARP_INSTALLING[$pkg]=1

    # Resolve and install dependencies before the main package
    local deps_str
    deps_str=$(repo_index_get "$pkg" "deps")
    if [[ -n "$deps_str" ]]; then
        IFS=',' read -ra _dep_list <<< "$deps_str"
        for _dep in "${_dep_list[@]}"; do
            _dep="${_dep// /}"
            [[ -z "$_dep" ]] && continue
            if db_exists "$_dep"; then
                log_step "Dependency $_dep already satisfied..." ok
            elif grep -q "^\[$_dep\]" "$index"; then
                log_step "Installing dependency: $_dep"
                repo_install "$_dep" 1
            elif ldconfig -p 2>/dev/null | grep -q "^[[:space:]]*lib${_dep}\.so"; then
                log_step "Dependency $_dep satisfied by system..." ok
                db_save "$_dep" "system" "system"
            else
                warn "Dependency '$_dep' not found in repository or system — package may not work"
            fi
        done
    fi

    log_step "Found: $pkg $version" ok

    local file sha256 repo_url
    file=$(repo_index_get "$pkg" "file")
    sha256=$(repo_index_get "$pkg" "sha256")
    repo_url="${CFG_REPO:-https://repo.flow.org/core}"

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
    unset "_WARP_INSTALLING[$pkg]"
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

cmd_gen_index() {
    local dir="${1:-.}"
    [[ -d "$dir" ]] || done_err "Directory not found: $dir"

    local out="$dir/INDEX"
    local count=0

    log_step "Scanning $dir for .wrp packages..."
    echo ""

    # Write to a temp file first so a partial run doesn't corrupt the existing INDEX
    local tmp
    tmp=$(mktemp /tmp/warp-index.XXXXXX)

    while IFS= read -r wrp; do
        local tmpdir
        tmpdir=$(mktemp -d /tmp/warp-idx.XXXXXX)

        # Extract only WARPINFO from the archive (fast — no full unpack)
        tar -xJf "$wrp" -C "$tmpdir" ./WARPINFO 2>/dev/null \
            || tar -xJf "$wrp" -C "$tmpdir" WARPINFO 2>/dev/null \
            || { rm -rf "$tmpdir"; warn "Cannot read WARPINFO from $(basename "$wrp") — skipping"; continue; }

        local info="$tmpdir/WARPINFO"
        [[ -f "$info" ]] || { rm -rf "$tmpdir"; warn "WARPINFO missing in $(basename "$wrp") — skipping"; continue; }

        local name version arch deps description
        name=$(grep '^name='        "$info" | cut -d= -f2)
        version=$(grep '^version='  "$info" | cut -d= -f2)
        arch=$(grep '^arch='        "$info" | cut -d= -f2)
        deps=$(grep '^deps='        "$info" | cut -d= -f2)
        description=$(grep '^description=' "$info" | cut -d= -f2)
        rm -rf "$tmpdir"

        [[ -z "$name" || -z "$version" ]] && { warn "Incomplete WARPINFO in $(basename "$wrp") — skipping"; continue; }

        local relpath sha256 size
        relpath="packages/$(basename "$wrp")"
        sha256=$(sha256sum "$wrp" | cut -d' ' -f1)
        size=$(stat -c%s "$wrp" 2>/dev/null || stat -f%z "$wrp" 2>/dev/null || echo 0)

        {
            echo "[$name]"
            echo "version=$version"
            echo "arch=${arch:-x86_64}"
            [[ -n "$deps" ]]        && echo "deps=$deps"
            [[ -n "$description" ]] && echo "description=$description"
            echo "file=$relpath"
            echo "sha256=$sha256"
            echo "size=$size"
            echo ""
        } >> "$tmp"

        log_step "$name $version" ok
        count=$(( count + 1 ))
    done < <(find "$dir/packages" -maxdepth 1 -name "*.wrp" 2>/dev/null | sort)

    if [[ $count -eq 0 ]]; then
        rm -f "$tmp"
        done_err "No .wrp files found in $dir/packages/"
    fi

    mv "$tmp" "$out"
    echo ""
    log_step "Written: $out ($count packages)" ok
}
