# Configuration

WARP reads its configuration from `/etc/warp/warp.conf`. If the file doesn't exist, built-in defaults are used.

---

## Full Config Reference

```ini
[core]
# Interface language (en / pl)
language=en

# Enable ANSI colors in output
color=true

# Ask for confirmation before installing/removing
confirm=true

[network]
# Default repository URL
repo=https://repo.flow.org/core

# Number of mirror servers to try on failure
mirrors=3

# HTTP request timeout in seconds
timeout=30

[output]
# Suppress all output except final result (same as -q flag)
quiet=false

# Show apt-style progress bar during downloads/installs
progress_bar=true

# Show summary (package count, total size, time) after operations
summary=true

[cache]
# Directory for downloaded packages and repo index
dir=/var/cache/warp

# How many days to keep cached .wrp files before auto-cleanup
keep_days=7
```

---

## Environment Variables

Environment variables override the config file:

| Variable | Description |
|---|---|
| `WARP_DB` | Override package database path |
| `WARP_CONF` | Override config file path |

Example:

```bash
# Use a test database without touching the system one
WARP_DB=/tmp/test-db warp -gP mypkg.wrp
```

---

## Per-user Config

Not yet implemented. All configuration is system-wide via `/etc/warp/warp.conf`.

---

## Default Values

| Key | Default |
|---|---|
| `language` | `en` |
| `color` | `true` |
| `confirm` | `true` |
| `repo` | `https://repo.flow.org/core` |
| `mirrors` | `3` |
| `timeout` | `30` |
| `quiet` | `false` |
| `progress_bar` | `true` |
| `summary` | `true` |
| `cache.dir` | `/var/cache/warp` |
| `keep_days` | `7` |
