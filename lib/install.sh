#!/usr/bin/env bash
# Install logic

INSTALL_PREFIX="${INSTALL_PREFIX:-}"

install_warp_pkg() {
    local file="$1"

    local size
    size=$(format_size "$file")

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Cannot extract archive: $file"

    local info="$tmpdir/WARPINFO"
    [[ -f "$info" ]] || { rm -rf "$tmpdir"; done_err "WARPINFO missing from package"; }

    local name version
    name=$(grep '^name=' "$info" | cut -d= -f2)
    version=$(grep '^version=' "$info" | cut -d= -f2)

    [[ $QUIET -eq 0 ]] && printf "Selecting %s %s [%s]\n" "$name" "$version" "${size:-?}"

    # Step weights: extract=10, metadata=20, deps=35, install_script=50, files=50-90, register=95
    progress_bar 10 ""

    progress_bar 20 ""

    # Check deps
    local deps_file="$tmpdir/DEPS"
    if [[ -f "$deps_file" ]]; then
        local deps
        deps=$(cat "$deps_file")
        if [[ -n "$deps" ]]; then
            local missing=()
            while IFS=',' read -ra dep_list; do
                for dep in "${dep_list[@]}"; do
                    dep="${dep// /}"
                    [[ -z "$dep" ]] && continue
                    db_exists "$dep" || missing+=("$dep")
                done
            done <<< "$deps"
            if [[ ${#missing[@]} -gt 0 ]]; then
                clear_progress
                warn "Missing dependencies: ${missing[*]}"
                warn "Install them manually or use warp -G"
                [[ $QUIET -eq 0 ]] && echo ""
            fi
        fi
    fi
    progress_bar 35 ""

    # Run INSTALL script
    if [[ -f "$tmpdir/INSTALL" ]]; then
        progress_bar 45 ""
        chmod +x "$tmpdir/INSTALL"
        (cd "$tmpdir" && bash INSTALL) || { rm -rf "$tmpdir"; done_err "INSTALL script failed"; }
    fi
    progress_bar 50 ""

    # Copy files
    if [[ -d "$tmpdir/files" ]]; then
        local dest="${INSTALL_PREFIX:-/}"
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
            # file copy occupies 50-90% of total progress
            local pct=$(( 50 + count * 40 / total ))
            progress_bar "$pct" ""
        done <<< "$all_files"

        progress_bar 95 ""
        db_init
        db_save "$name" "$version" "warp" "$all_files"
    else
        progress_bar 95 ""
        db_init
        db_save "$name" "$version" "warp"
    fi

    progress_bar 100 ""
    clear_progress
    db_log "install" "$name" "$version"
    rm -rf "$tmpdir"

    [[ $QUIET -eq 0 ]] && echo -e "${GREEN}${BOLD}Done!${RESET}"
}

install_tarxz_pkg() {
    local file="$1"

    local size
    size=$(format_size "$file")

    warn "No WARPINFO — raw mode"
    [[ $QUIET -eq 0 ]] && echo ""

    local name version
    name=$(name_from_file "$file")
    version=$(version_from_file "$file")

    [[ $QUIET -eq 0 ]] && printf "Selecting %s %s [%s]\n" "$name" "$version" "${size:-?}"

    local tmpdir
    tmpdir=$(extract_to_tmp "$file") || done_err "Cannot extract archive: $file"

    progress_bar 10 ""

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
        local pct=$(( 10 + count * 80 / total ))
        progress_bar "$pct" ""
    done <<< "$all_files"

    progress_bar 95 ""

    local installed_files
    installed_files=$(echo "$all_files" | sed "s|^|/|" | sed 's|//|/|g')

    db_init
    db_save "$name" "$version" "tarxz" "$installed_files"

    progress_bar 100 ""
    clear_progress
    db_log "install" "$name" "$version"
    rm -rf "$tmpdir"

    [[ $QUIET -eq 0 ]] && echo -e "${GREEN}${BOLD}Done!${RESET}"
}
