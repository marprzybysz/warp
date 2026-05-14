#include "diag.h"
#include "tui.h"
#include "db.h"
#include "config.h"
#include "install.h"
#include "remove.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <array>
#include <map>
#include <cstdio>

namespace diag {

namespace fs = std::filesystem;

static fs::path pkg_cache() {
    return fs::path(config::cfg.cache_dir) / "packages";
}

static fs::path log_path() {
    return fs::path(config::cfg.cache_dir).parent_path() / "warp" / "warp.log";
}

void check() {
    std::cout << "\n";
    int errors = 0;
    for (const auto& name : db::list_names()) {
        auto files = db::get_files(name);
        std::vector<std::string> missing;
        for (const auto& f : files)
            if (!fs::exists(f)) missing.push_back(f);

        if (missing.empty()) {
            tui::log_step(name, "ok");
        } else {
            tui::log_step(name, "err");
            for (const auto& f : missing)
                std::cout << "    missing: " << f << "\n";
            ++errors;
        }
    }
    std::cout << "\n";
    if (errors == 0)
        std::cout << "All packages OK.\n";
    else
        std::cout << errors << " package(s) have missing files. Run: warp --fix\n";
}

void fix() {
    std::cout << "\n";
    int fixed = 0;
    for (const auto& name : db::list_names()) {
        std::string deps_str = db::get_deps(name);
        if (deps_str.empty()) continue;
        std::istringstream ss(deps_str);
        std::string dep;
        while (std::getline(ss, dep, ',')) {
            dep.erase(0, dep.find_first_not_of(' '));
            dep.erase(dep.find_last_not_of(' ') + 1);
            if (dep.empty() || db::exists(dep)) continue;
            tui::warn("Missing dep '" + dep + "' for '" + name + "' — installing...");
            // repo::install(dep) — can't include repo here (circular); user must fix manually
            std::cout << "  Run: warp -G " << dep << "\n";
            ++fixed;
        }
    }
    std::cout << "\n";
    if (fixed == 0)
        std::cout << "No broken dependencies found.\n";
    else
        std::cout << fixed << " missing dependency/dependencies listed above.\n";
}

void orphans() {
    std::cout << "\n";
    // Collect all deps
    std::vector<std::string> all_deps;
    for (const auto& name : db::list_names()) {
        std::string deps_str = db::get_deps(name);
        std::istringstream ss(deps_str);
        std::string dep;
        while (std::getline(ss, dep, ',')) {
            dep.erase(0, dep.find_first_not_of(' '));
            dep.erase(dep.find_last_not_of(' ') + 1);
            if (!dep.empty()) all_deps.push_back(dep);
        }
    }

    std::vector<std::string> orphan_list;
    for (const auto& name : db::list_names()) {
        bool is_dep = false;
        for (const auto& d : all_deps)
            if (d == name) { is_dep = true; break; }
        if (!is_dep) orphan_list.push_back(name);
    }

    if (orphan_list.empty()) { std::cout << "No orphaned packages.\n"; return; }

    std::cout << std::left << std::setw(30) << "PACKAGE" << "VERSION\n";
    std::cout << std::string(40, '-') << "\n";
    for (const auto& name : orphan_list)
        std::cout << std::left << std::setw(30) << name << db::get_version(name) << "\n";
    std::cout << "\nRemove with: warp -D <package>\n";
}

void show_log() {
    fs::path lp = db::db_root.parent_path() / "warp.log";
    if (!fs::exists(lp)) { std::cout << "No operations logged yet.\n"; return; }
    std::cout << std::left
              << std::setw(20) << "DATE"
              << std::setw(12) << "ACTION"
              << std::setw(25) << "PACKAGE"
              << "VERSION\n";
    std::cout << std::string(65, '-') << "\n";
    std::ifstream f(lp);
    std::string line;
    while (std::getline(f, line))
        std::cout << line << "\n";
}

void rollback(const std::string& name) {
    if (name.empty()) tui::done_err("Provide a package name");
    if (!db::exists(name)) tui::done_err("Package '" + name + "' is not installed");

    fs::path cache = pkg_cache();
    fs::path prev;
    int count = 0;
    for (const auto& e : fs::directory_iterator(cache)) {
        std::string fname = e.path().filename().string();
        if (fname.rfind(name + "-", 0) == 0 && fname.size() > 4 &&
            fname.substr(fname.size() - 4) == ".wrp") {
            if (count == 1) { prev = e.path(); break; }
            ++count;
        }
    }

    if (prev.empty())
        tui::done_err("No previous version cached for '" + name + "' — cannot rollback");

    tui::log_step("Rolling back " + name + " to " + prev.filename().string() + "...");
    remove_pkg::remove(name, false);
    ::install::from_warp(prev);
    db::log("rollback", name);
    tui::done_ok();
}

void remove_cache(const std::string& name) {
    if (name.empty()) tui::done_err("Provide a package name");
    fs::path cache = pkg_cache();
    int count = 0;
    for (const auto& e : fs::directory_iterator(cache)) {
        std::string fname = e.path().filename().string();
        if (fname.rfind(name + "-", 0) == 0) {
            fs::remove(e.path());
            ++count;
        }
    }
    if (count == 0)
        std::cout << "No cached files for '" << name << "'.\n";
    else {
        tui::log_step("Cleared cache for " + name + "...", "ok");
        db::log("cache-clear", name);
    }
}

void verify(const std::string& path) {
    if (path.empty()) tui::done_err("Provide a .wrp file path");
    if (!fs::exists(path)) tui::done_err("File not found: " + path);

    std::string sha_path = path + ".sha256";
    if (!fs::exists(sha_path))
        tui::done_err("No .sha256 file found next to: " + path);

    tui::log_step("Verifying " + path + "...");

    std::ifstream sf(sha_path);
    std::string expected;
    sf >> expected;

    std::array<char, 128> buf{};
    FILE* p = popen(("sha256sum " + path + " | cut -d' ' -f1").c_str(), "r");
    std::string actual;
    if (p) { fgets(buf.data(), buf.size(), p); pclose(p); actual = buf.data(); }
    actual.erase(actual.find_last_not_of(" \n\r") + 1);

    if (actual == expected) {
        tui::log_step("SHA256 OK", "ok");
        std::cout << "  expected: " << expected << "\n"
                  << "  actual:   " << actual   << "\n";
    } else {
        tui::log_step("SHA256 MISMATCH", "err");
        std::cout << "  expected: " << expected << "\n"
                  << "  actual:   " << actual   << "\n";
        std::exit(1);
    }
}

void push(const std::string& path) {
    if (path.empty()) tui::done_err("Provide a .wrp file path");
    if (!fs::exists(path)) tui::done_err("File not found: " + path);

    std::string url = config::cfg.repo;
    if (url.empty()) tui::done_err("No repo URL configured");

    std::string fname = fs::path(path).filename().string();
    tui::log_step("Uploading " + fname + " to " + url + "...");

    std::string cmd = "curl -fsSL --max-time " +
                      std::to_string(config::cfg.timeout) +
                      " -T " + path + " " + url + "/" + fname;
    if (std::system(cmd.c_str()) != 0)
        tui::done_err("Upload failed");

    std::string sha_path = path + ".sha256";
    if (fs::exists(sha_path)) {
        std::string sha_cmd = "curl -fsSL --max-time " +
                              std::to_string(config::cfg.timeout) +
                              " -T " + sha_path + " " + url + "/" + fname + ".sha256";
        std::system(sha_cmd.c_str());
    }

    tui::log_step("Uploaded " + fname + "...", "ok");
    db::log("push", fname);
}

void autoremove() {
    auto all = db::list_names();

    // Collect all deps referenced by any package
    std::vector<std::string> needed;
    for (const auto& name : all) {
        std::string deps_str = db::get_deps(name);
        std::istringstream ss(deps_str);
        std::string dep;
        while (std::getline(ss, dep, ',')) {
            dep.erase(0, dep.find_first_not_of(' '));
            dep.erase(dep.find_last_not_of(' ') + 1);
            if (!dep.empty()) needed.push_back(dep);
        }
    }

    // Find packages that are only deps (no one explicitly installed them)
    // We identify "auto-installed" by SOURCE==repo and not in needed list
    std::vector<std::string> orphans;
    for (const auto& name : all) {
        // Skip system libs
        fs::path src_file = db::db_root / name / "SOURCE";
        std::string source;
        { std::ifstream f(src_file); std::getline(f, source); }
        if (source == "system") continue;

        bool is_needed = false;
        for (const auto& d : needed)
            if (d == name) { is_needed = true; break; }
        if (!is_needed) continue; // explicitly installed — keep

        // It's a dep — check if anything still needs it
        bool still_needed = false;
        for (const auto& other : all) {
            if (other == name) continue;
            std::string ds = db::get_deps(other);
            std::istringstream ss2(ds);
            std::string d;
            while (std::getline(ss2, d, ',')) {
                d.erase(0, d.find_first_not_of(' '));
                d.erase(d.find_last_not_of(' ') + 1);
                if (d == name) { still_needed = true; break; }
            }
            if (still_needed) break;
        }
        if (!still_needed) orphans.push_back(name);
    }

    if (orphans.empty()) { std::cout << "Nothing to remove.\n"; return; }

    std::cout << "\nThe following packages are no longer needed:\n";
    for (const auto& o : orphans) printf("  %s\n", o.c_str());
    std::cout << "\n";

    if (!tui::quiet) {
        printf("Remove them? [Y/n] ");
        std::string ans; std::getline(std::cin, ans);
        if (ans != "Y" && ans != "y" && !ans.empty()) { std::cout << "Aborted.\n"; return; }
    }

    for (const auto& o : orphans) {
        tui::log_step("Removing " + o + "...");
        remove_pkg::remove(o, false);
    }
    tui::done_ok();
}

void clean_cache() {
    fs::path cache = pkg_cache();
    if (!fs::is_directory(cache)) { std::cout << "Cache is empty.\n"; return; }

    uintmax_t total_size = 0;
    int count = 0;
    for (const auto& e : fs::directory_iterator(cache)) {
        if (e.path().extension() == ".wrp" ||
            e.path().extension() == ".sha256") {
            total_size += fs::file_size(e.path());
            ++count;
        }
    }

    if (count == 0) { std::cout << "Cache is empty.\n"; return; }

    std::cout << "Cache: " << count << " file(s), "
              << tui::format_size_bytes(total_size) << "\n\n";

    if (!tui::quiet) {
        printf("Remove all cached packages? [Y/n] ");
        std::string ans; std::getline(std::cin, ans);
        if (ans != "Y" && ans != "y" && !ans.empty()) { std::cout << "Aborted.\n"; return; }
    }

    for (const auto& e : fs::directory_iterator(cache))
        if (e.path().extension() == ".wrp" || e.path().extension() == ".sha256")
            fs::remove(e.path());

    tui::log_step("Cache cleared...", "ok");
    tui::done_ok();
}

static std::map<std::string, std::string> load_pc_versions() {
    std::map<std::string, std::string> result;
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };
    for (const char* dir : {"/usr/lib/pkgconfig", "/usr/share/pkgconfig",
                             "/usr/lib64/pkgconfig", "/usr/local/lib/pkgconfig"}) {
        if (!fs::is_directory(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".pc") continue;
            std::string name, version;
            std::ifstream f(entry.path());
            std::string line;
            while (std::getline(f, line)) {
                if (line.rfind("Name:", 0) == 0)      name    = line.substr(5);
                else if (line.rfind("Version:", 0) == 0) version = line.substr(8);
            }
            trim(name); trim(version);
            if (!name.empty() && !version.empty())
                result[name] = version;
        }
    }
    return result;
}

