# WARP — Warp Archive Repository Packager

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
warp -i firefox-92.0-x86_64.wrp

# Install a raw archive
warp -i mytool-1.0.tar.xz

# Remove a package
warp -D firefox

# List installed packages
warp -A

# Show package info
warp -s firefox
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

---

## Repository Policy

The official Flow Linux repository is **curated and verified**. Every package that lands in the repo goes through the following process:

1. Package is submitted by the community or a maintainer
2. A maintainer reviews the `WARPBUILD` / package contents
3. The package is compiled in a clean environment
4. SHA256 checksum is generated and added to the INDEX
5. The package is published

**Community members cannot push packages directly.** If you want a package added to the official repo, open an issue or pull request on GitHub with your `WARPBUILD` file. Maintainers will review and publish it.

This keeps the repository safe — every package in the repo has been seen by a human before it reaches your system.

---

## Contributing a Package

1. Write a `WARPBUILD` for the software you want packaged (see [Creating Packages](creating-packages.md))
2. Test it locally with `warp -build <folder>` and `warp -i <file.wrp>`
3. Open an issue on the Flow Linux GitHub with the `WARPBUILD` attached
4. A maintainer will review, build, and publish it

If you want to become a maintainer, reach out on GitHub.
