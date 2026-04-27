#include "repo.h"
#include "tui.h"
#include "config.h"
#include "db.h"
#include "install.h"
#include "remove.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <array>
#include <cstdio>

namespace repo {

namespace fs = std::filesystem;

static fs::path index_dir()   { return fs::path(config::cfg.cache_dir) / "index"; }
static fs::path pkg_cache()   { return fs::path(config::cfg.cache_dir) / "packages"; }
static fs::path index_file()  { return index_dir() / "INDEX"; }

static std::string run(const std::string& cmd) {
    std::array<char, 256> buf{};
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    while (fgets(buf.data(), buf.size(), p))
        out += buf.data();
    pclose(p);
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

static bool download(const std::string& url, const fs::path& dest) {
    fs::create_directories(dest.parent_path());
    std::string cmd = "curl -fsSL --max-time " +
                      std::to_string(config::cfg.timeout) +
                      " -o " + dest.string() + " " + url;
    return std::system(cmd.c_str()) == 0;
}

static std::string index_get(const std::string& pkg, const std::string& field) {
    std::ifstream f(index_file());
    if (!f.is_open()) return "";
    bool in_pkg = false;
    std::string line, section = "[" + pkg + "]";
    while (std::getline(f, line)) {
        if (line == section) { in_pkg = true; continue; }
        if (in_pkg) {
            if (!line.empty() && line.front() == '[') break;
            if (line.rfind(field + "=", 0) == 0)
                return line.substr(field.size() + 1);
        }
    }
    return "";
}

void sync() {
    std::string url = config::cfg.repo + "/INDEX";
    tui::log_step("Syncing from " + config::cfg.repo + "...");
    fs::create_directories(index_dir());

    if (!download(url, index_file()))
        tui::done_err("Cannot fetch INDEX from " + url);

    std::ifstream f(index_file());
    int count = 0;
    std::string line;
    while (std::getline(f, line))
        if (!line.empty() && line.front() == '[') ++count;

    tui::log_step("Fetched INDEX (" + std::to_string(count) + " packages)...", "ok");
}

void install(const std::string& pkg) {
    if (pkg.empty()) tui::done_err("Provide a package name");
    if (!fs::exists(index_file()))
        tui::done_err("INDEX not found — run: warp --sync");

    std::string version = index_get(pkg, "version");
    if (version.empty())
        tui::done_err("Package '" + pkg + "' not found in repository");

    tui::log_step("Found: " + pkg + " " + version, "ok");

    if (db::exists(pkg) && db::get_version(pkg) == version) {
        tui::log_step(pkg + " " + version + " already installed...", "ok");
        return;
    }

    std::string file_rel = index_get(pkg, "file");
    std::string sha256   = index_get(pkg, "sha256");
    if (file_rel.empty())
        tui::done_err("No file entry for '" + pkg + "' in INDEX");

    fs::path cached = pkg_cache() / fs::path(file_rel).filename();

    if (!fs::exists(cached)) {
        std::string url = config::cfg.repo + "/" + file_rel;
        tui::log_step("Downloading " + pkg + " " + version + "...");
        fs::create_directories(pkg_cache());
        std::string cmd = "curl -fL --max-time " +
                          std::to_string(config::cfg.timeout) +
                          " -o " + cached.string() + " " + url;
        if (std::system(cmd.c_str()) != 0) {
            fs::remove(cached);
            tui::done_err("Download failed: " + url);
        }
        tui::log_step("Downloaded " + pkg + "...", "ok");
    } else {
        tui::log_step("From cache: " + cached.filename().string(), "ok");
    }

    if (!sha256.empty()) {
        tui::log_step("Verifying checksum...");
        std::string actual = run("sha256sum " + cached.string() + " | cut -d' ' -f1");
        if (actual != sha256) {
            fs::remove(cached);
            tui::done_err("SHA256 mismatch — corrupted file");
        }
        tui::log_step("SHA256 OK...", "ok");
    }

    ::install::from_warp(cached);
}

void list_updates() {
    if (!fs::exists(index_file()))
        tui::done_err("INDEX not found — run: warp --sync");

    struct Update { std::string name, installed, available; };
    std::vector<Update> updates;

    for (const auto& pkg : db::list_names()) {
        std::string inst = db::get_version(pkg);
        std::string avail = index_get(pkg, "version");
        if (avail.empty() || avail == inst) continue;
        updates.push_back({pkg, inst, avail});
    }

    if (updates.empty()) { std::cout << "All packages up to date.\n"; return; }

    std::cout << std::left << std::setw(25) << "PACKAGE"
              << std::setw(15) << "INSTALLED" << "AVAILABLE\n";
    std::cout << std::string(55, '-') << "\n";
    for (const auto& u : updates)
        std::cout << std::left << std::setw(25) << u.name
                  << std::setw(15) << u.installed << u.available << "\n";
}

void upgrade() {
    if (!fs::exists(index_file()))
        tui::done_err("INDEX not found — run: warp --sync");

    int upgraded = 0;
    for (const auto& pkg : db::list_names()) {
        std::string inst  = db::get_version(pkg);
        std::string avail = index_get(pkg, "version");
        if (avail.empty() || avail == inst) continue;

        tui::log_step("Upgrading " + pkg + ": " + inst + " → " + avail + "...");
        remove_pkg::remove(pkg, false);
        install(pkg);
        db::log("upgrade", pkg, avail);
        ++upgraded;
    }

    std::cout << "\n";
    if (upgraded == 0)
        std::cout << "Nothing to upgrade.\n";
    else
        tui::done_ok();
}

void search(const std::string& query) {
    if (query.empty()) tui::done_err("Provide a search query");
    if (!fs::exists(index_file()))
        tui::done_err("INDEX not found — run: warp --sync");

    std::ifstream f(index_file());
    struct Result { std::string name, version, description; };
    std::vector<Result> results;
    Result cur;
    std::string line;

    while (std::getline(f, line)) {
        if (!line.empty() && line.front() == '[') {
            if (!cur.name.empty() &&
                (cur.name.find(query) != std::string::npos ||
                 cur.description.find(query) != std::string::npos))
                results.push_back(cur);
            cur = { line.substr(1, line.size() - 2), "", "" };
        } else if (line.rfind("version=", 0) == 0)     cur.version     = line.substr(8);
          else if (line.rfind("description=", 0) == 0) cur.description = line.substr(12);
    }
    if (!cur.name.empty() &&
        (cur.name.find(query) != std::string::npos ||
         cur.description.find(query) != std::string::npos))
        results.push_back(cur);

    if (results.empty()) { std::cout << "No results for: " << query << "\n"; return; }

    std::cout << std::left << std::setw(25) << "PACKAGE"
              << std::setw(12) << "VERSION" << "DESCRIPTION\n";
    std::cout << std::string(60, '-') << "\n";
    for (const auto& r : results)
        std::cout << std::left << std::setw(25) << r.name
                  << std::setw(12) << r.version << r.description << "\n";
}

} // namespace repo
