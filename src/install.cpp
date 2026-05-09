#include "install.h"
#include "repo.h"
#include "tui.h"
#include "db.h"
#include "format.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>

namespace install {

namespace fs = std::filesystem;

static std::string read_field(const fs::path& info, const std::string& key) {
    std::ifstream f(info);
    std::string line;
    while (std::getline(f, line))
        if (line.rfind(key + "=", 0) == 0)
            return line.substr(key.size() + 1);
    return "";
}

static std::vector<std::string> collect_files(const fs::path& dir) {
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    return files;
}

void from_warp(const fs::path& file, bool resolve_deps) {
    std::string size = tui::format_size(file.string());

    fs::path tmpdir = format::extract_to_tmp(file);

    fs::path info_path = tmpdir / "WARPINFO";
    if (!fs::exists(info_path)) {
        fs::remove_all(tmpdir);
        tui::done_err("WARPINFO missing from package");
    }

    std::string name    = read_field(info_path, "name");
    std::string version = read_field(info_path, "version");

    if (!tui::quiet)
        std::cout << "Selecting " << name << " " << version << " [" << size << "]\n";

    tui::progress_bar(10, "Resolving  " + name);

    // Resolve and auto-install deps from the package's own DEPS file.
    // Skipped when called from repo::install which already resolved the full tree.
    if (resolve_deps) {
        fs::path deps_path = tmpdir / "DEPS";
        if (fs::exists(deps_path)) {
            std::ifstream df(deps_path);
            std::string deps_line;
            if (std::getline(df, deps_line) && !deps_line.empty()) {
                std::istringstream ss(deps_line);
                std::string dep;
                while (std::getline(ss, dep, ',')) {
                    dep.erase(0, dep.find_first_not_of(' '));
                    dep.erase(dep.find_last_not_of(' ') + 1);
                    if (dep.empty() || db::exists(dep)) continue;
                    tui::log_step("Installing dependency: " + dep + "...");
                    repo::install(dep);
                }
            }
        }
    }
    tui::progress_bar(35, "Extracting " + name);

    // Run INSTALL script if present
    fs::path install_script = tmpdir / "INSTALL";
    if (fs::exists(install_script)) {
        tui::progress_bar(45, "Configuring" + name);
        fs::permissions(install_script, fs::perms::owner_exec, fs::perm_options::add);
        if (std::system(install_script.c_str()) != 0) {
            fs::remove_all(tmpdir);
            tui::done_err("INSTALL script failed");
        }
    }
    tui::progress_bar(50, "Installing " + name);

    // Copy files
    fs::path files_dir = tmpdir / "files";
    std::vector<std::string> installed_files;
    if (fs::is_directory(files_dir)) {
        auto src_files = collect_files(files_dir);
        int total = static_cast<int>(src_files.size());

        // Conflict detection — check if any file is owned by another package
        std::vector<std::string> conflicts;
        for (const auto& src : src_files) {
            fs::path dst = fs::path("/") / fs::relative(src, files_dir);
            if (!fs::exists(dst)) continue;
            std::string owner = db::owner_of(dst.string());
            if (!owner.empty() && owner != name)
                conflicts.push_back(dst.string() + " (owned by " + owner + ")");
        }
        if (!conflicts.empty()) {
            fs::remove_all(tmpdir);
            std::string msg = "File conflicts detected:\n";
            for (const auto& c : conflicts) msg += "  " + c + "\n";
            tui::done_err(msg);
        }

        int count = 0;
        try {
            for (const auto& src : src_files) {
                fs::path rel = fs::relative(src, files_dir);
                fs::path dst = fs::path("/") / rel;
                fs::create_directories(dst.parent_path());
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
                installed_files.push_back(dst.string());
                ++count;
                tui::progress_bar(50 + count * 40 / total);
            }
        } catch (const std::exception& ex) {
            for (const auto& f : installed_files) {
                std::error_code ec;
                fs::remove(f, ec);
            }
            fs::remove_all(tmpdir);
            tui::done_err("Install failed, rolled back: " + std::string(ex.what()));
        }
    }

    tui::progress_bar(95, "Registering" + name);
    db::save(name, version, "warp", installed_files);
    tui::progress_bar(100, "Done       " + name);
    db::log("install", name, version);
    fs::remove_all(tmpdir);
    tui::done_ok();
}

void from_tarxz(const fs::path& file) {
    std::string size = tui::format_size(file.string());
    tui::warn("No WARPINFO — raw mode");

    std::string name    = format::name_from_file(file);
    std::string version = format::version_from_file(file);

    if (!tui::quiet)
        std::cout << "Selecting " << name << " " << version << " [" << size << "]\n";

    fs::path tmpdir = format::extract_to_tmp(file);
    auto src_files  = collect_files(tmpdir);

    int total = static_cast<int>(src_files.size());
    int count = 0;
    std::vector<std::string> installed_files;

    tui::progress_bar(10);
    for (const auto& src : src_files) {
        fs::path rel = fs::relative(src, tmpdir);
        fs::path dst = fs::path("/") / rel;
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        installed_files.push_back(dst.string());
        ++count;
        tui::progress_bar(10 + count * 80 / total);
    }

    tui::progress_bar(95);
    db::save(name, version, "tarxz", installed_files);
    tui::progress_bar(100);
    db::log("install", name, version);
    fs::remove_all(tmpdir);
    tui::done_ok();
}

} // namespace install
