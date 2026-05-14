#include "tui.h"
#include "config.h"
#include "db.h"
#include "format.h"
#include "install.h"
#include "remove.h"
#include "build.h"
#include "repo.h"
#include "diag.h"
#include "protect.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <clocale>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

namespace fs = std::filesystem;

static const std::string WARP_VERSION = "0.5.7";

static void usage() {
    std::cout << R"(Usage: warp [options] [package/file]

Package management:
  -G  <pkg...>       Install from repository (one or more packages)
  -i  <file|folder>  Install locally (.wrp or .tar.xz)
  -D  <pkg...>       Remove package(s)
  -DD <pkg...>       Remove package(s) and dependencies
  -DC <pkg>          Remove cached files for package
  -DA <pkg>          Remove package, dependencies, and cache

Updates:
  --sync             Synchronize repository index
  -U                 Upgrade all installed packages
  -LU                List available updates

Query:
  -A                 List all installed packages
  -s  <pkg>          Show detailed package info
  -S  <file>         Which package owns a file?
  -ls <query>        Search repository for packages

Building:
  -cP <folder>       Build a .wrp package from folder
  -build <folder>    Build a .wrp from source (uses WARPBUILD)
  -buildI <folder>   Build from source and install immediately
  --verify <file>    Verify checksum of a .wrp file
  --push <file>      Upload package to repository

Repositories:
  repo --list        List configured repositories
  repo --add <url>   Add a repository
  repo --remove <n>  Remove a repository by number

Diagnostics:
  --autoremove       Remove packages no longer needed as dependencies
  --clean-cache      Remove all cached package files
  --fix              Repair broken dependencies
  --check            Verify integrity of installed packages
  --orphans          List orphaned packages
  --log              Show operation history
  --rollback <pkg>   Revert package to previous version

Query (scripting):
  -Q                 Print installed package count (for scripts/fetch tools)

Protection:
  protect <pkg>      Add package to protected list (never remove/upgrade)
  unprotect <pkg>    Remove package from protected list
  protected          List all protected packages

System:
  --version, -v      Show version and license
  -info              WARP version and system info
  -help              Show this help
  -q                 Quiet mode (can appear anywhere)
)";
}

static void cmd_version() {
    std::cout << "WARP " << WARP_VERSION << " — Warp Archive Repository Packager\n"
              << "Copyright (C) 2026 Marcin Przybysz\n"
              << "License: GPL-2.0 <https://www.gnu.org/licenses/>\n";
}

static void cmd_install_local(const std::string& target) {
    if (target.empty())       tui::done_err("Provide a file or folder path");
    if (!fs::exists(target))  tui::done_err("Not found: " + target);

    if (fs::is_directory(target)) {
        tui::log_step("Directory detected — building package...");
        build::create_pkg(target);
        fs::path wrp_file;
        for (const auto& e : fs::directory_iterator(fs::temp_directory_path()))
            if (e.path().extension() == ".wrp") { wrp_file = e.path(); break; }
        if (wrp_file.empty()) tui::done_err("Failed to build package from folder");
        install::from_warp(wrp_file);
        fs::remove(wrp_file);
        fs::remove(fs::path(wrp_file.string() + ".sha256"));
        return;
    }

    auto fmt = format::detect(target);
    switch (fmt) {
        case format::Type::Warp:    install::from_warp(target);   break;
        case format::Type::TarXz:   install::from_tarxz(target);  break;
        case format::Type::Unknown: tui::done_err("Unknown format: " + target);
    }
}

static void cmd_remove(const std::string& name, bool with_deps) {
    if (name.empty()) tui::done_err("Provide a package name");
    remove_pkg::remove(name, with_deps);
}

static void get_term_size(int& rows, int& cols) {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) {
        rows = w.ws_row;
        cols = w.ws_col;
    } else {
        rows = 24;
        cols = 80;
    }
}

