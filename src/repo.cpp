#include "repo.h"
#include "tui.h"
#include "config.h"
#include "db.h"
#include "install.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <array>
#include <iomanip>

namespace repo {

namespace fs = std::filesystem;

static fs::path index_dir() {
    return fs::path(config::cfg.cache_dir) / "index";
}

static fs::path pkg_cache_dir() {
    return fs::path(config::cfg.cache_dir) / "packages";
}

static fs::path index_file() {
    return index_dir() / "INDEX";
}

static std::string download(const std::string& url, const fs::path& dest) {
    fs::create_directories(dest.parent_path());
    std::string cmd = "curl -fsSL --max-time " +
                      std::to_string(config::cfg.timeout) +
                      " -o " + dest.string() + " " + url + " 2>&1";
    std::array<char, 256> buf{};
    std::string output;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "popen failed";
    while (fgets(buf.data(), buf.size(), p))
        output += buf.data();
    int ret = pclose(p);
    if (ret != 0) return output.empty() ? "download failed" : output;
    return "";
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

    std::string err = download(url, index_file());
    if (!err.empty())
        tui::done_err("Cannot fetch INDEX: " + err);

    // Count packages
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

    // Check package exists in index
    std::string version = index_get(pkg, "version");
    if (version.empty())
        tui::done_err("Package '" + pkg + "' not found in repository");

    tui::log_step("Found: " + pkg + " " + version, "ok");

    // Already installed?
    if (db::exists(pkg)) {
        std::string inst_ver = db::get_info(pkg);
        // Extract version from info string
        for (auto& line : std::vector<std::string>{}) { (void)line; }
        tui::log_step(pkg + " already installed, use -U to update...", "ok");
        return;
    }

    std::string file_rel = index_get(pkg, "file");
    std::string sha256   = index_get(pkg, "sha256");

    if (file_rel.empty())
        tui::done_err("No file entry for '" + pkg + "' in INDEX");

    fs::path cached = pkg_cache_dir() / fs::path(file_rel).filename();

    if (!fs::exists(cached)) {
        std::string url = config::cfg.repo + "/" + file_rel;
        tui::log_step("Downloading " + pkg + " " + version + "...");
        fs::create_directories(pkg_cache_dir());

        // Download with progress
        std::string cmd = "curl -fL --max-time " +
                          std::to_string(config::cfg.timeout) +
                          " --progress-bar -o " + cached.string() + " " + url;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            fs::remove(cached);
            tui::done_err("Download failed: " + url);
        }
        tui::clear_progress();
        tui::log_step("Downloaded " + pkg + "...", "ok");
    } else {
        tui::log_step("From cache: " + cached.filename().string(), "ok");
    }

    // Verify SHA256
    if (!sha256.empty()) {
        tui::log_step("Verifying checksum...");
        std::string cmd = "sha256sum " + cached.string() + " | cut -d' ' -f1";
        std::array<char, 128> buf{};
        FILE* p = popen(cmd.c_str(), "r");
        std::string actual;
        if (p) { fgets(buf.data(), buf.size(), p); pclose(p); actual = buf.data(); }
        actual.erase(actual.find_last_not_of(" \n\r") + 1);
        if (actual != sha256) {
            fs::remove(cached);
            tui::done_err("SHA256 mismatch — corrupted file");
        }
        tui::log_step("SHA256 OK...", "ok");
    }

    install::from_warp(cached);
}

void search(const std::string& query) {
    if (query.empty()) tui::done_err("Provide a search query");
    if (!fs::exists(index_file()))
        tui::done_err("INDEX not found — run: warp --sync");

    std::ifstream f(index_file());
    std::string line, current_pkg;
    struct Result { std::string name, version, description; };
    std::vector<Result> results;
    Result cur;

    while (std::getline(f, line)) {
        if (!line.empty() && line.front() == '[') {
            if (!cur.name.empty() &&
                (cur.name.find(query) != std::string::npos ||
                 cur.description.find(query) != std::string::npos))
                results.push_back(cur);
            cur = { line.substr(1, line.size() - 2), "", "" };
        } else if (line.rfind("version=", 0) == 0) {
            cur.version = line.substr(8);
        } else if (line.rfind("description=", 0) == 0) {
            cur.description = line.substr(12);
        }
    }
    // Check last entry
    if (!cur.name.empty() &&
        (cur.name.find(query) != std::string::npos ||
         cur.description.find(query) != std::string::npos))
        results.push_back(cur);

    if (results.empty()) {
        std::cout << "No results for: " << query << "\n";
        return;
    }

    std::cout << std::left << std::setw(25) << "PACKAGE"
              << std::setw(12) << "VERSION" << "DESCRIPTION\n";
    std::cout << std::string(60, '-') << "\n";
    for (const auto& r : results)
        std::cout << std::left << std::setw(25) << r.name
                  << std::setw(12) << r.version << r.description << "\n";
}

} // namespace repo
