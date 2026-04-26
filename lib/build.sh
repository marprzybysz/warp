#!/usr/bin/env bash
# warp -cP — budowanie paczki .wrp z folderu

build_pkg() {
    local src_dir="$1"
    [[ -z "$src_dir" ]] && done_err "Podaj folder źródłowy"
    [[ -d "$src_dir" ]] || done_err "Nie znaleziono folderu: $src_dir"
    src_dir="${src_dir%/}"

    log_step "Sprawdzanie struktury folderu..."

    # Upewnij się że files/ istnieje
    local files_dir="$src_dir/files"
    if [[ ! -d "$files_dir" ]]; then
        warn "Brak folderu files/ — traktuję cały folder jako files/"
        local tmp_wrap
        tmp_wrap=$(mktemp -d /tmp/warp-build.XXXXXX)
        mkdir -p "$tmp_wrap/files"
        cp -a "$src_dir/." "$tmp_wrap/files/"
        src_dir="$tmp_wrap"
        files_dir="$src_dir/files"
    fi

    # Odczytaj lub utwórz WARPINFO
    local info_file="$src_dir/WARPINFO"
    local name version arch maintainer license description

    if [[ -f "$info_file" ]]; then
        name=$(grep '^name='        "$info_file" | cut -d= -f2)
        version=$(grep '^version='  "$info_file" | cut -d= -f2)
        arch=$(grep '^arch='        "$info_file" | cut -d= -f2)
        maintainer=$(grep '^maintainer=' "$info_file" | cut -d= -f2)
        license=$(grep '^license='  "$info_file" | cut -d= -f2)
        description=$(grep '^description=' "$info_file" | cut -d= -f2)
        log_step "Odczytano WARPINFO ($name $version)..." ok
    else
        log_step "Brak WARPINFO — uzupełnij metadane:"
        echo ""
        read -rp "  name:        " name
        read -rp "  version:     " version
        read -rp "  arch [x86_64]: " arch;     arch="${arch:-x86_64}"
        read -rp "  maintainer:  " maintainer
        read -rp "  license:     " license
        read -rp "  description: " description
        echo ""
    fi

    [[ -z "$name" || -z "$version" ]] && done_err "name i version są wymagane"

    # Auto-detect zależności przez ldd
    log_step "Wykrywanie zależności (ldd)..."
    local detected_deps=()
    while IFS= read -r bin; do
        if file "$bin" 2>/dev/null | grep -q ELF; then
            while IFS= read -r lib_line; do
                local lib
                lib=$(echo "$lib_line" | awk '{print $1}')
                [[ "$lib" == linux-* ]] && continue
                [[ "$lib" == /lib* || "$lib" == /usr* ]] && {
                    local pkg
                    pkg=$(basename "$lib" | sed 's/\.so.*//' | sed 's/^lib//')
                    [[ -n "$pkg" ]] && detected_deps+=("$pkg")
                }
            done < <(ldd "$bin" 2>/dev/null | grep -v 'not found' | grep '=>')
        fi
    done < <(find "$files_dir" -type f)

    # Deduplikuj
    local deps_str=""
    if [[ ${#detected_deps[@]} -gt 0 ]]; then
        deps_str=$(printf '%s\n' "${detected_deps[@]}" | sort -u | tr '\n' ',' | sed 's/,$//')
        log_step "Wykryto: $deps_str" ok
    else
        log_step "Brak wykrytych zależności..." ok
    fi

    # Pozwól użytkownikowi edytować deps
    if [[ $QUIET -eq 0 ]]; then
        read -rp "  Zależności [$deps_str]: " user_deps
        [[ -n "$user_deps" ]] && deps_str="$user_deps"
    fi

    # Zapisz WARPINFO i DEPS
    cat > "$src_dir/WARPINFO" <<EOF
name=$name
version=$version
arch=$arch
deps=$deps_str
maintainer=$maintainer
license=$license
description=$description
EOF
    echo "$deps_str" > "$src_dir/DEPS"
    log_step "Zapisano WARPINFO i DEPS..." ok

    # Spakuj
    local output="${name}-${version}-${arch}.wrp"
    log_step "Pakowanie do $output..."
    tar -cJf "$output" -C "$src_dir" . 2>/dev/null \
        || done_err "Błąd podczas pakowania"
    log_step "Paczka gotowa: $output" ok

    # Checksum
    local sha256
    sha256=$(sha256sum "$output" | cut -d' ' -f1)
    echo "$sha256  $output" > "${output}.sha256"
    log_step "SHA256: ${sha256:0:16}..." ok

    echo ""
    echo "Rozmiar: $(du -sh "$output" | cut -f1)"
    echo "Plik:    $output"
}
