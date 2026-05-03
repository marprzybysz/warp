#!/usr/bin/env bash
# Diagnostics: --check, --fix, --orphans, --log, --rollback, -DC, -DA, --verify, --push, --scan-system

WARP_INIT_MARKER="/var/lib/warp/.initialized"

cmd_scan_system() {
    local auto="${1:-0}"

    [[ $auto -eq 1 ]] && echo "First run — scanning system libraries..." || log_step "Scanning system libraries..."
    echo ""

    db_init

    if ! command -v ldconfig &>/dev/null; then
        warn "ldconfig not found — skipping system scan"
        touch "$WARP_INIT_MARKER"
        return
    fi

    local count=0
    declare -A _seen=()

    while IFS= read -r line; do
        local soname
        soname=$(awk '{print $1}' <<< "$line")
        [[ "$soname" == lib*.so* ]] || continue

        local name version
        name=$(sed 's/^lib//; s/\.so.*//' <<< "$soname")
        [[ -z "$name" ]] && continue
        [[ -n "${_seen[$name]:-}" ]] && continue
        _seen[$name]=1

        if [[ "$soname" =~ \.so\.([0-9][0-9.]*) ]]; then
            version="${BASH_REMATCH[1]}"
        else
            version="system"
        fi

        # Never overwrite packages installed by WARP itself
        if db_exists "$name"; then
            local src
            src=$(grep '^source=' "$WARP_DB/$name/WARPINFO" 2>/dev/null | cut -d= -f2)
            [[ "$src" != "system" ]] && continue
        fi

        db_save "$name" "$version" "system"
        count=$(( count + 1 ))
    done < <(ldconfig -p 2>/dev/null | tail -n +2)

    touch "$WARP_INIT_MARKER"
    log_step "Registered $count system libraries..." ok
    echo ""
}

