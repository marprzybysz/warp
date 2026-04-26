# WARP Package Manager

WARP is the native package manager for **Flow Linux** — a minimal LFS-based distribution. It handles installing, removing, and querying packages in the `.wrp` format, as well as raw `.tar.xz` archives.

---

## Table of Contents

- [How It Works](how-it-works.md)
- [Commands Reference](commands.md)
- [Creating Packages](creating-packages.md)
- [Repository Format](repository.md)
- [Configuration](configuration.md)

---

## Quick Start

```bash
# Install a package from a file
warp -gP firefox-92.0-x86_64.wrp

# Install a raw archive
warp -gP mytool-1.0.tar.xz

# Remove a package
warp -D firefox

# List installed packages
warp -Q

# Show package info
warp -Qi firefox
```

---

## Package Format

WARP uses the `.wrp` format — a renamed `tar.xz` archive with a defined internal structure:

```
firefox.wrp
├── WARPINFO     # metadata
├── DEPS         # dependency list
├── INSTALL      # pre/post install script (optional)
├── REMOVE       # uninstall script (optional)
└── files/       # actual files to install
    ├── usr/
    ├── etc/
    └── ...
```

WARP also accepts plain `.tar.xz` archives (raw mode — no metadata, no dependency resolution).
