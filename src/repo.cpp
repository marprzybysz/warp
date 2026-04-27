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
#include <curl/curl.h>

namespace repo {

namespace fs = std::filesystem;

static fs::path index_dir()  { return fs::path(config::cfg.cache_dir) / "index"; }
static fs::path pkg_cache()  { return fs::path(config::cfg.cache_dir) / "packages"; }
static fs::path index_file() { return index_dir() / "INDEX"; }

// libcurl write callback — writes data to a file
static size_t write_file(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

// libcurl progress callback — updates tui progress bar
static int progress_cb(void* /*client*/,
                        curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    if (dltotal > 0) {
        int pct = static_cast<int>(dlnow * 100 / dltotal);
        tui::progress_bar(pct);
    }
    return 0;
}

static bool curl_download(const std::string& url, const fs::path& dest) {
    fs::create_directories(dest.parent_path());

    FILE* f = fopen(dest.c_str(), "wb");
    if (!f) return false;

    CURL* curl = curl_easy_init();
    if (!curl) { fclose(f); return false; }

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        static_cast<long>(config::cfg.timeout));
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,    1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,     0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "warp/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(f);

    if (res != CURLE_OK) {
        fs::remove(dest);
        return false;
    }
    return true;
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

static std::string sha256sum(const fs::path& file) {
    std::array<char, 128> buf{};
    FILE* p = popen(("sha256sum " + file.string() + " | cut -d' ' -f1").c_str(), "r");
    if (!p) return "";
    fgets(buf.data(), buf.size(), p);
    pclose(p);
    std::string s(buf.data());
    s.erase(s.find_last_not_of(" \n\r") + 1);
    return s;
}

void sync() {
    std::string url = config::cfg.repo + "/INDEX";
    tui::log_step("Syncing from " + config::cfg.repo + "...");
    fs::create_directories(index_dir());

    if (!curl_download(url, index_file()))
        tui::done_err("Cannot fetch INDEX from " + url);
    tui::clear_progress();

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
    std::string expected_sha = index_get(pkg, "sha256");
    if (file_rel.empty())
        tui::done_err("No file entry for '" + pkg + "' in INDEX");

    fs::path cached = pkg_cache() / fs::path(file_rel).filename();

    if (!fs::exists(cached)) {
        std::string url = config::cfg.repo + "/" + file_rel;
        tui::log_step("Downloading " + pkg + " " + version + "...");
        fs::create_directories(pkg_cache());

        if (!curl_download(url, cached))
            tui::done_err("Download failed: " + url);

        tui::clear_progress();
        tui::log_step("Downloaded " + pkg + "...", "ok");
    } else {
        tui::log_step("From cache: " + cached.filename().string(), "ok");
    }

    if (!expected_sha.empty()) {
        tui::log_step("Verifying checksum...");
        std::string actual = sha256sum(cached);
        if (actual != expected_sha) {
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
        std::string inst  = db::get_version(pkg);
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
        } else if (line.rfind("version=", 0) == 0)
            cur.version = line.substr(8);
        else if (line.rfind("description=", 0) == 0)
            cur.description = line.substr(12);
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
