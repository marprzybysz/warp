#!/usr/bin/env bash
# Remove logic

remove_pkg() {
    local name="$1"
    local remove_deps="${2:-0}"

    db_exists "$name" || done_err "Package '$name' is not installed"

    log_step "Reading file list for $name..."
    local files
    files=$(db_get_files "$name")

    if [[ -n "$files" ]]; then
        local total
        total=$(echo "$files" | wc -l)
        local count=0

        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            [[ -f "$f" ]] && rm -f "$f"
            count=$(( count + 1 ))
            local pct=$(( count * 100 / total ))
            progress_bar "$pct" "Removing $name"
        done <<< "$files"

        clear_progress

        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            local dir
            dir=$(dirname "$f")
            rmdir --ignore-fail-on-non-empty "$dir" 2>/dev/null || true
        done <<< "$files"
    fi

    log_step "Removing files..." ok

    local remove_script="$WARP_DB/$name/REMOVE"
    if [[ -f "$remove_script" ]]; then
        bash "$remove_script" 2>/dev/null || true
        log_step "Remove script..." ok
    fi

    db_remove "$name"
    log_step "Package record removed..." ok
    db_log "remove" "$name"
}