cmd_check() {
    local errors=0
    log_step "Checking integrity of installed packages..."
    echo ""

    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        [[ -f "$pkg_dir/FILES" ]] || continue

        local missing=()
        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            [[ -f "$f" ]] || missing+=("$f")
        done < "$pkg_dir/FILES"

        if [[ ${#missing[@]} -gt 0 ]]; then
            log_step "$name" err
            for f in "${missing[@]}"; do
                echo "    missing: $f"
            done
            errors=$(( errors + 1 ))
        else
            log_step "$name" ok
        fi
    done

    echo ""
    if [[ $errors -eq 0 ]]; then
        echo "All packages OK."
    else
        echo "$errors package(s) have missing files. Run: warp --fix"
    fi
}

cmd_fix() {
    log_step "Checking dependencies for all packages..."
    echo ""
    local fixed=0

    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        [[ -f "$pkg_dir/DEPS" ]] || continue

        local deps
        deps=$(cat "$pkg_dir/DEPS")
        [[ -z "$deps" ]] && continue

        IFS=',' read -ra dep_list <<< "$deps"
        for dep in "${dep_list[@]}"; do
            dep="${dep// /}"
            [[ -z "$dep" ]] && continue
            if ! db_exists "$dep"; then
                warn "Missing dep '$dep' for '$name' — installing..."
                repo_install "$dep"
                fixed=$(( fixed + 1 ))
            fi
        done
    done

    echo ""
    if [[ $fixed -eq 0 ]]; then
        echo "No broken dependencies found."
    else
        echo "Fixed $fixed missing dependency/dependencies."
    fi
}

cmd_orphans() {
    log_step "Scanning for orphaned packages..."
    echo ""

    local all_deps=()
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -f "$pkg_dir/DEPS" ]] || continue
        local deps
        deps=$(cat "$pkg_dir/DEPS")
        IFS=',' read -ra dep_list <<< "$deps"
        for dep in "${dep_list[@]}"; do
            dep="${dep// /}"
            [[ -n "$dep" ]] && all_deps+=("$dep")
        done
    done

    local orphans=()
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local name
        name=$(basename "$pkg_dir")
        local is_dep=0
        for dep in "${all_deps[@]}"; do
            [[ "$dep" == "$name" ]] && { is_dep=1; break; }
        done
        [[ $is_dep -eq 0 ]] && orphans+=("$name")
    done

    if [[ ${#orphans[@]} -eq 0 ]]; then
        echo "No orphaned packages."
        return
    fi

    printf "%-30s %s\n" "PACKAGE" "VERSION"
    printf "%-30s %s\n" "-------" "-------"
    for pkg in "${orphans[@]}"; do
        local ver
        ver=$(db_get_version "$pkg")
        printf "%-30s %s\n" "$pkg" "${ver:-unknown}"
    done
    echo ""
    echo "Remove with: warp -D <package>"
}

cmd_log() {
    if [[ ! -f "$WARP_LOG" ]]; then
        echo "No operations logged yet."
        return
    fi
    printf "%-20s %-10s %-25s %s\n" "DATE" "ACTION" "PACKAGE" "VERSION"
    printf "%-20s %-10s %-25s %s\n" "----" "------" "-------" "-------"
    cat "$WARP_LOG"
}

cmd_rollback() {
    local name="$1"
    [[ -z "$name" ]] && done_err "Provide a package name"
    db_exists "$name" || done_err "Package '$name' is not installed"

    local cached_dir="$WARP_PKG_CACHE"
    local prev
    prev=$(ls -t "$cached_dir/${name}"-*.wrp 2>/dev/null | sed -n '2p')

    if [[ -z "$prev" ]]; then
        done_err "No previous version cached for '$name' — cannot rollback"
    fi

    log_step "Rolling back $name to $(basename "$prev")..."
    remove_pkg "$name" 0
    install_warp_pkg "$prev"
    db_log "rollback" "$name"
    echo ""
    done_ok
}

cmd_remove_cache() {
    local name="$1"
    [[ -z "$name" ]] && done_err "Provide a package name"
    local dir="$WARP_PKG_CACHE"
    local count
    count=$(ls "$dir/${name}"-*.wrp 2>/dev/null | wc -l)
    if [[ $count -eq 0 ]]; then
        echo "No cached files for '$name'."
        return
    fi
    rm -f "$dir/${name}"-*.wrp "$dir/${name}"-*.wrp.sha256
    log_step "Cleared cache for $name..." ok
    db_log "cache-clear" "$name"
}

cmd_remove_all() {
    local name="$1"
    [[ -z "$name" ]] && done_err "Provide a package name"
    cmd_remove "$name" 1
    cmd_remove_cache "$name"
}

cmd_verify() {
    local file="$1"
    [[ -z "$file" ]] && done_err "Provide a .wrp file path"
    [[ -f "$file" ]] || done_err "File not found: $file"

    local sha_file="${file}.sha256"
    if [[ ! -f "$sha_file" ]]; then
        done_err "No .sha256 file found next to: $file"
    fi

    log_step "Verifying $file..."
    local expected actual
    expected=$(awk '{print $1}' "$sha_file")
    actual=$(sha256sum "$file" | cut -d' ' -f1)

    if [[ "$actual" == "$expected" ]]; then
        log_step "SHA256 OK" ok
        echo "  expected: $expected"
        echo "  actual:   $actual"
    else
        log_step "SHA256 MISMATCH" err
        echo "  expected: $expected"
        echo "  actual:   $actual"
        exit 1
    fi
}

cmd_push() {
    local file="$1"
    [[ -z "$file" ]] && done_err "Provide a .wrp file path"
    [[ -f "$file" ]] || done_err "File not found: $file"

    local repo_url="${CFG_REPO:-}"
    [[ -z "$repo_url" ]] && done_err "No repo URL configured in warp.conf"

    local push_url="${CFG_PUSH_URL:-}"
    if [[ -z "$push_url" ]]; then
        # derive push URL from repo URL (replace https:// with scp-style or http PUT)
        push_url="$repo_url"
    fi

    log_step "Uploading $(basename "$file") to $push_url..."

    if command -v curl &>/dev/null; then
        curl -fsSL --max-time "${CFG_TIMEOUT:-60}" \
            -T "$file" "$push_url/$(basename "$file")" \
            || done_err "Upload failed"
        if [[ -f "${file}.sha256" ]]; then
            curl -fsSL --max-time "${CFG_TIMEOUT:-60}" \
                -T "${file}.sha256" "$push_url/$(basename "${file}.sha256")" \
                || warn "SHA256 upload failed"
        fi
    else
        done_err "curl required for --push"
    fi

    log_step "Uploaded $(basename "$file")..." ok
    db_log "push" "$(basename "$file")"
}