static void cmd_list() {
    auto pkgs = db::list_all();
    if (pkgs.empty()) { std::cout << "No packages installed.\n"; return; }

    bool is_tty = isatty(STDOUT_FILENO);
    bool color  = tui::use_color && is_tty;

    // Non-interactive: simple two-column output
    if (!is_tty) {
        for (const auto& p : pkgs)
            std::cout << std::left << std::setw(30) << p.name << p.version << "\n";
        return;
    }

    int total = static_cast<int>(pkgs.size());
    const int NCOLS = 3;
    int total_rows = (total + NCOLS - 1) / NCOLS;

    // Layout state — recalculated on resize
    struct Layout {
        int term_rows, term_cols;
        int col_width, name_w, ver_w, content_rows;
    };
    auto make_layout = [&]() -> Layout {
        Layout l{};
        get_term_size(l.term_rows, l.term_cols);
        l.col_width    = l.term_cols / NCOLS;
        l.name_w       = std::max(10, l.col_width - 14);
        l.ver_w        = l.col_width - l.name_w - 1;
        l.content_rows = std::max(1, l.term_rows - 3);
        return l;
    };

    auto render_screen = [&](const Layout& L, int top_row) {
        std::string out;
        out.reserve(8192);
        out += "\033[H";  // move to top-left

        // Header
        out += "\033[1m";
        for (int c = 0; c < NCOLS; ++c) {
            std::ostringstream h;
            h << std::left << std::setw(L.name_w) << "PACKAGE"
              << " " << std::left << std::setw(L.ver_w) << "VERSION";
            std::string hs = h.str();
            hs.resize(L.col_width, ' ');
            out += hs;
        }
        out += "\033[0m\n";

        // Separator
        out += std::string(L.term_cols, '-') + "\n";

        // Content rows
        for (int r = 0; r < L.content_rows; ++r) {
            int row_idx = top_row + r;
            for (int c = 0; c < NCOLS; ++c) {
                int pkg_idx = row_idx * NCOLS + c;
                if (pkg_idx < total) {
                    const auto& p = pkgs[pkg_idx];
                    if (color) {
                        if (p.source == "system")          out += "\033[2m";
                        else if (p.name.rfind("lib",0)==0) out += "\033[0;33m";
                        else                               out += "\033[0;32m";
                    }
                    std::string nm = p.name;
                    if ((int)nm.size() > L.name_w) { nm.resize(L.name_w - 1); nm += '>'; }
                    std::string vr = p.version;
                    if ((int)vr.size() > L.ver_w)  { vr.resize(L.ver_w  - 1); vr += '>'; }

                    std::ostringstream cell;
                    cell << std::left << std::setw(L.name_w) << nm
                         << " " << std::left << std::setw(L.ver_w) << vr;
                    std::string cs = cell.str();
                    cs.resize(L.col_width, ' ');
                    out += cs;
                    if (color) out += "\033[0m";
                } else {
                    out += std::string(L.col_width, ' ');
                }
            }
            out += "\033[K\n";  // clear to eol
        }

        // Status bar
        int vis_first = top_row * NCOLS + 1;
        int vis_last  = std::min((top_row + L.content_rows) * NCOLS, total);
        std::ostringstream sb;
        sb << "\033[7m [↑↓] scroll  [PgUp/PgDn] page  [q] quit"
           << "   " << vis_first << "-" << vis_last << " / " << total << " packages \033[0m\033[K";
        out += sb.str();

        std::cout << out << std::flush;
    };

    // Enter alternate screen + hide cursor
    std::cout << "\033[?1049h\033[?25l" << std::flush;

    struct termios orig{}, raw{};
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    Layout L = make_layout();
    int top     = 0;
    int max_top = std::max(0, total_rows - L.content_rows);
    render_screen(L, top);

    while (true) {
        char buf[4] = {};
        if (read(STDIN_FILENO, buf, 1) <= 0) break;

        bool changed = false;
        if (buf[0] == 'q' || buf[0] == 'Q') break;

        if (buf[0] == '\033') {
            // Read rest of escape sequence
            read(STDIN_FILENO, buf + 1, 1);
            if (buf[1] == '[') {
                read(STDIN_FILENO, buf + 2, 1);
                if (buf[2] == 'A') {                      // Up
                    if (top > 0) { --top; changed = true; }
                } else if (buf[2] == 'B') {               // Down
                    if (top < max_top) { ++top; changed = true; }
                } else if (buf[2] == '5') {               // PgUp
                    read(STDIN_FILENO, buf, 1);
                    top = std::max(0, top - L.content_rows);
                    changed = true;
                } else if (buf[2] == '6') {               // PgDn
                    read(STDIN_FILENO, buf, 1);
                    top = std::min(max_top, top + L.content_rows);
                    changed = true;
                }
            }
        }

        // Recalculate layout (handles terminal resize)
        L       = make_layout();
        max_top = std::max(0, total_rows - L.content_rows);
        if (top > max_top) { top = max_top; changed = true; }
        if (changed || true) render_screen(L, top);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    std::cout << "\033[?25h\033[?1049l" << std::flush;
}

static void cmd_info(const std::string& name) {
    if (name.empty()) tui::done_err("Provide a package name");

    if (db::exists(name)) {
        std::cout << db::get_info(name);
        auto [repo_ver, _] = repo::index_get_any(name, "version");
        if (!repo_ver.empty() && repo_ver != db::get_version(name))
            std::cout << "update=" << repo_ver << " (available)\n";
        return;
    }

    // Not installed — try repo index
    auto [version, re]    = repo::index_get_any(name, "version");
    if (version.empty())  tui::done_err("Package '" + name + "' not found (not installed, not in index)");

    auto [desc, _1]       = repo::index_get_any(name, "description");
    auto [deps, _2]       = repo::index_get_any(name, "deps");
    auto [license, _3]    = repo::index_get_any(name, "license");
    auto [arch, _4]       = repo::index_get_any(name, "arch");
    auto [size_kb, _5]    = repo::index_get_any(name, "size");

    std::cout << "name="        << name    << "\n"
              << "version="     << version << "\n"
              << "arch="        << arch    << "\n"
              << "deps="        << deps    << "\n"
              << "license="     << license << "\n"
              << "description=" << desc    << "\n"
              << "size="        << size_kb << " kB\n"
              << "status=not installed\n";
}

static void cmd_owner(const std::string& filepath) {
    if (filepath.empty()) tui::done_err("Provide a file path");
    std::string owner = db::owner_of(filepath);
    if (!owner.empty())
        std::cout << filepath << " → " << owner << "\n";
    else {
        std::cerr << "No package owns: " << filepath << "\n";
        std::exit(1);
    }
}

static void cmd_sysinfo() {
    cmd_version();
    std::cout << "\n";
    auto pkgs = db::list_all();
    struct utsname un{};
    uname(&un);
    std::cout << "Arch:     " << un.machine << "\n"
              << "Kernel:   " << un.release << "\n"
              << "DB:       " << db::db_root.string() << "\n"
              << "Packages: " << pkgs.size() << " installed\n";
}

// Collect args skipping flags like -q
static std::vector<std::string> pkg_args(int argc, char* argv[], int start) {
    std::vector<std::string> result;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-q") continue;
        result.push_back(a);
    }
    return result;
}

