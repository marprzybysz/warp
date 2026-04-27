#!/usr/bin/env bash
# Remove logic

# Check if $dep is required by any installed package other than $exclude
_dep_required_by() {
    local dep="$1"
    local exclude="$2"
    for pkg_dir in "$WARP_DB"/*/; do
        [[ -d "$pkg_dir" ]] || continue
        local pkg
        pkg=$(basename "$pkg_dir")
        [[ "$pkg" == "$exclude" ]] && continue
        [[ -f "$pkg_dir/DEPS" ]] || continue
        local deps
        deps=$(cat "$pkg_dir/DEPS")
        IFS=',' read -ra dep_list <<< "$deps"
        for d in "${dep_list[@]}"; do
            d="${d// /}"
            [[ "$d" == "$dep" ]] && return 0
        done
    done
    return 1
}

_remove_files() {
    local name="$1"
    local files
    files=$(db_get_files "$name")

    if [[ -n "$files" ]]; then
        local total count=0
        total=$(echo "$files" | wc -l)

        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            [[ -f "$f" ]] && rm -f "$f"
            count=$(( count + 1 ))
            progress_bar $(( count * 95 / total )) ""
        done <<< "$files"

        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            rmdir --ignore-fail-on-non-empty "$(dirname "$f")" 2>/dev/null || true
        done <<< "$files"
    fi

    local remove_script="$WARP_DB/$name/REMOVE"
    if [[ -f "$remove_script" ]]; then
        bash "$remove_script" 2>/dev/null || true
    fi

    progress_bar 100 ""
    db_remove "$name"
    db_log "remove" "$name"
    done_ok
}

remove_pkg() {
    local name="$1"
    local remove_deps="${2:-0}"

    db_exists "$name" || done_err "Package '$name' is not installed"

    if [[ "$remove_deps" -eq 1 ]]; then
        # Collect deps of this package
        local deps_str=""
        [[ -f "$WARP_DB/$name/DEPS" ]] && deps_str=$(cat "$WARP_DB/$name/DEPS")

        local safe_deps=()
        if [[ -n "$deps_str" ]]; then
            IFS=',' read -ra dep_list <<< "$deps_str"
            for dep in "${dep_list[@]}"; do
                dep="${dep// /}"
                [[ -z "$dep" ]] && continue
                db_exists "$dep" || continue
                if _dep_required_by "$dep" "$name"; then
                    warn "Keeping $dep — required by other packages"
                else
                    safe_deps+=("$dep")
                fi
            done
        fi

        # Show what will be removed
        if [[ $QUIET -eq 0 ]]; then
            echo ""
            echo "The following packages will be removed:"
            printf "  %s\n" "$name"
            for dep in "${safe_deps[@]}"; do
                printf "  %s\n" "$dep"
            done
            echo ""
            printf "Do you want to continue? [Y/n] "
            local ans; read -r ans; ans="${ans:-Y}"
            if [[ "$ans" != "Y" && "$ans" != "y" ]]; then
                echo "Aborted."
                return 1
            fi
            echo ""
        fi

        # Remove main package first
        log_step "Removing $name..."
        _remove_files "$name"

        # Remove safe deps
        for dep in "${safe_deps[@]}"; do
            log_step "Removing $dep..."
            _remove_files "$dep"
        done
    else
        _remove_files "$name"
    fi
}
