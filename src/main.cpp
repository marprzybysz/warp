#include "tui.h"
#include "config.h"
#include "db.h"
#include "format.h"
#include "install.h"
#include "remove.h"
#include "build.h"
#include "repo.h"
#include "diag.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <clocale>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

static const std::string WARP_VERSION = "0.4.0";

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

static void cmd_list() {
    auto pkgs = db::list_all();
    if (pkgs.empty()) { std::cout << "No packages installed.\n"; return; }

    bool color = tui::use_color && isatty(STDOUT_FILENO);

    auto colorize = [&](const db::PkgEntry& p) -> std::string {
        if (!color) return "";
        if (p.source == "system")                        return "\033[2m";     // dim — system
        if (p.name.rfind("lib", 0) == 0)                return "\033[0;33m";  // yellow — library
        return "\033[0;32m";                                                    // green — app
    };
    const std::string reset = color ? "\033[0m" : "";

    std::ostringstream out;
    out << std::left << std::setw(30) << "PACKAGE" << "VERSION\n";
    out << std::string(30, '-') << "-------\n";
    for (const auto& p : pkgs) {
        std::string col = colorize(p);
        out << col
            << std::left << std::setw(30) << p.name
            << p.version
            << reset << "\n";
    }

    std::string content = out.str();

    if (isatty(STDOUT_FILENO) && pkgs.size() > 20) {
        FILE* pager = popen("less -R", "w");
        if (pager) {
            fwrite(content.c_str(), 1, content.size(), pager);
            pclose(pager);
            return;
        }
    }
    std::cout << content;
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
    else if (cmd == "--scan-system") { diag::scan_system(); tui::done_ok(); }
    else if (cmd == "--sync")     { repo::sync(); tui::println(""); tui::done_ok(); }
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
    } else if (cmd == "--version" || cmd == "-v")  { cmd_version(); }
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
