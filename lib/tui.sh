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
_PROGRESS_PCT=-1  # -1 = no active progress bar

_term_width()  { tput cols  2>/dev/null || echo 80; }
_term_height() { tput lines 2>/dev/null || echo 24; }

_draw_bar() {
    local pct="$1"
    local tw
    tw=$(_term_width)
    local prefix="Progress:${pct}% "
    local bar_width=$(( tw - ${#prefix} - 3 ))
    [[ $bar_width -lt 10 ]] && bar_width=10
    local filled=$(( pct * bar_width / 100 ))
    [[ $filled -lt 0 ]]        && filled=0
    [[ $filled -gt $bar_width ]] && filled=$bar_width
    local bar="" empty="" i
    for (( i=0; i<filled; i++ ));           do bar+="#";   done
    for (( i=filled; i<bar_width; i++ ));   do empty+=" "; done
    printf "%s[%s%s]" "$prefix" "$bar" "$empty"
}

progress_bar() {
    [[ $QUIET -eq 1 ]] && return
    local pct="$1"
    [[ $pct -lt 0 ]]   && pct=0
    [[ $pct -gt 100 ]] && pct=100
    _PROGRESS_PCT=$pct

    local th
    th=$(_term_height)
    # save cursor → go to last line → clear line → draw → restore cursor
    printf "\033[s\033[%d;1H\033[2K%s\033[u" "$th" "$(_draw_bar "$pct")"
}

_redraw_bar() {
    [[ $_PROGRESS_PCT -lt 0 ]] && return
    local th
    th=$(_term_height)
    printf "\033[s\033[%d;1H\033[2K%s\033[u" "$th" "$(_draw_bar "$_PROGRESS_PCT")"
}

_erase_bar() {
    [[ $_PROGRESS_PCT -lt 0 ]] && return
    local th
    th=$(_term_height)
    printf "\033[s\033[%d;1H\033[2K\033[u" "$th"
}

clear_progress() {
    [[ $QUIET -eq 1 ]] && return
    _erase_bar
    _PROGRESS_PCT=-1
}

log_step() {
    [[ $QUIET -eq 1 ]] && return
    local msg="$1"
    local status="${2:-}"

    _erase_bar
    case "$status" in
        ok)   printf "%-45s ${GREEN}✓${RESET}\n" "$msg" ;;
        err)  printf "%-45s ${RED}✗${RESET}\n" "$msg" ;;
        warn) printf "  ${YELLOW}⚠ %s${RESET}\n" "$msg" ;;
        *)    printf "%s\n" "$msg" ;;
    esac
    _redraw_bar
}

done_ok() {
    clear_progress
    printf "${GREEN}${BOLD}Done!${RESET}\n"
}

done_err() {
    clear_progress
    printf "${RED}ERROR: %s${RESET}\n" "$1" >&2
    exit 1
}

warn() {
    [[ $QUIET -eq 1 ]] && return
    _erase_bar
    printf "${YELLOW}⚠ %s${RESET}\n" "$1"
    _redraw_bar
}

format_size() {
    local file="$1"
    local kb
    kb=$(du -sk "$file" 2>/dev/null | cut -f1)
    if (( kb < 1024 )); then
        echo "${kb} kB"
    elif (( kb < 1048576 )); then
        local h=$(( kb * 100 / 1024 ))
        printf "%d.%02d MB" $(( h / 100 )) $(( h % 100 ))
    else
        local h=$(( kb * 100 / 1048576 ))
        printf "%d.%02d GB" $(( h / 100 )) $(( h % 100 ))
    fi
}
