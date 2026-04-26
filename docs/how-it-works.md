# How WARP Works

## Architecture

WARP is written in C++ (with a Bash prototype for development). It operates entirely in userspace and requires root only when writing to system directories.

```
┌─────────────────────────────────┐
│           warp CLI              │
├────────────┬────────────────────┤
│  TUI layer │  Config loader     │
├────────────┴────────────────────┤
│         Core logic              │
│  install / remove / query       │
├─────────────────────────────────┤
│       Package database          │
│      /var/lib/warp/db/          │
├─────────────────────────────────┤
│        libarchive               │
│   (.wrp and .tar.xz I/O)       │
└─────────────────────────────────┘
```

---

## Package Database

Every installed package gets a record in `/var/lib/warp/db/<name>/`:

```
/var/lib/warp/db/firefox/
├── WARPINFO      # name, version, install date, source
├── FILES         # list of every installed file path
└── SOURCE        # "warp" | "tarxz" | "repo"
```

`FILES` is what makes removal safe — WARP reads it line by line and deletes exactly what it installed, nothing more.

---

## Format Detection

When you run `warp -i <file>`, WARP determines the format automatically:

```
Is extension .wrp?
  └─ Yes → full .wrp mode
  └─ No  → peek inside the archive for WARPINFO
             ├─ Found → treat as .wrp
             └─ Not found → raw .tar.xz mode
```

---

## Installation Flow (.wrp)

```
1. Extract archive to /tmp/warp.<pid>/
2. Read WARPINFO → name, version, arch
3. Read DEPS → check if dependencies are installed
4. Run INSTALL script (if present) as root
5. Copy files/ → / (progress bar)
6. Write record to /var/lib/warp/db/<name>/
7. Clean up /tmp/warp.<pid>/
```

## Installation Flow (raw .tar.xz)

```
1. Extract archive to /tmp/warp.<pid>/
2. Warn: "No WARPINFO — raw mode"
3. Copy all files → / (preserving paths from archive)
4. Write synthetic record to /var/lib/warp/db/<name>/
   - name inferred from filename (e.g. firefox-92.tar.xz → firefox)
   - version inferred from filename (e.g. 92)
   - source = "tarxz"
5. Clean up /tmp/warp.<pid>/
```

Raw mode has no dependency resolution or install scripts. It tracks files so `warp -D` can still remove the package cleanly.

---

## Removal Flow

```
1. Read /var/lib/warp/db/<name>/FILES
2. Delete each file listed (progress bar)
3. Remove empty parent directories
4. Delete /var/lib/warp/db/<name>/
```

---

## Locale

WARP sets `LC_ALL=C.UTF-8` on startup. This ensures consistent behavior and correct UTF-8 rendering of progress indicators (`✓`, `⚠`, etc.) regardless of the host locale.
