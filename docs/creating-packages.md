# Creating WARP Packages

## Package Structure

A `.wrp` file is a `tar.xz` archive with the following layout:

```
mypkg-1.0.0-x86_64.wrp
├── WARPINFO          # required — package metadata
├── DEPS              # required — dependency list (can be empty)
├── INSTALL           # optional — runs before file copy
├── REMOVE            # optional — runs on package removal
└── files/            # required — files to install
    ├── usr/
    │   └── bin/
    │       └── mypkg
    ├── etc/
    │   └── mypkg.conf
    └── ...
```

---

## WARPINFO

Plain key=value metadata file:

```
name=mypkg
version=1.0.0
arch=x86_64
deps=glibc,libfoo
maintainer=Marcin
license=MIT
description=My example package
```

| Field | Required | Description |
|---|---|---|
| `name` | yes | Package name (lowercase, no spaces) |
| `version` | yes | Package version |
| `arch` | yes | Target architecture (`x86_64`, `aarch64`, `any`) |
| `deps` | yes | Comma-separated list of dependencies (can be empty) |
| `maintainer` | no | Maintainer name or email |
| `license` | no | SPDX license identifier |
| `description` | no | One-line description |

---

## DEPS

One line, comma-separated list of package names that must be installed before this package:

```
glibc,gtk3,dbus
```

Leave the file empty if the package has no dependencies:

```
(empty file)
```

---

## INSTALL Script

A Bash script that runs as root **before** files are copied. Use it for pre-install checks or creating users/groups:

```bash
#!/bin/bash
# Create config directory
mkdir -p /etc/mypkg

# Create dedicated user
id mypkg &>/dev/null || useradd -r -s /sbin/nologin mypkg
```

Exit code must be `0` — a non-zero exit aborts the installation.

---

## REMOVE Script

A Bash script that runs on `warp -D`. Use it to clean up runtime state (not files — WARP removes those automatically via `FILES`):

```bash
#!/bin/bash
# Stop service if running
rc-service mypkg stop 2>/dev/null || true

# Remove runtime data (logs, cache — not config)
rm -rf /var/run/mypkg
```

---

## Building a Package

### Method 1 — Manual

1. Create the folder structure:

```bash
mkdir -p mypkg-1.0.0/files/usr/bin
cp mybinary mypkg-1.0.0/files/usr/bin/mypkg

cat > mypkg-1.0.0/WARPINFO <<EOF
name=mypkg
version=1.0.0
arch=x86_64
deps=glibc
maintainer=Marcin
license=MIT
EOF

echo "glibc" > mypkg-1.0.0/DEPS
```

2. Pack it:

```bash
tar -cJf mypkg-1.0.0-x86_64.wrp -C mypkg-1.0.0 .
```

### Method 2 — warp -cP (recommended)

```bash
warp -cP mypkg-1.0.0/
```

WARP will:
- Auto-detect dependencies using `ldd` on every binary in `files/`
- Write `DEPS` automatically
- Create `WARPINFO` from a prompt (or from an existing one)
- Pack everything into `<name>-<version>-<arch>.wrp`

---

## Auto-detecting Dependencies

When using `warp -cP`, WARP runs `ldd` on every ELF binary it finds and maps shared libraries to package names:

```
$ ldd files/usr/bin/firefox
    libgtk-3.so.0 → gtk3
    libdbus-1.so.3 → dbus
    libc.so.6 → glibc
```

The detected package names are written to `DEPS`. You can edit the file manually afterwards to add or remove entries.

---

## Naming Convention

```
<name>-<version>-<arch>.wrp
```

Examples:
```
firefox-92.0-x86_64.wrp
gtk3-3.24.0-x86_64.wrp
mylib-1.2.3-any.wrp        # arch=any for scripts/data
```

---

## Verifying a Package

```bash
warp --verify firefox-92.0-x86_64.wrp
```

Checks:
- Archive integrity (can it be opened?)
- Presence of `WARPINFO` and `DEPS`
- `WARPINFO` has all required fields
- `files/` directory exists and is non-empty
- SHA256 checksum matches repo index (if synced)
