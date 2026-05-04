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
#include <algorithm>
#include <array>
#include <cstdio>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

namespace repo {

namespace fs = std::filesystem;

static fs::path repos_dir()  { return fs::path("/etc/warp/repos.d"); }
static fs::path pkg_cache()  { return fs::path(config::cfg.cache_dir) / "packages"; }
static fs::path index_root() { return fs::path(config::cfg.cache_dir) / "index"; }

struct RepoEntry {
    int         n;
    std::string url;
};

// Load all repos from /etc/warp/repos.d/<n>.conf
static std::vector<RepoEntry> load_repos() {
    std::vector<RepoEntry> repos;

    if (!fs::is_directory(repos_dir())) {
        // Fallback to single repo from warp.conf
        repos.push_back({1, config::cfg.repo});
        return repos;
    }

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(repos_dir()))
        if (e.path().extension() == ".conf")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        std::ifstream in(f);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("url=", 0) == 0) {
                std::string stem = f.stem().string();
                int n = 1;
                try { n = std::stoi(stem); } catch (...) {}
                repos.push_back({n, line.substr(4)});
                break;
            }
        }
    }

    if (repos.empty())
        repos.push_back({1, config::cfg.repo});

    return repos;
}

static fs::path index_for(int n) {
    return index_root() / std::to_string(n) / "INDEX";
}

