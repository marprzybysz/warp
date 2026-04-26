#!/usr/bin/env bash
# Wykrywanie i obsługa formatów paczek

# Zwraca: "warp" | "tarxz" | "unknown"
detect_format() {
    local file="$1"
    [[ -f "$file" ]] || { echo "unknown"; return 1; }

    case "$file" in
        *.wrp) echo "warp"; return 0 ;;
    esac

    # .tar.xz — sprawdź czy ma WARPINFO w środku
    if [[ "$file" == *.tar.xz || "$file" == *.tar.gz || "$file" == *.tar.bz2 ]]; then
        if tar -tf "$file" 2>/dev/null | grep -q '^WARPINFO$'; then
            echo "warp"
        else
            echo "tarxz"
        fi
        return 0
    fi

    echo "unknown"
    return 1
}

# Wypakuj archiwum do katalogu tymczasowego
# Zwraca ścieżkę do tmpdir (caller musi sprzątać)
extract_to_tmp() {
    local file="$1"
    local tmpdir
    tmpdir=$(mktemp -d /tmp/warp.XXXXXX)
    tar -xf "$file" -C "$tmpdir" 2>/dev/null || {
        rm -rf "$tmpdir"
        return 1
    }
    echo "$tmpdir"
}

# Wyciągnij nazwę pakietu z nazwy pliku
name_from_file() {
    local file="$1"
    local base
    base=$(basename "$file")
    # firefox-92.0-x86_64.tar.xz → firefox
    base="${base%.wrp}"
    base="${base%.tar.xz}"
    base="${base%.tar.gz}"
    base="${base%.tar.bz2}"
    # usuń wersję i arch (pattern: -digit lub -x86)
    base=$(echo "$base" | sed 's/-[0-9].*//')
    echo "$base"
}

# Wyciągnij wersję z nazwy pliku
version_from_file() {
    local file="$1"
    local base
    base=$(basename "$file")
    base="${base%.wrp}"
    base="${base%.tar.xz}"
    base="${base%.tar.gz}"
    base="${base%.tar.bz2}"
    local version
    version=$(echo "$base" | grep -oP '(?<=-)\d[^-]*' | head -1)
    echo "${version:-unknown}"
}
