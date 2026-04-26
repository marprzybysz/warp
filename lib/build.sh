#!/usr/bin/env bash
# warp -cP — build a .wrp package from a folder

build_pkg() {
    local src_dir="$1"
    [[ -z "$src_dir" ]] && done_err "Provide a source folder"
    [[ -d "$src_dir" ]] || done_err "Directory not found: $src_dir"
    src_dir="${src_dir%/}"

    log_step "Checking folder structure..."

    local files_dir="$src_dir/files"
    if [[ ! -d "$files_dir" ]]; then
        warn "No files/ subdir — treating entire folder as files/"
        local tmp_wrap
        tmp_wrap=$(mktemp -d /tmp/warp-build.XXXXXX)
        mkdir -p "$tmp_wrap/files"
        cp -a "$src_dir/." "$tmp_wrap/files/"
        src_dir="$tmp_wrap"
        files_dir="$src_dir/files"
    fi

    local info_file="$src_dir/WARPINFO"
    local name version arch maintainer license description

    if [[ -f "$info_file" ]]; then
        name=$(grep '^name='        "$info_file" | cut -d= -f2)
        version=$(grep '^version='  "$info_file" | cut -d= -f2)
        arch=$(grep '^arch='        "$info_file" | cut -d= -f2)
        maintainer=$(grep '^maintainer=' "$info_file" | cut -d= -f2)
        license=$(grep '^license='  "$info_file" | cut -d= -f2)
        description=$(grep '^description=' "$info_file" | cut -d= -f2)
        log_step "Read WARPINFO ($name $version)..." ok
    else
        log_step "No WARPINFO — fill in metadata:"
        echo ""
        read -rp "  name:        " name
        read -rp "  version:     " version
        read -rp "  arch [x86_64]: " arch;     arch="${arch:-x86_64}"
        read -rp "  maintainer:  " maintainer
        read -rp "  license:     " license
        read -rp "  description: " description
        echo ""
    fi

    [[ -z "$name" || -z "$version" ]] && done_err "name and version are required"

    log_step "Detecting dependencies (ldd)..."
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

    local deps_str=""
    if [[ ${#detected_deps[@]} -gt 0 ]]; then
        deps_str=$(printf '%s\n' "${detected_deps[@]}" | sort -u | tr '\n' ',' | sed 's/,$//')
        log_step "Detected: $deps_str" ok
    else
        log_step "No dependencies detected..." ok
    fi

    if [[ $QUIET -eq 0 ]]; then
        read -rp "  Dependencies [$deps_str]: " user_deps
        [[ -n "$user_deps" ]] && deps_str="$user_deps"
    fi

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
    log_step "Saved WARPINFO and DEPS..." ok

    local output="${name}-${version}-${arch}.wrp"
    log_step "Packing $output..."
    tar -cJf "$output" -C "$src_dir" . 2>/dev/null \
        || done_err "Error while packing"
    log_step "Package ready: $output" ok

    local sha256
    sha256=$(sha256sum "$output" | cut -d' ' -f1)
    echo "$sha256  $output" > "${output}.sha256"
    log_step "SHA256: ${sha256:0:16}..." ok

    echo ""
    echo "Size: $(du -sh "$output" | cut -f1)"
    echo "File: $output"
}
