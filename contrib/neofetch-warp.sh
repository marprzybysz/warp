# Neofetch — integracja z WARP
# Wklej te dwie rzeczy do ~/.config/neofetch/config.conf

# ── 1. Dodaj tę funkcję gdziekolwiek przed print_info() ──────────────────────
warp_packages() {
    local count
    count=$(warp -Q 2>/dev/null) || count=0
    echo "${count} (warp)"
}

# ── 2. W funkcji print_info() dodaj tę linię w miejscu gdzie mają być paczki ─
#    info "Packages" warp_packages
#
# Przykład print_info() z warpem:
#
# print_info() {
#     info title
#     info underline
#     info "OS"       distro
#     info "Kernel"   kernel
#     info "Packages" warp_packages    # <-- tu
#     info "Shell"    shell
#     info "Terminal" term
#     info cols
# }
