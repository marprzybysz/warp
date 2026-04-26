#!/usr/bin/env bash
# TUI — kolory, logi, pasek postępu

export LANG=C.UTF-8
export LC_ALL=C.UTF-8

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BOLD='\033[1m'
RESET='\033[0m'
BG_MAGENTA='\033[45m'
FG_BLACK='\033[30m'

QUIET=0

log_step() {
    [[ $QUIET -eq 1 ]] && return
    local msg="$1"
    local status="${2:-}"
    case "$status" in
        ok)   printf "%-45s ${GREEN}✓${RESET}\n" "$msg" ;;
        err)  printf "%-45s ${RED}✗${RESET}\n" "$msg" ;;
        warn) printf "  ${YELLOW}⚠ %s${RESET}\n" "$msg" ;;
        *)    printf "%s\n" "$msg" ;;
    esac
}

progress_bar() {
    [[ $QUIET -eq 1 ]] && return
    local percent="$1"
    local action="$2"
    local term_width
    term_width=$(tput cols 2>/dev/null || echo 80)
    # Format: "Progress:75% [######          ]"
    local prefix="Progress:${percent}% "
    local bar_width=$(( term_width - ${#prefix} - 3 ))
    [[ $bar_width -lt 10 ]] && bar_width=10
    local filled=$(( percent * bar_width / 100 ))
    [[ $filled -lt 0 ]] && filled=0
    [[ $filled -gt $bar_width ]] && filled=$bar_width
    local bar=""
    local i
    for (( i=0; i<filled; i++ )); do bar+="#"; done
    local empty=""
    for (( i=filled; i<bar_width; i++ )); do empty+=" "; done
    printf "\r%s[%s%s]" "$prefix" "$bar" "$empty"
}

clear_progress() {
    [[ $QUIET -eq 1 ]] && return
    local term_width
    term_width=$(tput cols 2>/dev/null || echo 80)
    printf "\r%*s\r" "$term_width" ""
}

done_ok() {
    echo -e "${GREEN}${BOLD}Gotowe${RESET}"
}

done_err() {
    echo -e "${RED}ERROR: $1${RESET}" >&2
    exit 1
}

warn() {
    [[ $QUIET -eq 1 ]] && return
    echo -e "${YELLOW}⚠ $1${RESET}"
}
