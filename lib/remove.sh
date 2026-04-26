#!/usr/bin/env bash
# Logika usuwania pakietów

remove_pkg() {
    local name="$1"
    local remove_deps="${2:-0}"

    db_exists "$name" || done_err "Pakiet '$name' nie jest zainstalowany"

    log_step "Czytanie listy plików $name..."
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
            progress_bar "$pct" "Usuwanie $name"
        done <<< "$files"

        clear_progress

        # Usuń puste katalogi po sobie
        while IFS= read -r f; do
            [[ -z "$f" ]] && continue
            local dir
            dir=$(dirname "$f")
            rmdir --ignore-fail-on-non-empty "$dir" 2>/dev/null || true
        done <<< "$files"
    fi

    log_step "Usuwanie plików..." ok

    # Uruchom skrypt REMOVE jeśli istnieje w db
    local remove_script="$WARP_DB/$name/REMOVE"
    if [[ -f "$remove_script" ]]; then
        bash "$remove_script" 2>/dev/null || true
        log_step "Skrypt deinstalacyjny..." ok
    fi

    db_remove "$name"
    log_step "Rejestr pakietu usunięty..." ok
}