static void run_queue(const std::vector<std::string>& pkgs,
                      const std::string& action_label,
                      const std::function<void(const std::string&)>& fn) {
    int total = static_cast<int>(pkgs.size());
    if (total == 1) { fn(pkgs[0]); return; }

    tui::queue_mode = true;
    for (int i = 0; i < total; ++i) {
        tui::queue_msg(">>> " + action_label + " (" + std::to_string(i+1) +
                       " of " + std::to_string(total) + ") " + pkgs[i]);
        fn(pkgs[i]);
        tui::queue_msg(">>> Completed (" + std::to_string(i+1) +
                       " of " + std::to_string(total) + ") " + pkgs[i]);
    }
    tui::queue_mode = false;
    tui::done_ok();
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "C.UTF-8");
    if (argc < 2) { usage(); return 0; }

    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "-q") tui::quiet = true;

    config::load();
    if (config::cfg.quiet)  tui::quiet     = true;
    if (!config::cfg.color) tui::use_color = false;

    std::string db_path = config::cfg.cache_dir + "/db";
    if (const char* e = getenv("WARP_DB")) db_path = e;
    db::init(db_path);

    std::string cmd = argv[1];

    if (cmd == "-i") {
        cmd_install_local(argc > 2 ? argv[2] : "");
    } else if (cmd == "-G") {
        auto pkgs = pkg_args(argc, argv, 2);
        run_queue(pkgs, "Fetching", [](const std::string& p){ repo::install(p); });
    } else if (cmd == "-D") {
        auto pkgs = pkg_args(argc, argv, 2);
        run_queue(pkgs, "Removing", [](const std::string& p){ cmd_remove(p, false); });
    } else if (cmd == "-DD") {
        auto pkgs = pkg_args(argc, argv, 2);
        run_queue(pkgs, "Removing", [](const std::string& p){ cmd_remove(p, true); });
    } else if (cmd == "-DC") {
        diag::remove_cache(argc > 2 ? argv[2] : "");
    } else if (cmd == "-DA") {
        std::string name = argc > 2 ? argv[2] : "";
        cmd_remove(name, true);
        diag::remove_cache(name);
        tui::done_ok();
    } else if (cmd == "-A")       { cmd_list(); }
    else if (cmd == "-Q")         { std::cout << db::list_all().size() << "\n"; }
    else if (cmd == "-s")         { cmd_info(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-S")         { cmd_owner(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-ls")        { repo::search(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-U")         { repo::upgrade(); }
    else if (cmd == "-LU")        { repo::list_updates(); }
    else if (cmd == "-cP")        { build::create_pkg(argc > 2 ? argv[2] : ""); tui::done_ok(); }
    else if (cmd == "-build")     { build::build_from_source(argc > 2 ? argv[2] : "", false); tui::done_ok(); }
    else if (cmd == "-buildI")    { build::build_from_source(argc > 2 ? argv[2] : "", true);  tui::done_ok(); }
    else if (cmd == "--sync")     { repo::sync(); diag::scan_system(); repo::list_updates(); tui::println(""); tui::done_ok(); }
    else if (cmd == "--autoremove")  { diag::autoremove(); }
    else if (cmd == "--clean-cache") { diag::clean_cache(); }
    else if (cmd == "--fix")      { diag::fix(); }
    else if (cmd == "--check")    { diag::check(); }
    else if (cmd == "--orphans")  { diag::orphans(); }
    else if (cmd == "--log")      { diag::show_log(); }
    else if (cmd == "--rollback") { diag::rollback(argc > 2 ? argv[2] : ""); }
    else if (cmd == "--verify")   { diag::verify(argc > 2 ? argv[2] : ""); }
    else if (cmd == "--push")     { diag::push(argc > 2 ? argv[2] : ""); }
    else if (cmd == "repo") {
        std::string sub = argc > 2 ? argv[2] : "";
        if (sub == "--list")                { repo::list_repos(); }
        else if (sub == "--add")            { repo::add_repo(argc > 3 ? argv[3] : ""); }
        else if (sub == "--remove")         { repo::remove_repo(argc > 3 ? std::stoi(argv[3]) : 0); }
        else if (sub == "--gen-index")      { repo::gen_index(argc > 3 ? argv[3] : "."); tui::println(""); tui::done_ok(); }
        else { std::cerr << "Usage: warp repo --list | --add <url> | --remove <n> | --gen-index <dir>\n"; return 1; }
    } else if (cmd == "protect")   { protect::add(argc > 2 ? argv[2] : ""); }
    else if (cmd == "unprotect")   { protect::remove(argc > 2 ? argv[2] : ""); }
    else if (cmd == "protected")   { protect::list(); }
    else if (cmd == "--version" || cmd == "-v")  { cmd_version(); }
    else if (cmd == "-info")      { cmd_sysinfo(); }
    else if (cmd == "-help" || cmd == "--help" || cmd == "-h") { usage(); }
    else if (cmd == "-q")         { usage(); }
    else {
        std::cerr << "Unknown option: " << cmd << "\n";
        usage();
        return 1;
    }

    return 0;
}