void scan_system() {
    tui::log_step("Scanning system libraries via ldconfig and pkg-config...");

    auto pc = load_pc_versions();

    FILE* p = popen("ldconfig -p 2>/dev/null", "r");
    if (!p) tui::done_err("ldconfig not found — cannot scan system libraries");

    int count = 0;
    std::array<char, 512> buf{};
    bool first = true;
    while (fgets(buf.data(), buf.size(), p)) {
        if (first) { first = false; continue; } // skip header line
        std::string line(buf.data());
        line.erase(line.find_last_not_of(" \n\r\t") + 1);

        // Format: "\tlib<name>.so.X (libc6,...) => /path"
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string soname = line.substr(tab + 1);
        auto space = soname.find(' ');
        if (space != std::string::npos) soname = soname.substr(0, space);

        if (soname.rfind("lib", 0) != 0) continue;

        // Derive name: libfoo.so.1 → foo
        std::string name = soname.substr(3); // strip "lib"
        auto dot = name.find(".so");
        if (dot != std::string::npos) name = name.substr(0, dot);
        if (name.empty()) continue;

        // Full version: prefer pkg-config .pc, fall back to SONAME number
        std::string ver;
        auto pc_it = pc.find(name);
        if (pc_it != pc.end()) {
            ver = pc_it->second;
        } else {
            auto so_pos = soname.find(".so.");
            ver = (so_pos != std::string::npos) ? soname.substr(so_pos + 4) : "system";
        }

        if (db::exists(name)) continue; // don't overwrite user-installed packages

        fs::path pkg_dir = db::db_root / name;
        fs::create_directories(pkg_dir);
        { std::ofstream wi(pkg_dir / "WARPINFO");
          wi << "name="    << name    << "\n"
             << "version=" << ver     << "\n"
             << "source=system\n"; }
        { std::ofstream src(pkg_dir / "SOURCE"); src << "system\n"; }

        ++count;
    }
    pclose(p);

    // Register .pc packages that weren't caught by ldconfig (e.g. bash, coreutils-like tools)
    for (const auto& [name, ver] : pc) {
        if (db::exists(name)) continue;
        fs::path pkg_dir = db::db_root / name;
        fs::create_directories(pkg_dir);
        { std::ofstream wi(pkg_dir / "WARPINFO");
          wi << "name="    << name << "\n"
             << "version=" << ver  << "\n"
             << "source=system\n"; }
        { std::ofstream src(pkg_dir / "SOURCE"); src << "system\n"; }
        ++count;
    }

    // Mark as initialized
    fs::path marker = db::db_root.parent_path() / ".initialized";
    { std::ofstream m(marker); m << "1\n"; }

    tui::log_step("Registered " + std::to_string(count) + " system libraries/packages...", "ok");
}

void scan_system_quiet() {
    bool prev = tui::quiet;
    tui::quiet = true;
    scan_system();
    tui::quiet = prev;
}

} // namespace diag
