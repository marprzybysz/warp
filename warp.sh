#!/usr/bin/env bash
# WARP Package Manager — prototyp bash
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
source "$SCRIPT_DIR/lib/repo.sh"

config_load

usage() {
    cat <<EOF
Usage: warp [options] [package/file]

Package management:
  -G  <pkg>          Install from repository
  -gP <file|folder>  Install locally (.warp or .tar.xz)
  -D  <pkg>          Remove package
  -DD <pkg>          Remove package and its dependencies
  -DC <pkg>          Remove cached files for package
  -DA <pkg>          Remove package, dependencies, and cache

Updates:
  --sync             Synchronize repository index
  -U  <pkg>          Update a specific package
  -AU                Update all installed packages
  -LU                List available updates

Query:
  -Q                 List all installed packages
  -Qi <pkg>          Show detailed package info
  -Qe                List explicitly installed packages
  -Qd                List dependency-only packages
  -Qo <file>         Which package owns a file?
  -ls <query>        Search repository for packages

Building:
  -cP <folder>       Build a .warp package from folder
  --verify <file>    Verify checksum of a .warp file
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
    [[ -z "$target" ]] && done_err "Podaj ścieżkę do pliku lub folderu"
    [[ -e "$target" ]] || done_err "Nie znaleziono: $target"

    local fmt
    fmt=$(detect_format "$target")

    case "$fmt" in
        warp)   install_warp_pkg "$target" ;;
        tarxz)  install_tarxz_pkg "$target" ;;
        unknown) done_err "Nieznany format: $target" ;;
    esac

    echo ""
    done_ok
}

cmd_remove() {
    local name="$1"
    local with_deps="${2:-0}"
    [[ -z "$name" ]] && done_err "Podaj nazwę pakietu"
    remove_pkg "$name" "$with_deps"
    echo ""
    done_ok
}

cmd_list() {
    local result
    result=$(db_list)
    if [[ -z "$result" ]]; then
        echo "Brak zainstalowanych pakietów."
    else
        printf "%-30s %s\n" "PAKIET" "WERSJA"
        printf "%-30s %s\n" "------" "------"
        echo "$result"
    fi
}

cmd_info() {
    local name="$1"
    [[ -z "$name" ]] && done_err "Podaj nazwę pakietu"
    db_exists "$name" || done_err "Pakiet '$name' nie jest zainstalowany"
    db_get_info "$name"
}

cmd_owner() {
    local file="$1"
    [[ -z "$file" ]] && done_err "Podaj ścieżkę do pliku"
    local owner
    owner=$(db_owner "$file")
    if [[ -n "$owner" ]]; then
        echo "$file → $owner"
    else
        echo -e "${YELLOW}Żaden pakiet nie posiada pliku: $file${RESET}"
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
    echo "Pakiety: $count zainstalowanych"
}

# --- Parsowanie argumentów ---

[[ $# -eq 0 ]] && { usage; exit 0; }

# Tryb cichy
for arg in "$@"; do
    [[ "$arg" == "-q" ]] && QUIET=1
done

case "$1" in
    -gP)     cmd_install_local "$2" ;;
    -G)      repo_install "$2" ;;
    -D)      cmd_remove "$2" 0 ;;
    -DD)     cmd_remove "$2" 1 ;;
    -Q)      cmd_list ;;
    -Qi)     cmd_info "$2" ;;
    -Qo)     cmd_owner "$2" ;;
    -ls)     repo_search "$2" ;;
    -cP)     build_pkg "$2" && echo "" && done_ok ;;
    --sync)  repo_sync && echo "" && done_ok ;;
    -info)   cmd_sysinfo ;;
    -help|--help|-h) usage ;;
    -q)      usage ;;
    *)       echo -e "${RED}Unknown option: $1${RESET}"; usage; exit 1 ;;
esac
