# fish completion for warp
# Install: copy to /usr/share/fish/vendor_completions.d/warp.fish

set -l warp_cmds -G -i -D -DD -DC -DA --sync -U -LU -A -s -S -ls -cP -build -buildI --verify --push repo --autoremove --clean-cache --fix --check --orphans --log --rollback protect unprotect protected -Q -v --version -info -help

# Disable file completion globally, re-enable where needed
complete -c warp -f

# --- Package management ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s G -d "Zainstaluj z repozytorium"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s i -d "Zainstaluj lokalnie (.wrp lub .tar.xz)"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s D -d "Usuń paczkę"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l DD -d "Usuń paczkę i jej zależności"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l DC -d "Usuń cache paczki"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l DA -d "Usuń paczkę, zależności i cache"

# --- Updates ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l sync    -d "Synchronizuj indeks repozytorium"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s U       -d "Aktualizuj wszystkie paczki"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l LU      -d "Lista dostępnych aktualizacji"

# --- Query ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s A       -d "Lista zainstalowanych paczek"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s s       -d "Szczegóły paczki"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s S       -d "Która paczka posiada ten plik?"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l ls      -d "Szukaj paczek w repo"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s Q       -d "Liczba zainstalowanych paczek (dla skryptów)"

# --- Building ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l cP      -d "Zbuduj .wrp z folderu"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l build   -d "Zbuduj .wrp ze źródeł (WARPBUILD)"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l buildI  -d "Zbuduj ze źródeł i zainstaluj"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l verify  -d "Zweryfikuj checksum pliku .wrp"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l push    -d "Wyślij paczkę do repozytorium"

# --- Repositories ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -a repo    -d "Zarządzanie repozytoriami"
complete -c warp -n "__fish_seen_subcommand_from repo" -a --list   -d "Lista repozytoriów"
complete -c warp -n "__fish_seen_subcommand_from repo" -a --add    -d "Dodaj repozytorium"
complete -c warp -n "__fish_seen_subcommand_from repo" -a --remove -d "Usuń repozytorium"

# --- Diagnostics ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l autoremove   -d "Usuń niepotrzebne zależności"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l clean-cache  -d "Wyczyść cache paczek"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l fix          -d "Napraw zepsute zależności"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l check        -d "Sprawdź integralność paczek"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l orphans      -d "Lista osieroconych paczek"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l log          -d "Historia operacji"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l rollback     -d "Cofnij paczkę do poprzedniej wersji"

# --- Protection ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -a protect   -d "Chroń paczkę przed usunięciem"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -a unprotect -d "Usuń ochronę paczki"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -a protected -d "Lista chronionych paczek"

# --- System ---
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -s v -l version -d "Wersja WARP"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l info         -d "Wersja WARP i info o systemie"
complete -c warp -n "not __fish_seen_subcommand_from $warp_cmds" -l help         -d "Pokaż pomoc"

# --- Global flags (zawsze dostępne) ---
complete -c warp -s q -l quiet   -d "Tryb cichy"
complete -c warp -l verbose      -d "Tryb szczegółowy"

# --- Uzupełnianie plików .wrp dla -i ---
complete -c warp -n "__fish_seen_subcommand_from -i" -F -a "(__fish_complete_suffix .wrp)"

# --- Uzupełnianie zainstalowanych paczek dla -D, -DD, -DC, -DA, -s, -S, protect, unprotect, --rollback ---
function __warp_installed_pkgs
    warp -A 2>/dev/null | awk '{print $1}'
end

for flag in -D -DD -DC -DA -s protect unprotect --rollback
    complete -c warp -n "__fish_seen_subcommand_from $flag" -a "(__warp_installed_pkgs)" -d "zainstalowana paczka"
end

# --- Uzupełnianie dostępnych paczek w repo dla -G ---
function __warp_repo_pkgs
    warp -ls "" 2>/dev/null | awk '{print $1}'
end

complete -c warp -n "__fish_seen_subcommand_from -G" -a "(__warp_repo_pkgs)" -d "paczka w repo"
