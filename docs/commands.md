# Commands Reference

## Package Management

| Command | Description |
|---|---|
| `warp -G <pkg>` | Install package from configured repository |
| `warp -i <file.wrp>` | Install from a local `.wrp` file |
| `warp -i <file.tar.xz>` | Install from a raw `.tar.xz` archive |
| `warp -i <folder/>` | Install from a local folder (auto-builds `.wrp`) |
| `warp -D <pkg>` | Remove a package |
| `warp -DD <pkg>` | Remove a package and its dependencies |
| `warp -DC <pkg>` | Remove cached files for a package |
| `warp -DA <pkg>` | Remove package, dependencies, and cache |

## Updates

| Command | Description |
|---|---|
| `warp --sync` | Synchronize repository index |
| `warp -U <pkg>` | Update a specific package |
| `warp -U` | Update all installed packages |
| `warp -LU` | List available updates |

## Query

| Command | Description |
|---|---|
| `warp -Q` | List all installed packages |
| `warp -s <pkg>` | Show detailed info about an installed package |
| `warp -Qe` | List explicitly installed packages (not pulled as deps) |
| `warp -Qd` | List packages installed as dependencies only |
| `warp -S <file>` | Show which package owns a given file |
| `warp -ls <query>` | Search repository for packages |

## Building Packages

| Command | Description |
|---|---|
| `warp -cP <folder>` | Build a `.wrp` package from a folder |
| `warp --verify <file>` | Verify checksum of a `.wrp` file |
| `warp --push <file>` | Upload package to configured repository |

## Repositories

| Command | Description |
|---|---|
| `warp repo --list` | List configured repositories |
| `warp repo --add <url>` | Add a repository |
| `warp repo --remove <name>` | Remove a repository |
| `warp repo --info <name>` | Show repository details |

## Diagnostics

| Command | Description |
|---|---|
| `warp --fix` | Repair broken dependencies |
| `warp --check` | Verify integrity of installed packages |
| `warp --orphans` | List orphaned packages (installed as deps, no longer needed) |
| `warp --log` | Show operation history |
| `warp --rollback <pkg>` | Revert a package to its previous version |

## System

| Command | Description |
|---|---|
| `warp -info` | Show WARP version, arch, and system info |
| `warp -help` | Show general help |
| `warp -help <flag>` | Show help for a specific flag |
| `warp -q` | Enable quiet mode (suppress all output except result) |

---

## Quiet Mode

Append `-q` to any command to suppress all output except the final result:

```bash
warp -i firefox.wrp -q
# output: Gotowe
```

On error in quiet mode, only the error message is shown:

```bash
warp -D nonexistent -q
# output: ERROR: Package 'nonexistent' is not installed
```