// libcurl write callback
static size_t write_file(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

// libcurl progress callback
static int progress_cb(void*, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    if (dltotal > 0)
        tui::progress_bar(static_cast<int>(dlnow * 100 / dltotal));
    return 0;
}

static bool curl_download(const std::string& url, const fs::path& dest) {
    fs::create_directories(dest.parent_path());
    FILE* f = fopen(dest.c_str(), "wb");
    if (!f) return false;

    CURL* curl = curl_easy_init();
    if (!curl) { fclose(f); return false; }

    curl_easy_setopt(curl, CURLOPT_URL,              url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,          static_cast<long>(config::cfg.timeout));
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,      1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,        "warp/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(f);

    if (res != CURLE_OK) { fs::remove(dest); return false; }
    return true;
}

// Search field in a specific INDEX file
static std::string index_get_file(const fs::path& idx, const std::string& pkg, const std::string& field) {
    std::ifstream f(idx);
    if (!f.is_open()) return "";
    bool in_pkg = false;
    std::string line, section = "[" + pkg + "]";
    while (std::getline(f, line)) {
        if (line == section)         { in_pkg = true; continue; }
        if (in_pkg) {
            if (!line.empty() && line.front() == '[') break;
            if (line.rfind(field + "=", 0) == 0)
                return line.substr(field.size() + 1);
        }
    }
    return "";
}

// Search all repos in order, return {value, repo_entry}
static std::pair<std::string, RepoEntry> index_get_all(
        const std::vector<RepoEntry>& repos,
        const std::string& pkg, const std::string& field) {
    for (const auto& r : repos) {
        fs::path idx = index_for(r.n);
        if (!fs::exists(idx)) continue;
        std::string val = index_get_file(idx, pkg, field);
        if (!val.empty()) return {val, r};
    }
    return {"", {}};
}

static std::string sha256sum_file(const fs::path& file) {
    std::array<char, 128> buf{};
    FILE* p = popen(("sha256sum " + file.string() + " | cut -d' ' -f1").c_str(), "r");
    if (!p) return "";
    fgets(buf.data(), buf.size(), p);
    pclose(p);
    std::string s(buf.data());
    s.erase(s.find_last_not_of(" \n\r") + 1);
    return s;
}

// ─── Public API ──────────────────────────────────────────────────────────────

void sync() {
    auto repos = load_repos();
    for (const auto& r : repos) {
        tui::log_step("Syncing " + r.url + "...");
        fs::path dest = index_for(r.n);
        fs::create_directories(dest.parent_path());

        if (!curl_download(r.url + "/INDEX", dest))
            tui::done_err("Cannot fetch INDEX from " + r.url);
        tui::clear_progress();

        std::ifstream f(dest);
        int count = 0;
        std::string line;
        while (std::getline(f, line))
            if (!line.empty() && line.front() == '[') ++count;

        tui::log_step("Fetched INDEX (" + std::to_string(count) + " packages)...", "ok");
    }
}

void install(const std::string& pkg) {
    if (pkg.empty()) tui::done_err("Provide a package name");

    auto repos = load_repos();
    auto [version, repo_entry] = index_get_all(repos, pkg, "version");
    if (version.empty())
        tui::done_err("Package '" + pkg + "' not found — run: warp --sync");

    tui::log_step("Found: " + pkg + " " + version, "ok");

    if (db::exists(pkg) && db::get_version(pkg) == version) {
        tui::log_step(pkg + " " + version + " already installed...", "ok");
        return;
    }

    std::string file_rel    = index_get_file(index_for(repo_entry.n), pkg, "file");
    std::string expected_sha = index_get_file(index_for(repo_entry.n), pkg, "sha256");

    if (file_rel.empty())
        tui::done_err("No file entry for '" + pkg + "' in INDEX");

    fs::path cached = pkg_cache() / fs::path(file_rel).filename();

    if (!fs::exists(cached)) {
        std::string url = repo_entry.url + "/" + file_rel;
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
        if (sha256sum_file(cached) != expected_sha) {
            fs::remove(cached);
            tui::done_err("SHA256 mismatch — corrupted file");
        }
        tui::log_step("SHA256 OK...", "ok");
    }

    ::install::from_warp(cached);
}

void list_updates() {
    auto repos = load_repos();
    bool any_index = false;
    for (const auto& r : repos)
        if (fs::exists(index_for(r.n))) { any_index = true; break; }
    if (!any_index) tui::done_err("No INDEX found — run: warp --sync");

    struct Update { std::string name, installed, available; };
    std::vector<Update> updates;

    for (const auto& pkg : db::list_names()) {
        std::string inst  = db::get_version(pkg);
        auto [avail, _]   = index_get_all(repos, pkg, "version");
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
    auto repos = load_repos();
    int upgraded = 0;

    for (const auto& pkg : db::list_names()) {
        std::string inst = db::get_version(pkg);
        auto [avail, _]  = index_get_all(repos, pkg, "version");
        if (avail.empty() || avail == inst) continue;

        tui::log_step("Upgrading " + pkg + ": " + inst + " → " + avail + "...");
        remove_pkg::remove(pkg, false);
        install(pkg);
        db::log("upgrade", pkg, avail);
        ++upgraded;
    }

    std::cout << "\n";
    if (upgraded == 0) std::cout << "Nothing to upgrade.\n";
    else               tui::done_ok();
}

void search(const std::string& query) {
    if (query.empty()) tui::done_err("Provide a search query");

    auto repos = load_repos();
    struct Result { std::string name, version, description; };
    std::vector<Result> results;

    for (const auto& r : repos) {
        fs::path idx = index_for(r.n);
        if (!fs::exists(idx)) continue;

        std::ifstream f(idx);
        Result cur;
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.front() == '[') {
                if (!cur.name.empty() &&
                    (cur.name.find(query)        != std::string::npos ||
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
    }

    if (results.empty()) { std::cout << "No results for: " << query << "\n"; return; }

    std::cout << std::left << std::setw(25) << "PACKAGE"
              << std::setw(12) << "VERSION" << "DESCRIPTION\n";
    std::cout << std::string(60, '-') << "\n";
    for (const auto& r : results)
        std::cout << std::left << std::setw(25) << r.name
                  << std::setw(12) << r.version << r.description << "\n";
}

void list_repos() {
    auto repos = load_repos();
    if (repos.empty()) { std::cout << "No repositories configured.\n"; return; }

    std::cout << std::left << std::setw(4) << "  #"
              << std::setw(45) << "URL" << "INDEX\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& r : repos) {
        fs::path idx = index_for(r.n);
        std::string status = fs::exists(idx) ? "synced" : "not synced";
        std::cout << "  " << std::left << std::setw(2) << r.n << " "
                  << std::setw(45) << r.url << status << "\n";
    }
}

void add_repo(const std::string& url) {
    if (url.empty()) tui::done_err("Provide a repository URL");

    fs::create_directories(repos_dir());

    // Find next available number
    int next = 1;
    while (fs::exists(repos_dir() / (std::to_string(next) + ".conf")))
        ++next;

    std::ofstream f(repos_dir() / (std::to_string(next) + ".conf"));
    f << "url=" << url << "\n";

    std::cout << "Added repo #" << next << ": " << url << "\n";
    std::cout << "Run 'warp --sync' to fetch the package index.\n";
}

void gen_index(const fs::path& dir) {
    if (!fs::is_directory(dir))
        tui::done_err("Not a directory: " + dir.string());

    fs::path out = dir / "INDEX";
    std::ofstream idx(out);
    if (!idx) tui::done_err("Cannot write INDEX to: " + out.string());

    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".wrp") continue;

        // Extract WARPINFO from archive without full unpack
        struct archive* a = archive_read_new();
        archive_read_support_filter_xz(a);
        archive_read_support_format_tar(a);
        if (archive_read_open_filename(a, entry.path().c_str(), 10240) != ARCHIVE_OK) {
            archive_read_free(a);
            continue;
        }

        std::string warpinfo_data;
        struct archive_entry* ae;
        while (archive_read_next_header(a, &ae) == ARCHIVE_OK) {
            std::string pname = archive_entry_pathname(ae);
            if (pname == "./WARPINFO" || pname == "WARPINFO") {
                std::string chunk;
                chunk.resize(4096);
                la_ssize_t n = archive_read_data(a, chunk.data(), chunk.size());
                if (n > 0) warpinfo_data = chunk.substr(0, static_cast<size_t>(n));
                break;
            }
            archive_read_data_skip(a);
        }
        archive_read_free(a);

        if (warpinfo_data.empty()) continue;

        // Parse WARPINFO fields
        auto get_field = [&](const std::string& key) -> std::string {
            std::istringstream ss(warpinfo_data);
            std::string line;
            while (std::getline(ss, line))
                if (line.rfind(key + "=", 0) == 0)
                    return line.substr(key.size() + 1);
            return "";
        };

        std::string name    = get_field("name");
        std::string version = get_field("version");
        std::string arch    = get_field("arch");
        std::string deps    = get_field("deps");
        std::string desc    = get_field("description");
        std::string license = get_field("license");

        if (name.empty() || version.empty()) continue;

        // SHA256
        std::array<char, 128> sha_buf{};
        FILE* sp = popen(("sha256sum " + entry.path().string() + " | cut -d' ' -f1").c_str(), "r");
        std::string sha;
        if (sp) { fgets(sha_buf.data(), sha_buf.size(), sp); pclose(sp); sha = sha_buf.data(); }
        sha.erase(sha.find_last_not_of(" \n\r") + 1);

        uintmax_t size_kb = fs::file_size(entry.path()) / 1024;

        idx << "[" << name << "]\n"
            << "version="     << version << "\n"
            << "arch="        << arch    << "\n"
            << "file="        << entry.path().filename().string() << "\n"
            << "sha256="      << sha     << "\n"
            << "size="        << size_kb << "\n"
            << "deps="        << deps    << "\n"
            << "license="     << license << "\n"
            << "description=" << desc    << "\n"
            << "\n";

        tui::log_step("  " + name + " " + version, "ok");
        ++count;
    }

    tui::log_step("Generated INDEX with " + std::to_string(count) + " packages...", "ok");
    tui::println("File: " + out.string());
}

void remove_repo(int n) {
    if (n <= 0) tui::done_err("Provide a valid repository number (see: warp repo --list)");

    fs::path conf = repos_dir() / (std::to_string(n) + ".conf");
    if (!fs::exists(conf))
        tui::done_err("Repository #" + std::to_string(n) + " not found");

    // Read URL before deleting for display
    std::string url;
    { std::ifstream f(conf); std::string l;
      while (std::getline(f, l)) if (l.rfind("url=", 0) == 0) { url = l.substr(4); break; } }

    fs::remove(conf);

    // Remove cached index
    fs::path idx_dir = index_root() / std::to_string(n);
    if (fs::exists(idx_dir)) fs::remove_all(idx_dir);

    std::cout << "Removed repo #" << n << ": " << url << "\n";
}

} // namespace repo
