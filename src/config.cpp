#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace config {

Config cfg;

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

static bool to_bool(const std::string& v) {
    return v == "true" || v == "1" || v == "yes";
}

void load(const std::filesystem::path& path) {
    namespace fs = std::filesystem;

    std::vector<fs::path> candidates;
    if (!path.empty())
        candidates.push_back(path);
    candidates.push_back("/etc/warp/warp.conf");

    // Also try bundled conf next to the binary (dev mode)
    if (const char* self = getenv("WARP_CONF"))
        candidates.insert(candidates.begin(), fs::path(self));

    std::ifstream f;
    for (const auto& p : candidates) {
        f.open(p);
        if (f.is_open()) break;
    }
    if (!f.is_open()) return;

    std::string section, line;
    while (std::getline(f, line)) {
        // strip comment
        auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);
        line = trim(line);
        if (line.empty()) continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == "core") {
            if (key == "language") cfg.language = val;
            if (key == "color")    cfg.color    = to_bool(val);
            if (key == "confirm")  cfg.confirm  = to_bool(val);
        } else if (section == "network") {
            if (key == "repo")    cfg.repo    = val;
            if (key == "mirrors") cfg.mirrors  = std::stoi(val);
            if (key == "timeout") cfg.timeout  = std::stoi(val);
        } else if (section == "output") {
            if (key == "quiet")        cfg.quiet    = to_bool(val);
            if (key == "progress_bar") cfg.progress = to_bool(val);
            if (key == "summary")      cfg.summary  = to_bool(val);
        } else if (section == "cache") {
            if (key == "dir")       cfg.cache_dir  = val;
            if (key == "keep_days") cfg.keep_days  = std::stoi(val);
        }
    }
}

} // namespace config
