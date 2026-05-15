# Commands Reference

## Package Management

| Command | Description |
|---|---|
| `warp -G <pkg> [pkg...]` | Install one or more packages from repository |
| `warp -i <file.wrp>` | Install from a local `.wrp` file |
| `warp -i <file.tar.xz>` | Install from a raw `.tar.xz` archive |
| `warp -i <folder/>` | Install from a local folder (auto-builds `.wrp`) |
| `warp -D <pkg> [pkg...]` | Remove one or more packages |
| `warp -DD <pkg> [pkg...]` | Remove packages and their dependencies |
| `warp -DC <pkg>` | Remove cached files for a package |
| `warp -DA <pkg>` | Remove package, dependencies, and cache |

## Updates

| Command | Description |
|---|---|
| `warp --sync` | Synchronize repository index, scan system packages, list updates |
| `warp -U` | Upgrade all installed packages |
| `warp -LU` | List available updates |

## Query

| Command | Description |
|---|---|
| `warp -A` | List all installed packages (colored, built-in pager) |
| `warp -Q` | Print installed package count (for scripts/fetch tools) |
| `warp -s <pkg>` | Show detailed info about a package (installed or in repo) |
| `warp -S <file>` | Show which package owns a given file |
| `warp -ls <query>` | Search repository for packages |

## Building Packages

| Command | Description |
|---|---|
| `warp -cP <folder>` | Build a `.wrp` package from a folder |
| `warp -build <folder>` | Build a `.wrp` from source (uses WARPBUILD) |
| `warp -buildI <folder>` | Build from source and install immediately |
| `warp --verify <file>` | Verify checksum of a `.wrp` file |
| `warp --push <file>` | Upload package to configured repository |

## Repositories

| Command | Description |
|---|---|
| `warp repo --list` | List configured repositories |
| `warp repo --add <url>` | Add a repository |
| `warp repo --remove <n>` | Remove a repository by number |
| `warp repo --gen-index <dir>` | Generate INDEX from a directory of `.wrp` files |

## Diagnostics

| Command | Description |
|---|---|
| `warp --autoremove` | Remove packages no longer needed as dependencies |
| `warp --clean-cache` | Remove all cached package files |
| `warp --fix` | Repair broken dependencies |
| `warp --check` | Verify integrity of installed packages |
| `warp --orphans` | List orphaned packages |
| `warp --log` | Show operation history |
| `warp --rollback <pkg>` | Revert a package to its previous version |

## Protection

| Command | Description |
|---|---|
| `warp protect <pkg>` | Add package to protected list (blocks remove/upgrade) |
| `warp unprotect <pkg>` | Remove package from protected list |
| `warp protected` | List all protected packages |

## System

| Command | Description |
|---|---|
| `warp -v` / `warp --version` | Show version, copyright, and license |
| `warp -info` | Show WARP version, arch, kernel, and package count |
| `warp -help` | Show usage summary |

---

## Output Modifiers

Append `v` or `q` to any short command to control verbosity:

| Modifier | Effect | Example |
|---|---|---|
| `v` | Verbose — full install detail | `warp -iv pkg.wrp`, `warp -Gv firefox` |
| `q` | Quiet — errors only | `warp -iq pkg.wrp`, `warp -Gq firefox` |


**Verbose output** shows each dependency status, file count, and registration step:
```
  ◆ tar 1.35  [1.17 MB]
  Scanning system libraries...
  ↳ glibc — satisfied
  Installing 47 files...
  Registering tar in database...
Done!
```

**Normal output** (default) shows a compact summary:
```
  ◆ tar 1.35  [1.17 MB]
  ↳ 1 dependency satisfied
  ↳ 47 files installed
Done!
```

**Quiet output** shows only the progress bar and errors.

---

## Package List Colors (warp -A)

When outputting to a terminal, `warp -A` uses colors to distinguish package types:

| Color | Type |
|---|---|
| Green | Application packages |
| Yellow | Library packages (name starts with `lib`) |
| Dim | System packages (detected via `--sync`) |

When piped (e.g. `warp -A | grep foo`), colors are automatically disabled and the built-in pager is skipped.
