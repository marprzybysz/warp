#include "db.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace db {

namespace fs = std::filesystem;
fs::path db_root;

void init(const fs::path& path) {
    db_root = path;
    fs::create_directories(db_root);
}

bool exists(const std::string& name) {
    return fs::is_directory(db_root / name);
}

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

void save(const std::string& name,
          const std::string& version,
          const std::string& source,
          const std::vector<std::string>& files) {
    fs::path pkg_dir = db_root / name;
    fs::create_directories(pkg_dir);

    std::ofstream info(pkg_dir / "WARPINFO");
    info << "name="      << name      << "\n"
         << "version="   << version   << "\n"
         << "installed=" << now_iso() << "\n"
         << "source="    << source    << "\n";

    std::ofstream src_f(pkg_dir / "SOURCE");
    src_f << source << "\n";

    if (!files.empty()) {
        std::ofstream fl(pkg_dir / "FILES");
        for (const auto& f : files)
            fl << f << "\n";
    }
}

std::string get_info(const std::string& name) {
    std::ifstream f(db_root / name / "WARPINFO");
    if (!f.is_open()) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> get_files(const std::string& name) {
    std::vector<std::string> result;
    std::ifstream f(db_root / name / "FILES");
    if (!f.is_open()) return result;
    std::string line;
    while (std::getline(f, line))
        if (!line.empty()) result.push_back(line);
    return result;
}

void remove(const std::string& name) {
    fs::remove_all(db_root / name);
}

std::vector<PkgEntry> list_all() {
    std::vector<PkgEntry> result;
    if (!fs::is_directory(db_root)) return result;

    for (const auto& entry : fs::directory_iterator(db_root)) {
        if (!entry.is_directory()) continue;
        PkgEntry pkg;
        pkg.name = entry.path().filename().string();

        std::ifstream info(entry.path() / "WARPINFO");
        std::string line;
        while (std::getline(info, line)) {
            if (line.rfind("version=", 0) == 0)
                pkg.version = line.substr(8);
        }
        if (pkg.version.empty()) pkg.version = "unknown";
        result.push_back(std::move(pkg));
    }
    return result;
}

std::string owner_of(const std::string& filepath) {
    if (!fs::is_directory(db_root)) return "";
    for (const auto& pkg_dir : fs::directory_iterator(db_root)) {
        if (!pkg_dir.is_directory()) continue;
        auto files_path = pkg_dir.path() / "FILES";
        std::ifstream f(files_path);
        std::string line;
        while (std::getline(f, line))
            if (line == filepath)
                return pkg_dir.path().filename().string();
    }
    return "";
}

} // namespace db
