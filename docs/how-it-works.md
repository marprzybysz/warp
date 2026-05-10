# How WARP Works

## Architecture

WARP is written in C++ and linked against libarchive and libcurl. It operates entirely in userspace and requires root only when writing to system directories.

```
┌─────────────────────────────────┐
│           warp CLI              │
├────────────┬────────────────────┤
│  TUI layer │  Config loader     │
├────────────┴────────────────────┤
│         Core logic              │
│  install / remove / query       │
│  build / repo / diagnostics     │
├─────────────────────────────────┤
│       Package database          │
│      /var/cache/warp/db/        │
├─────────────────────────────────┤
│    libarchive    │   libcurl    │
│  (.wrp/.tar.xz)  │  (downloads) │
└─────────────────────────────────┘
```

---

## Package Database

Every installed package gets a record in `/var/cache/warp/db/<name>/`:

```
/var/cache/warp/db/firefox/
├── WARPINFO      # name, version, install date, source
├── FILES         # list of every installed file path
├── DEPS          # dependency list
└── SOURCE        # "warp" | "tarxz" | "repo" | "system"
```

`FILES` is what makes removal safe — WARP reads it line by line and deletes exactly what it installed, nothing more.

The `source=system` marker is used for packages detected by `--sync` via ldconfig and pkg-config. These are system libraries that WARP tracks but did not install.

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
6. Write record to /var/cache/warp/db/<name>/
7. Clean up /tmp/warp.<pid>/
```

## Installation Flow (raw .tar.xz)

```
1. Extract archive to /tmp/warp.<pid>/
2. Warn: "No WARPINFO — raw mode"
3. Copy all files → / (preserving paths from archive)
4. Write synthetic record to /var/cache/warp/db/<name>/
   - name inferred from filename (e.g. firefox-92.tar.xz → firefox)
   - version inferred from filename (e.g. 92)
   - source = "tarxz"
5. Clean up /tmp/warp.<pid>/
```

Raw mode has no dependency resolution or install scripts. It tracks files so `warp -D` can still remove the package cleanly.

---

## Removal Flow

```
1. Read /var/cache/warp/db/<name>/FILES
2. Delete each file listed (progress bar)
3. Remove empty parent directories
4. Delete /var/cache/warp/db/<name>/
```

---

## Sync Flow (warp --sync)

`warp --sync` performs a full system synchronization in three steps:

```
1. Download INDEX from all configured repositories
2. Scan system libraries via ldconfig + pkg-config
   → registers detected packages as source=system
3. Compare installed versions against repo INDEX
   → list available updates
```

---

## Build Flow (warp -build)

```
1. Read WARPBUILD → pkgname, pkgver, source URL, sha256
2. Check makedeps — abort if any are missing
3. Download source tarball and verify SHA256
4. Extract source to temporary workspace
5. Set DESTDIR, PREFIX=/usr, MAKEFLAGS=-j<nproc>
6. Run build() inside source directory
7. Run package() → files land in DESTDIR
8. Pack DESTDIR contents into <name>-<version>-<arch>.wrp
9. Generate .sha256 checksum file
10. Clean up workspace
```

---

## Progress Bar

WARP shows a colored progress bar during install/remove operations:

```
 Extracting firefox   [████████████░░░░░░░░] 60%
```

- Cyan label (action + package name)
- Green filled blocks (█) and gray empty blocks (░)
- Automatically disabled when output is piped

---

## Locale

WARP sets `LC_ALL=C.UTF-8` on startup. This ensures consistent behavior and correct UTF-8 rendering of progress indicators (`✓`, `⚠`, `█`, `░`) regardless of the host locale.
