#!/usr/bin/env bash
# Config parser — reads INI-style warp.conf

WARP_CONF="${WARP_CONF:-/etc/warp/warp.conf}"
WARP_CONF_DEFAULT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/conf/warp.conf"

# Defaults (used when config is missing or key not set)
CFG_LANGUAGE="en"
CFG_COLOR="true"
CFG_CONFIRM="true"
CFG_REPO="https://repo.flow.org/core"
CFG_MIRRORS=3
CFG_TIMEOUT=30
CFG_QUIET="false"
CFG_PROGRESS_BAR="true"
CFG_SUMMARY="true"
CFG_CACHE_DIR="/var/cache/warp"
CFG_CACHE_KEEP_DAYS=7

config_load() {
    local conf_file="$WARP_CONF"

    # Fall back to bundled default if system config missing
    [[ -f "$conf_file" ]] || conf_file="$WARP_CONF_DEFAULT"
    [[ -f "$conf_file" ]] || return 0

    local section=""
    while IFS= read -r line || [[ -n "$line" ]]; do
        # Strip comments and whitespace
        line="${line%%#*}"
        line="${line//  / }"
        line="${line# }"
        line="${line% }"
        [[ -z "$line" ]] && continue

        # Section header
        if [[ "$line" =~ ^\[(.+)\]$ ]]; then
            section="${BASH_REMATCH[1]}"
            continue
        fi

        # Key=value
        if [[ "$line" =~ ^([^=]+)=(.*)$ ]]; then
            local key="${BASH_REMATCH[1]}"
            local val="${BASH_REMATCH[2]}"
            key="${key% }"
            val="${val# }"

            case "${section}.${key}" in
                core.language)      CFG_LANGUAGE="$val" ;;
                core.color)         CFG_COLOR="$val" ;;
                core.confirm)       CFG_CONFIRM="$val" ;;
                network.repo)       CFG_REPO="$val" ;;
                network.mirrors)    CFG_MIRRORS="$val" ;;
                network.timeout)    CFG_TIMEOUT="$val" ;;
                output.quiet)       CFG_QUIET="$val" ;;
                output.progress_bar) CFG_PROGRESS_BAR="$val" ;;
                output.summary)     CFG_SUMMARY="$val" ;;
                cache.dir)          CFG_CACHE_DIR="$val" ;;
                cache.keep_days)    CFG_CACHE_KEEP_DAYS="$val" ;;
            esac
        fi
    done < "$conf_file"

    # Apply config to runtime vars
    [[ "$CFG_QUIET" == "true" ]]        && QUIET=1
    [[ "$CFG_COLOR" == "false" ]]       && _disable_colors
    [[ "$CFG_PROGRESS_BAR" == "false" ]] && PROGRESS_BAR=0
    WARP_DB="${WARP_DB:-$CFG_CACHE_DIR/db}"
}

_disable_colors() {
    RED="" GREEN="" YELLOW="" BOLD="" RESET=""
    BG_MAGENTA="" FG_BLACK=""
}
