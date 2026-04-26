#!/usr/bin/env bash
# warp -build — build a .wrp package from source code

build_from_source() {
    local build_dir="$1"
    local install_after="${2:-0}"

    [[ -z "$build_dir" ]] && done_err "Provide a directory containing WARPBUILD"
    [[ -d "$build_dir" ]] || done_err "Directory not found: $build_dir"

    local warpbuild="$build_dir/WARPBUILD"
    [[ -f "$warpbuild" ]] || done_err "WARPBUILD not found in $build_dir"

    log_step "Reading WARPBUILD..."

    # Read metadata fields
    local pkgname pkgver arch deps makedeps license description source sha256
    pkgname=$(grep '^pkgname='     "$warpbuild" | cut -d= -f2-)
    pkgver=$(grep '^pkgver='       "$warpbuild" | cut -d= -f2-)
    arch=$(grep '^arch='           "$warpbuild" | cut -d= -f2-)
    deps=$(grep '^deps='           "$warpbuild" | cut -d= -f2-)
    makedeps=$(grep '^makedeps='   "$warpbuild" | cut -d= -f2-)
    license=$(grep '^license='     "$warpbuild" | cut -d= -f2-)
    description=$(grep '^description=' "$warpbuild" | cut -d= -f2-)
    source=$(grep '^source='       "$warpbuild" | cut -d= -f2-)
    sha256=$(grep '^sha256='       "$warpbuild" | cut -d= -f2-)
    arch="${arch:-x86_64}"

    [[ -z "$pkgname" || -z "$pkgver" ]] && done_err "pkgname and pkgver are required in WARPBUILD"
    log_step "Package: $pkgname $pkgver" ok

    # Check makedeps
    if [[ -n "$makedeps" ]]; then
        log_step "Checking build dependencies..."
        local missing_make=()
        IFS=',' read -ra md_list <<< "$makedeps"
        for md in "${md_list[@]}"; do
            md="${md// /}"
            command -v "$md" &>/dev/null || missing_make+=("$md")
        done
        if [[ ${#missing_make[@]} -gt 0 ]]; then
            warn "Missing build deps: ${missing_make[*]}"
            warn "Install them first: warp -G ${missing_make[*]}"
            done_err "Cannot build without required build dependencies"
        fi
        log_step "Build dependencies satisfied..." ok
    fi

    # Workspace
    local workdir
    workdir=$(mktemp -d /tmp/warp-build-src.XXXXXX)
    local srcdir="$workdir/src"
    local destdir="$workdir/dest"
    mkdir -p "$srcdir" "$destdir"

    # Download or copy source
    if [[ -n "$source" ]]; then
        if [[ "$source" =~ ^https?:// ]]; then
            log_step "Downloading source..."
            local tarball="$workdir/$(basename "$source")"
            if command -v curl &>/dev/null; then
                curl -fsSL --max-time "${CFG_TIMEOUT:-60}" -o "$tarball" "$source" \
                    || { rm -rf "$workdir"; done_err "Failed to download: $source"; }
            elif command -v wget &>/dev/null; then
                wget -q --timeout="${CFG_TIMEOUT:-60}" -O "$tarball" "$source" \
                    || { rm -rf "$workdir"; done_err "Failed to download: $source"; }
            fi
            log_step "Source downloaded..." ok

            # Verify SHA256
            if [[ -n "$sha256" ]]; then
                log_step "Verifying checksum..."
                local actual
                actual=$(sha256sum "$tarball" | cut -d' ' -f1)
                [[ "$actual" == "$sha256" ]] || {
                    rm -rf "$workdir"
                    done_err "SHA256 mismatch — corrupted source"
                }
                log_step "SHA256 OK..." ok
            fi

            # Extract
            log_step "Extracting source..."
            tar -xf "$tarball" -C "$srcdir" --strip-components=1 2>/dev/null \
                || tar -xf "$tarball" -C "$srcdir" 2>/dev/null \
                || { rm -rf "$workdir"; done_err "Failed to extract source"; }
            log_step "Source extracted..." ok
        else
            # Local source folder or tarball
            if [[ -d "$build_dir/$source" ]]; then
                cp -a "$build_dir/$source/." "$srcdir/"
            elif [[ -f "$build_dir/$source" ]]; then
                tar -xf "$build_dir/$source" -C "$srcdir" --strip-components=1 2>/dev/null \
                    || tar -xf "$build_dir/$source" -C "$srcdir" 2>/dev/null
            else
                rm -rf "$workdir"
                done_err "Source not found: $source"
            fi
            log_step "Source ready..." ok
        fi
    else
        # No source field — use build_dir itself as source
        cp -a "$build_dir/." "$srcdir/"
        log_step "Using local directory as source..." ok
    fi

    # Source the WARPBUILD functions
    # shellcheck disable=SC1090
    source "$warpbuild" 2>/dev/null || true

    # Run build() function
    if declare -f build &>/dev/null; then
        log_step "Building..."
        (
            cd "$srcdir"
            export DESTDIR="$destdir"
            export PREFIX="/usr"
            export MAKEFLAGS="-j$(nproc)"
            build
        ) || { rm -rf "$workdir"; done_err "build() function failed"; }
        log_step "Build complete..." ok
    else
        warn "No build() function in WARPBUILD — skipping compilation"
    fi

    # Run package() function
    if declare -f package &>/dev/null; then
        log_step "Staging files..."
        (
            cd "$srcdir"
            export DESTDIR="$destdir"
            export PREFIX="/usr"
            package
        ) || { rm -rf "$workdir"; done_err "package() function failed"; }
        log_step "Files staged to DESTDIR..." ok
    else
        warn "No package() function in WARPBUILD — using DESTDIR as-is"
    fi

    # Build .wrp from destdir
    log_step "Creating package structure..."
    local pkg_staging="$workdir/pkg"
    mkdir -p "$pkg_staging/files"
    cp -a "$destdir/." "$pkg_staging/files/"

    cat > "$pkg_staging/WARPINFO" <<EOF
name=$pkgname
version=$pkgver
arch=$arch
deps=$deps
license=$license
description=$description
EOF
    echo "$deps" > "$pkg_staging/DEPS"

    # Copy INSTALL/REMOVE scripts if present
    [[ -f "$build_dir/INSTALL" ]] && cp "$build_dir/INSTALL" "$pkg_staging/INSTALL"
    [[ -f "$build_dir/REMOVE" ]]  && cp "$build_dir/REMOVE"  "$pkg_staging/REMOVE"

    log_step "Packing .wrp..."
    local output="${pkgname}-${pkgver}-${arch}.wrp"
    tar -cJf "$output" -C "$pkg_staging" . 2>/dev/null \
        || { rm -rf "$workdir"; done_err "Failed to pack .wrp"; }

    local sha256_out
    sha256_out=$(sha256sum "$output" | cut -d' ' -f1)
    echo "$sha256_out  $output" > "${output}.sha256"

    log_step "Package ready: $output" ok
    log_step "SHA256: ${sha256_out:0:16}..." ok

    rm -rf "$workdir"

    echo ""
    echo "Size: $(du -sh "$output" | cut -f1)"
    echo "File: $output"
    echo ""

    # Install immediately if requested
    if [[ "$install_after" == "1" ]]; then
        log_step "Installing $pkgname..."
        install_warp_pkg "$output"
    fi
}
