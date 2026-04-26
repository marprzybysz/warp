#include "tui.h"
#include "config.h"
#include "db.h"
#include "format.h"
#include "install.h"
#include "remove.h"
#include "build.h"
#include "repo.h"
#include <iostream>
#include <iomanip>
#include <clocale>
#include <filesystem>

namespace fs = std::filesystem;

static void usage() {
    std::cout << R"(Usage: warp [options] [package/file]

Package management:
  -G  <pkg>          Install from repository
  -gP <file|folder>  Install locally (.warp or .tar.xz)
  -D  <pkg>          Remove package
  -DD <pkg>          Remove package and its dependencies
  -DC <pkg>          Remove cached files for package
  -DA <pkg>          Remove package, dependencies, and cache

Updates:
  --sync             Synchronize repository index
  -U  <pkg>          Update a specific package
  -AU                Update all installed packages
  -LU                List available updates

Query:
  -Q                 List all installed packages
  -Qi <pkg>          Show detailed package info
  -Qe                List explicitly installed packages
  -Qd                List dependency-only packages
  -Qo <file>         Which package owns a file?
  -ls <query>        Search repository for packages

Building:
  -cP <folder>       Build a .warp package from folder
  --verify <file>    Verify checksum of a .warp file
  --push <file>      Upload package to repository

Repositories:
  repo --list        List configured repositories
  repo --add <url>   Add a repository
  repo --remove <n>  Remove a repository
  repo --info <n>    Show repository details

Diagnostics:
  --fix              Repair broken dependencies
  --check            Verify integrity of installed packages
  --orphans          List orphaned packages
  --log              Show operation history
  --rollback <pkg>   Revert package to previous version

System:
  -info              WARP version and system info
  -help [flag]       Show help (optionally for a specific flag)
  -q                 Quiet mode
)";
}

static void cmd_install_local(const std::string& target) {
    if (target.empty())        tui::done_err("Provide a file or folder path");
    if (!fs::exists(target))   tui::done_err("Not found: " + target);

    auto fmt = format::detect(target);
    switch (fmt) {
        case format::Type::Warp:   install::from_warp(target);   break;
        case format::Type::TarXz:  install::from_tarxz(target);  break;
        case format::Type::Unknown:
            tui::done_err("Unknown format: " + target);
    }
    tui::println("");
    tui::done_ok();
}

static void cmd_remove(const std::string& name, bool with_deps) {
    if (name.empty()) tui::done_err("Provide a package name");
    remove_pkg::remove(name, with_deps);
    tui::println("");
    tui::done_ok();
}

static void cmd_list() {
    auto pkgs = db::list_all();
    if (pkgs.empty()) {
        std::cout << "No packages installed.\n";
        return;
    }
    std::cout << std::left << std::setw(30) << "PACKAGE" << "VERSION\n";
    std::cout << std::string(30, '-') << "-------\n";
    for (const auto& p : pkgs)
        std::cout << std::left << std::setw(30) << p.name << p.version << "\n";
}

static void cmd_info(const std::string& name) {
    if (name.empty())        tui::done_err("Provide a package name");
    if (!db::exists(name))   tui::done_err("Package '" + name + "' is not installed");
    std::cout << db::get_info(name);
}

static void cmd_owner(const std::string& filepath) {
    if (filepath.empty()) tui::done_err("Provide a file path");
    std::string owner = db::owner_of(filepath);
    if (!owner.empty()) {
        std::cout << filepath << " → " << owner << "\n";
    } else {
        std::cerr << "No package owns: " << filepath << "\n";
        std::exit(1);
    }
}

static void cmd_sysinfo() {
    auto pkgs = db::list_all();
    std::cout << "WARP Package Manager 0.1.0\n";
    std::cout << "DB:       " << db::db_root.string() << "\n";
    std::cout << "Packages: " << pkgs.size() << " installed\n";
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "C.UTF-8");

    if (argc < 2) { usage(); return 0; }

    // Check for quiet flag early
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "-q") tui::quiet = true;

    config::load();

    // Apply config to tui
    if (config::cfg.quiet)   tui::quiet        = true;
    if (!config::cfg.color)  tui::use_color    = false;
    if (!config::cfg.progress) tui::use_progress = false;

    // Init DB
    std::string db_path = config::cfg.cache_dir + "/db";
    if (const char* e = getenv("WARP_DB")) db_path = e;
    db::init(db_path);

    std::string cmd = argv[1];

    if      (cmd == "-gP")              { cmd_install_local(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-G")               { repo::install(argc > 2 ? argv[2] : ""); tui::println(""); tui::done_ok(); }
    else if (cmd == "-D")               { cmd_remove(argc > 2 ? argv[2] : "", false); }
    else if (cmd == "-DD")              { cmd_remove(argc > 2 ? argv[2] : "", true); }
    else if (cmd == "-Q")               { cmd_list(); }
    else if (cmd == "-Qi")              { cmd_info(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-Qo")              { cmd_owner(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-ls")              { repo::search(argc > 2 ? argv[2] : ""); }
    else if (cmd == "-cP")              { build::create_pkg(argc > 2 ? argv[2] : ""); tui::println(""); tui::done_ok(); }
    else if (cmd == "--sync")           { repo::sync(); tui::println(""); tui::done_ok(); }
    else if (cmd == "-info")            { cmd_sysinfo(); }
    else if (cmd == "-help" || cmd == "--help" || cmd == "-h") { usage(); }
    else if (cmd == "-q")               { usage(); }
    else {
        std::cerr << "Unknown option: " << cmd << "\n";
        usage();
        return 1;
    }

    return 0;
}
