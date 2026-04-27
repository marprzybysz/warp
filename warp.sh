#!/usr/bin/env bash
# WARP — Warp Archive Repository Packager
# Flow Linux

export LANG=C.UTF-8
export LC_ALL=C.UTF-8

WARP_VERSION="0.1.0-bash"
WARP_DB="${WARP_DB:-/var/lib/warp/db}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib/tui.sh"
source "$SCRIPT_DIR/lib/config.sh"
source "$SCRIPT_DIR/lib/db.sh"
source "$SCRIPT_DIR/lib/format.sh"
source "$SCRIPT_DIR/lib/install.sh"
source "$SCRIPT_DIR/lib/remove.sh"
source "$SCRIPT_DIR/lib/build.sh"
source "$SCRIPT_DIR/lib/build_src.sh"
source "$SCRIPT_DIR/lib/repo.sh"
source "$SCRIPT_DIR/lib/diag.sh"

config_load

usage() {
    cat <<EOF
Usage: warp [options] [package/file]

Package management:
  -G  <pkg>          Install from repository
  -i  <file|folder>  Install locally (.wrp or .tar.xz)
  -D  <pkg>          Remove package
  -DD <pkg>          Remove package and its dependencies
  -DC <pkg>          Remove cached files for package
  -DA <pkg>          Remove package, dependencies, and cache

Updates:
  --sync             Synchronize repository index
  -U                 Upgrade all installed packages
  -LU                List available updates

Query:
  -A                 List all installed packages
  -s  <pkg>          Show detailed package info
  -S  <file>         Which package owns a file?
  -ls <query>        Search repository for packages

Building:
  -cP <folder>       Build a .wrp package from folder (binary)
  -build <folder>    Build a .wrp from source (uses WARPBUILD)
  -buildI <folder>   Build from source and install immediately
  --verify <file>    Verify checksum of a .wrp file
  --push <file>      Upload package to repository

Repositories:
  repo --list        List configured repositories
  repo --add <url>   Add a repository
  repo --remove <n>  Remove a repository
  repo --info <n>    Show repository details

Diagnostics:
  --fix              Repair broken dependencies
  --check            Verify integrity of installed packages
  --orphans          List orphaned packages
  --log              Show operation history
  --rollback <pkg>   Revert package to previous version

System:
  -info              WARP version and system info
  -help [flag]       Show help (optionally for a specific flag)
  -q                 Quiet mode
EOF
}

