# Repository Format

A WARP repository is a static directory served over HTTP. No special server software is required — nginx, Caddy, GitHub Pages, or even `python3 -m http.server` all work.

---

## Directory Layout

```
repo.flow.org/core/
├── REPOINFO              # repository metadata
├── INDEX                 # package list (downloaded by warp --sync)
└── packages/
    ├── firefox-92.0-x86_64.wrp
    ├── gtk3-3.24.0-x86_64.wrp
    └── ...
```

---

## REPOINFO

```
name=flow-core
description=Flow Linux core packages
maintainer=Marcin
arch=x86_64
```

---

## INDEX

One block per package, separated by blank lines:

```
[firefox]
version=92.0
arch=x86_64
deps=glibc,gtk3,dbus
file=packages/firefox-92.0-x86_64.wrp
sha256=a3f1c29d8e4b1f0c2a7d3e5f6b8c9a1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7
size=47382614

[gtk3]
version=3.24.0
arch=x86_64
deps=glibc
file=packages/gtk3-3.24.0-x86_64.wrp
sha256=b9e2d1f3a4c5e6b7d8f9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b9c0d1
size=8493012
```

### Generating INDEX

Use `warp repo --gen-index` to automatically generate an INDEX from a directory of `.wrp` files:

```bash
warp repo --gen-index /srv/repo/packages/
```

This scans all `.wrp` files, extracts their WARPINFO, and writes a complete INDEX file.

---

## Setting Up a Repository

### GitHub (recommended for small repos)

1. Create a GitHub repository (e.g. `warppkgs`)
2. Add `.wrp` files and an INDEX
3. Use raw GitHub URLs as the repo source

```bash
warp repo --add https://raw.githubusercontent.com/user/warppkgs/main
```

### Local HTTP Server (for testing)

```bash
# Create repo structure
mkdir -p ~/flow-repo/packages

# Add REPOINFO
cat > ~/flow-repo/REPOINFO <<EOF
name=flow-local
description=Local test repo
maintainer=Marcin
arch=x86_64
EOF

# Move packages in
cp firefox-92.0-x86_64.wrp ~/flow-repo/packages/

# Generate INDEX
warp repo --gen-index ~/flow-repo/

# Serve it
cd ~/flow-repo && python3 -m http.server 8080
```

Then configure WARP to use it:

```bash
warp repo --add http://localhost:8080
warp --sync
warp -G firefox
```

---

## Multiple Repositories

WARP supports multiple repos. Manage them with:

```bash
warp repo --list              # show all repos
warp repo --add <url>         # add a repo
warp repo --remove <number>   # remove by number
```

Repositories are tried in order. If the same package exists in two repos, the first repo wins.

---

## Publishing a Package

```bash
# Build
warp -cP mypkg-1.0.0/

# Verify
warp --verify mypkg-1.0.0-x86_64.wrp

# Push to repo
warp --push mypkg-1.0.0-x86_64.wrp
```

`warp --push` uploads the package to the configured repository server.
