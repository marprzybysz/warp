#compdef warp
# zsh completion for warp
# Install: copy to /usr/share/zsh/site-functions/_warp
#          then run: autoload -U compinit && compinit

_warp_installed_pkgs() {
    local -a pkgs
    pkgs=(${(f)"$(warp -A 2>/dev/null | awk '{print $1}')"})
    _describe 'zainstalowana paczka' pkgs
}

_warp_repo_pkgs() {
    local -a pkgs
    pkgs=(${(f)"$(warp -ls '' 2>/dev/null | awk '{print $1}')"})
    _describe 'paczka w repo' pkgs
}

_warp() {
    local state

    _arguments -C \
        '(-q --quiet)'{-q,--quiet}'[Tryb cichy]' \
        '--verbose[Tryb szczegółowy]' \
        '1: :->cmd' \
        '*: :->args'

    case $state in
        cmd)
            local -a commands
            commands=(
                '-G:Zainstaluj z repozytorium'
                '-i:Zainstaluj lokalnie (.wrp lub .tar.xz)'
                '-D:Usuń paczkę'
                '-DD:Usuń paczkę i jej zależności'
                '-DC:Usuń cache paczki'
                '-DA:Usuń paczkę, zależności i cache'
                '--sync:Synchronizuj indeks repozytorium'
                '-U:Aktualizuj wszystkie paczki'
                '-LU:Lista dostępnych aktualizacji'
                '-A:Lista zainstalowanych paczek'
                '-s:Szczegóły paczki'
                '-S:Która paczka posiada ten plik?'
                '-ls:Szukaj paczek w repo'
                '-Q:Liczba zainstalowanych paczek'
                '-cP:Zbuduj .wrp z folderu'
                '-build:Zbuduj .wrp ze źródeł (WARPBUILD)'
                '-buildI:Zbuduj ze źródeł i zainstaluj'
                '--verify:Zweryfikuj checksum pliku .wrp'
                '--push:Wyślij paczkę do repozytorium'
                'repo:Zarządzanie repozytoriami'
                '--autoremove:Usuń niepotrzebne zależności'
                '--clean-cache:Wyczyść cache paczek'
                '--fix:Napraw zepsute zależności'
                '--check:Sprawdź integralność paczek'
                '--orphans:Lista osieroconych paczek'
                '--log:Historia operacji'
                '--rollback:Cofnij paczkę do poprzedniej wersji'
                'protect:Chroń paczkę przed usunięciem'
                'unprotect:Usuń ochronę paczki'
                'protected:Lista chronionych paczek'
                '-v:Wersja WARP'
                '--version:Wersja WARP'
                '-info:Wersja WARP i info o systemie'
                '-help:Pokaż pomoc'
                '--help:Pokaż pomoc'
            )
            _describe 'polecenie' commands
            ;;

        args)
            case $words[2] in
                -G)
                    _warp_repo_pkgs
                    ;;
                -i)
                    _files -g '*.wrp'
                    ;;
                -D|-DD|-DC|-DA|-s|protect|unprotect|--rollback)
                    _warp_installed_pkgs
                    ;;
                -S|--verify|--push)
                    _files
                    ;;
                -ls)
                    # wolne pole tekstowe — brak uzupełnień
                    ;;
                -build|-buildI|-cP)
                    _directories
                    ;;
                repo)
                    local -a subcmds
                    subcmds=(
                        '--list:Lista repozytoriów'
                        '--add:Dodaj repozytorium'
                        '--remove:Usuń repozytorium po numerze'
                        '--gen-index:Generuj INDEX z katalogu paczek'
                    )
                    _describe 'podpolecenie repo' subcmds
                    ;;
            esac
            ;;
    esac
}

_warp "$@"