cmd_install_local() {
    local target="$1"
    [[ -z "$target" ]] && done_err "Provide a file or folder path"
    [[ -e "$target" ]] || done_err "Not found: $target"

    if [[ -d "$target" ]]; then
        log_step "Directory detected — building package..."
        local old_pwd="$PWD"
        cd /tmp
        build_pkg "$target" <<< ""
        local wrp_file
        wrp_file=$(ls -t /tmp/*.wrp 2>/dev/null | head -1)
        cd "$old_pwd"
        [[ -z "$wrp_file" ]] && done_err "Nie udało się zbudować paczki z folderu"
        echo ""
        install_warp_pkg "$wrp_file"
        rm -f "$wrp_file" "${wrp_file}.sha256"
        return
    fi

    local fmt
    fmt=$(detect_format "$target")

    case "$fmt" in
        warp)   install_warp_pkg "$target" ;;
        tarxz)  install_tarxz_pkg "$target" ;;
        unknown) done_err "Unknown format: $target" ;;
    esac
}

cmd_remove() {
    local name="$1"
    local with_deps="${2:-0}"
    [[ -z "$name" ]] && done_err "Provide a package name"
    remove_pkg "$name" "$with_deps"
    echo ""
    done_ok
}

cmd_list() {
    local result
    result=$(db_list)
    if [[ -z "$result" ]]; then
        echo "No packages installed."
    else
        printf "%-30s %s\n" "PACKAGE" "VERSION"
        printf "%-30s %s\n" "-------" "-------"
        echo "$result"
    fi
}

cmd_info() {
    local name="$1"
    [[ -z "$name" ]] && done_err "Provide a package name"
    db_exists "$name" || done_err "Package '$name' is not installed"
    db_get_info "$name"
}

cmd_owner() {
    local file="$1"
    [[ -z "$file" ]] && done_err "Provide a file path"
    local owner
    owner=$(db_owner "$file")
    if [[ -n "$owner" ]]; then
        echo "$file → $owner"
    else
        echo -e "${YELLOW}No package owns: $file${RESET}"
        exit 1
    fi
}

cmd_sysinfo() {
    echo "WARP Package Manager $WARP_VERSION"
    echo "Arch:    $(uname -m)"
    echo "Kernel:  $(uname -r)"
    echo "Locale:  $LANG"
    echo "DB:      $WARP_DB"
    local count=0
    [[ -d "$WARP_DB" ]] && count=$(find "$WARP_DB" -maxdepth 1 -mindepth 1 -type d | wc -l)
    echo "Packages: $count installed"
}

# --- Parsowanie argumentów ---

[[ $# -eq 0 ]] && { usage; exit 0; }

# Tryb cichy
for arg in "$@"; do
    [[ "$arg" == "-q" ]] && QUIET=1
done

case "$1" in
    -i)      cmd_install_local "$2" ;;
    -G)      shift
             pkgs=("$@"); total=${#pkgs[@]}
             if [[ $total -eq 1 ]]; then
                 repo_install "${pkgs[0]}"
             else
                 _WARP_QUEUE=1
                 for (( _qi=0; _qi<total; _qi++ )); do
                     _qn=$(( _qi+1 ))
                     echo ""
                     printf ">>> Fetching (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                     repo_install "${pkgs[$_qi]}"
                     printf ">>> Completed (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                 done
                 _WARP_QUEUE=0
                 echo ""
                 done_ok
             fi ;;
    -D)      shift
             pkgs=("$@"); total=${#pkgs[@]}
             if [[ $total -eq 1 ]]; then
                 cmd_remove "${pkgs[0]}" 0
             else
                 _WARP_QUEUE=1
                 for (( _qi=0; _qi<total; _qi++ )); do
                     _qn=$(( _qi+1 ))
                     echo ""
                     printf ">>> Removing (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                     cmd_remove "${pkgs[$_qi]}" 0
                     printf ">>> Completed (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                 done
                 _WARP_QUEUE=0
                 echo ""
                 done_ok
             fi ;;
    -DD)     shift
             pkgs=("$@"); total=${#pkgs[@]}
             if [[ $total -eq 1 ]]; then
                 cmd_remove "${pkgs[0]}" 1
             else
                 _WARP_QUEUE=1
                 for (( _qi=0; _qi<total; _qi++ )); do
                     _qn=$(( _qi+1 ))
                     echo ""
                     printf ">>> Removing (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                     cmd_remove "${pkgs[$_qi]}" 1
                     printf ">>> Completed (%d of %d) %s\n" "$_qn" "$total" "${pkgs[$_qi]}"
                 done
                 _WARP_QUEUE=0
                 echo ""
                 done_ok
             fi ;;
    -A)      cmd_list ;;
    -s)      cmd_info "$2" ;;
    -S)      cmd_owner "$2" ;;
    -ls)     repo_search "$2" ;;
    -U)      repo_upgrade ;;
    -LU)     repo_list_updates ;;
    -cP)     build_pkg "$2" && echo "" && done_ok ;;
    -build)  build_from_source "$2" 0 && done_ok ;;
    -buildI) build_from_source "$2" 1 && done_ok ;;
    --sync)  repo_sync && echo "" && done_ok ;;
    -DC)     cmd_remove_cache "$2" ;;
    -DA)     cmd_remove_all "$2" && echo "" && done_ok ;;
    --fix)   cmd_fix ;;
    --check) cmd_check ;;
    --orphans) cmd_orphans ;;
    --log)   cmd_log ;;
    --rollback) cmd_rollback "$2" ;;
    --verify) cmd_verify "$2" ;;
    --push)  cmd_push "$2" ;;
    -info)   cmd_sysinfo ;;
    -help|--help|-h) usage ;;
    -q)      usage ;;
    *)       echo -e "${RED}Unknown option: $1${RESET}"; usage; exit 1 ;;
esac
