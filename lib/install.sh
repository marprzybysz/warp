#!/usr/bin/env bash
# Install logic

INSTALL_PREFIX="${INSTALL_PREFIX:-}"

install_warp_pkg() {
    local file="$1"

    log_step "Detecting format..." ok
    log_step "Extracting to temp directory..."

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Cannot extract archive: $file"

    local info="$tmpdir/WARPINFO"
    [[ -f "$info" ]] || { rm -rf "$tmpdir"; done_err "WARPINFO missing from package"; }

    local name version
    name=$(grep '^name=' "$info" | cut -d= -f2)
    version=$(grep '^version=' "$info" | cut -d= -f2)
    log_step "Reading metadata ($name $version)..." ok

    local deps_file="$tmpdir/DEPS"
    if [[ -f "$deps_file" ]]; then
        local deps
        deps=$(cat "$deps_file")
        if [[ -n "$deps" ]]; then
            log_step "Checking dependencies..."
            local missing=()
            while IFS=',' read -ra dep_list; do
                for dep in "${dep_list[@]}"; do
                    dep="${dep// /}"
                    [[ -z "$dep" ]] && continue
                    db_exists "$dep" || missing+=("$dep")
                done
            done <<< "$deps"
            if [[ ${#missing[@]} -gt 0 ]]; then
                warn "Missing dependencies: ${missing[*]}"
                warn "Install them manually or use warp -G"
            else
                log_step "Dependencies satisfied..." ok
            fi
        fi
    fi

    if [[ -f "$tmpdir/INSTALL" ]]; then
        log_step "Running install script..."
        chmod +x "$tmpdir/INSTALL"
        (cd "$tmpdir" && bash INSTALL) || { rm -rf "$tmpdir"; done_err "INSTALL script failed"; }
        log_step "Install script..." ok
    fi

    if [[ -d "$tmpdir/files" ]]; then
        local dest="${INSTALL_PREFIX:-/}"
        log_step "Installing files to $dest..."

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
            progress_bar "$pct" "Installing $name"
        done <<< "$all_files"

        clear_progress
        log_step "Copying files..." ok

        db_init
        db_save "$name" "$version" "warp" "$all_files"
    else
        db_init
        db_save "$name" "$version" "warp"
    fi

    log_step "Registering package..." ok
    db_log "install" "$name" "$version"
    rm -rf "$tmpdir"
}

install_tarxz_pkg() {
    local file="$1"

    log_step "Detecting format..." ok
    warn "No WARPINFO — raw mode"
    [[ $QUIET -eq 0 ]] && echo ""

    local name version
    name=$(name_from_file "$file")
    version=$(version_from_file "$file")

    log_step "Extracting $name..."

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Cannot extract archive: $file"

    local all_files
    all_files=$(find "$tmpdir" -type f | sed "s|$tmpdir||")
    local total
    total=$(echo "$all_files" | wc -l)
    local count=0

    local dest="/"
    while IFS= read -r rel_path; do
        [[ -z "$rel_path" ]] && continue
        local src="$tmpdir$rel_path"
        local dst="${dest%/}$rel_path"
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
        count=$(( count + 1 ))
        local pct=$(( count * 100 / total ))
        progress_bar "$pct" "Extracting"
    done <<< "$all_files"

    clear_progress
    log_step "Extraction..." ok

    local installed_files
    installed_files=$(echo "$all_files" | sed "s|^|/|" | sed 's|//|/|g')

    db_init
    db_save "$name" "$version" "tarxz" "$installed_files"

    log_step "Registering package..." ok
    db_log "install" "$name" "$version"
    if [[ $QUIET -eq 0 ]]; then
        echo ""
        echo "Installed to: / (paths from archive)"
    fi
}
