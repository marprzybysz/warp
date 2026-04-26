#!/usr/bin/env bash
# Logika instalacji pakietów

INSTALL_PREFIX="${INSTALL_PREFIX:-}"  # puste = root /

install_warp_pkg() {
    local file="$1"

    log_step "Wykrywanie formatu..." ok
    log_step "Rozpakowywanie do katalogu tymczasowego..."

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Nie można wypakować archiwum: $file"

    # Odczytaj WARPINFO
    local info="$tmpdir/WARPINFO"
    [[ -f "$info" ]] || { rm -rf "$tmpdir"; done_err "Brak pliku WARPINFO w paczce"; }

    local name version
    name=$(grep '^name=' "$info" | cut -d= -f2)
    version=$(grep '^version=' "$info" | cut -d= -f2)
    log_step "Czytanie metadanych ($name $version)..." ok

    # Sprawdź zależności
    local deps_file="$tmpdir/DEPS"
    if [[ -f "$deps_file" ]]; then
        local deps
        deps=$(cat "$deps_file")
        if [[ -n "$deps" ]]; then
            log_step "Sprawdzanie zależności..."
            local missing=()
            while IFS=',' read -ra dep_list; do
                for dep in "${dep_list[@]}"; do
                    dep="${dep// /}"
                    [[ -z "$dep" ]] && continue
                    db_exists "$dep" || missing+=("$dep")
                done
            done <<< "$deps"
            if [[ ${#missing[@]} -gt 0 ]]; then
                warn "Brakujące zależności: ${missing[*]}"
                warn "Zainstaluj je ręcznie lub użyj warp -G"
            else
                log_step "Zależności spełnione..." ok
            fi
        fi
    fi

    # Uruchom skrypt INSTALL jeśli istnieje
    if [[ -f "$tmpdir/INSTALL" ]]; then
        log_step "Uruchamianie skryptu instalacyjnego..."
        chmod +x "$tmpdir/INSTALL"
        (cd "$tmpdir" && bash INSTALL) || { rm -rf "$tmpdir"; done_err "Skrypt INSTALL zakończył się błędem"; }
        log_step "Skrypt instalacyjny..." ok
    fi

    # Kopiuj pliki
    if [[ -d "$tmpdir/files" ]]; then
        local dest="${INSTALL_PREFIX:-/}"
        log_step "Instalowanie plików do $dest..."

        local all_files
        all_files=$(find "$tmpdir/files" -type f | sed "s|$tmpdir/files||")
        local total
        total=$(echo "$all_files" | wc -l)
        local count=0

        while IFS= read -r rel_path; do
            [[ -z "$rel_path" ]] && continue
            local src="$tmpdir/files$rel_path"
            local dst="${dest%/}$rel_path"
            mkdir -p "$(dirname "$dst")"
            cp -a "$src" "$dst"
            count=$(( count + 1 ))
            local pct=$(( count * 100 / total ))
            progress_bar "$pct" "Instalacja $name"
        done <<< "$all_files"

        clear_progress
        log_step "Instalacja plików..." ok

        # Zapisz listę plików do bazy
        db_init
        db_save "$name" "$version" "warp" "$all_files"
    else
        db_init
        db_save "$name" "$version" "warp"
    fi

    rm -rf "$tmpdir"
}

install_tarxz_pkg() {
    local file="$1"

    log_step "Wykrywanie formatu..." ok
    warn "Brak WARPINFO — tryb surowy"
    [[ $QUIET -eq 0 ]] && echo ""

    local name version
    name=$(name_from_file "$file")
    version=$(version_from_file "$file")

    log_step "Rozpakowywanie $name..."

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Nie można wypakować archiwum: $file"

    local all_files
    all_files=$(find "$tmpdir" -type f | sed "s|$tmpdir||")
    local total
    total=$(echo "$all_files" | wc -l)
    local count=0

    # Zainstaluj do / — ścieżki w archiwum są już absolutne (usr/local/bin/...)
    local dest="/"
    while IFS= read -r rel_path; do
        [[ -z "$rel_path" ]] && continue
        local src="$tmpdir$rel_path"
        local dst="${dest%/}$rel_path"
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
        count=$(( count + 1 ))
        local pct=$(( count * 100 / total ))
        progress_bar "$pct" "Rozpakowywanie"
    done <<< "$all_files"

    clear_progress
    log_step "Rozpakowanie..." ok

    local installed_files
    installed_files=$(echo "$all_files" | sed "s|^|/|" | sed 's|//|/|g')

    db_init
    db_save "$name" "$version" "tarxz" "$installed_files"

    log_step "Rejestrowanie pakietu..." ok
    if [[ $QUIET -eq 0 ]]; then
        echo ""
        echo "Zainstalowano do: /usr/local (ścieżki z archiwum)"
    fi
}
