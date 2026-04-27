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

## Method 3 — warp -build (build from source)

`warp -build` compiles software from source and produces a `.wrp` automatically. It requires a `WARPBUILD` file in the source directory — similar to Arch Linux's `PKGBUILD`.

### WARPBUILD structure

```bash
# Package metadata
pkgname=mypkg
pkgver=1.0.0
arch=x86_64
deps=glibc,libfoo
makedeps=gcc,make
license=MIT
description=My example package

# Source (URL or local path)
source=https://example.com/mypkg-1.0.0.tar.gz
sha256=abc123...

# Build function — runs inside extracted source directory
build() {
    ./configure --prefix=/usr \
                --sysconfdir=/etc \
                --localstatedir=/var
    make
}

# Package function — copies built files to DESTDIR
package() {
    make DESTDIR="$DESTDIR" install
}
```

### Metadata fields

| Field | Required | Description |
|---|---|---|
| `pkgname` | yes | Package name (lowercase, no spaces) |
| `pkgver` | yes | Package version |
| `arch` | no | Target architecture (default: `x86_64`) |
| `deps` | no | Runtime dependencies, comma-separated |
| `makedeps` | no | Build-time dependencies (not installed into package) |
| `license` | no | SPDX license identifier |
| `description` | no | One-line description |
| `source` | no | URL or local path to source tarball/folder |
| `sha256` | no | SHA256 checksum of the source tarball |

### Environment variables available in build() and package()

| Variable | Value |
|---|---|
| `DESTDIR` | Staging directory — install files here, not to `/` |
| `PREFIX` | `/usr` |
| `MAKEFLAGS` | `-j<nproc>` — parallel build jobs |

### Full example — building `hello` from GNU sources

```bash
# hello-2.12/WARPBUILD

pkgname=hello
pkgver=2.12
arch=x86_64
deps=glibc
makedeps=gcc,make
license=GPL-3.0
description=GNU Hello — prints a greeting
source=https://ftp.gnu.org/gnu/hello/hello-2.12.tar.gz
sha256=cf04af86dc085268c5f4470fbae49b18afbc221b78096aab842d934a76bad0ab

build() {
    ./configure --prefix=/usr
    make
}

package() {
    make DESTDIR="$DESTDIR" install
}
```

```bash
# Build the package
warp -build hello-2.12/

# Output: hello-2.12-x86_64.wrp

# Build and install immediately
warp -buildI hello-2.12/
```

### What warp -build does step by step

1. Reads `WARPBUILD` metadata
2. Checks `makedeps` — aborts if any are missing
3. Downloads and verifies source (SHA256)
4. Extracts source to a temporary workspace
5. Runs `build()` inside the source directory
6. Runs `package()` — files land in `DESTDIR`
7. Copies `DESTDIR` contents into `files/`
8. Writes `WARPINFO` and `DEPS` from metadata
9. Copies `INSTALL`/`REMOVE` scripts if present in the build directory
10. Packs everything into `<name>-<version>-<arch>.wrp`
11. Generates `.sha256` checksum file
12. Cleans up temporary workspace

### Optional INSTALL and REMOVE scripts

Place an `INSTALL` or `REMOVE` script next to `WARPBUILD` — they are automatically included in the `.wrp`:

```
hello-2.12/
├── WARPBUILD
├── INSTALL      # optional pre-install hook
└── REMOVE       # optional pre-remove hook
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
