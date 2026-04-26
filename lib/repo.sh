#!/usr/bin/env bash
# Repo: --sync i -G

WARP_CACHE="${WARP_CACHE:-/var/cache/warp}"
WARP_INDEX_DIR="$WARP_CACHE/index"
WARP_PKG_CACHE="$WARP_CACHE/packages"

repo_sync() {
    local repo_url="${CFG_REPO:-https://repo.flow.org/core}"
    mkdir -p "$WARP_INDEX_DIR" "$WARP_PKG_CACHE"

    log_step "Synchronizacja z $repo_url..."

    local index_url="$repo_url/INDEX"
    local dest="$WARP_INDEX_DIR/INDEX"

    if command -v curl &>/dev/null; then
        curl -fsSL --max-time "${CFG_TIMEOUT:-30}" -o "$dest" "$index_url" \
            || done_err "Nie można pobrać INDEX z $index_url"
    elif command -v wget &>/dev/null; then
        wget -q --timeout="${CFG_TIMEOUT:-30}" -O "$dest" "$index_url" \
            || done_err "Nie można pobrać INDEX z $index_url"
    else
        done_err "Brak curl ani wget — zainstaluj jedno z nich"
    fi

    local count
    count=$(grep -c '^\[' "$dest" 2>/dev/null || echo 0)
    log_step "Pobrano INDEX ($count pakietów)..." ok
}

# Parsuje INDEX i zwraca wartość pola dla pakietu
# repo_index_get <pkg> <field>
repo_index_get() {
    local pkg="$1"
    local field="$2"
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX nie istnieje — uruchom najpierw: warp --sync"

    awk -v pkg="[$pkg]" -v field="$field=" '
        /^\[/ { in_pkg = ($0 == pkg) }
        in_pkg && index($0, field) == 1 { print substr($0, length(field)+1); exit }
    ' "$index"
}

repo_install() {
    local pkg="$1"
    [[ -z "$pkg" ]] && done_err "Podaj nazwę pakietu"

    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX nie istnieje — uruchom najpierw: warp --sync"

    # Sprawdź czy pakiet istnieje w INDEX
    grep -q "^\[$pkg\]" "$index" || done_err "Pakiet '$pkg' nie istnieje w repozytorium"

    local version file sha256 repo_url
    version=$(repo_index_get "$pkg" "version")
    file=$(repo_index_get "$pkg" "file")
    sha256=$(repo_index_get "$pkg" "sha256")
    repo_url="${CFG_REPO:-https://repo.flow.org/core}"

    log_step "Znaleziono: $pkg $version" ok

    # Sprawdź czy już zainstalowany
    if db_exists "$pkg"; then
        local installed_ver
        installed_ver=$(grep '^version=' "$WARP_DB/$pkg/WARPINFO" 2>/dev/null | cut -d= -f2)
        if [[ "$installed_ver" == "$version" ]]; then
            log_step "$pkg $version już zainstalowany..." ok
            return 0
        fi
    fi

    # Sprawdź cache
    local cached="$WARP_PKG_CACHE/$(basename "$file")"
    if [[ -f "$cached" ]]; then
        log_step "Z cache: $(basename "$file")" ok
    else
        log_step "Pobieranie $pkg $version..."
        mkdir -p "$WARP_PKG_CACHE"

        local url="$repo_url/$file"
        local total_size
        total_size=$(repo_index_get "$pkg" "size")

        if command -v curl &>/dev/null; then
            curl -fL --max-time "${CFG_TIMEOUT:-30}" \
                --progress-bar -o "$cached" "$url" 2>&1 | \
                _curl_progress "$pkg" "$total_size"
            [[ ${PIPESTATUS[0]} -ne 0 ]] && done_err "Błąd pobierania $url"
        elif command -v wget &>/dev/null; then
            wget -q --show-progress --timeout="${CFG_TIMEOUT:-30}" \
                -O "$cached" "$url" || done_err "Błąd pobierania $url"
        fi
    fi

    # Weryfikacja SHA256
    if [[ -n "$sha256" ]]; then
        log_step "Weryfikacja sumy kontrolnej..."
        local actual
        actual=$(sha256sum "$cached" | cut -d' ' -f1)
        [[ "$actual" == "$sha256" ]] || {
            rm -f "$cached"
            done_err "SHA256 niezgodny — plik uszkodzony"
        }
        log_step "SHA256 OK..." ok
    fi

    # Zainstaluj
    install_warp_pkg "$cached"
}

# Wyświetla postęp curl w naszym formacie
_curl_progress() {
    local pkg="$1"
    local total="${2:-0}"
    local pct=0
    while IFS= read -r line; do
        if [[ "$line" =~ ([0-9]+)% ]]; then
            pct="${BASH_REMATCH[1]}"
            progress_bar "$pct" "Downloading $pkg"
        fi
    done
    clear_progress
    log_step "Pobieranie $pkg..." ok
}

repo_search() {
    local query="$1"
    local index="$WARP_INDEX_DIR/INDEX"
    [[ -f "$index" ]] || done_err "INDEX nie istnieje — uruchom najpierw: warp --sync"
    [[ -z "$query" ]] && done_err "Podaj szukaną frazę"

    local results
    results=$(grep -i "^\[$query\]\|description=.*$query\|name=.*$query" "$index" \
        | grep '^\[' | tr -d '[]' || true)

    if [[ -z "$results" ]]; then
        echo "Brak wyników dla: $query"
        return 1
    fi

    printf "%-25s %-12s %s\n" "PAKIET" "WERSJA" "OPIS"
    printf "%-25s %-12s %s\n" "------" "------" "----"
    while IFS= read -r pkg; do
        local ver desc
        ver=$(repo_index_get "$pkg" "version")
        desc=$(repo_index_get "$pkg" "description")
        printf "%-25s %-12s %s\n" "$pkg" "${ver:-?}" "${desc:-}"
    done <<< "$results"
}
