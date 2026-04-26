#include "install.h"
#include "tui.h"
#include "db.h"
#include "format.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

namespace install {

namespace fs = std::filesystem;

static std::string read_warpinfo_field(const fs::path& info, const std::string& key) {
    std::ifstream f(info);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key + "=", 0) == 0)
            return line.substr(key.size() + 1);
    }
    return "";
}

static std::vector<std::string> collect_files(const fs::path& dir) {
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(dir))
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    return files;
}

static void copy_with_progress(const std::vector<std::string>& src_files,
                                const fs::path& src_root,
                                const fs::path& dest_root,
                                const std::string& pkg_name) {
    int total = static_cast<int>(src_files.size());
    int count = 0;
    for (const auto& src : src_files) {
        fs::path rel = fs::relative(src, src_root);
        fs::path dst = dest_root / rel;
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        ++count;
        tui::progress_bar(count * 100 / total, "Installing " + pkg_name);
    }
    tui::clear_progress();
}

void from_warp(const fs::path& file) {
    tui::log_step("Detecting format...", "ok");

    fs::path tmpdir = format::extract_to_tmp(file);

    fs::path info_path = tmpdir / "WARPINFO";
    if (!fs::exists(info_path)) {
        fs::remove_all(tmpdir);
        tui::done_err("WARPINFO missing from package");
    }

    std::string name    = read_warpinfo_field(info_path, "name");
    std::string version = read_warpinfo_field(info_path, "version");
    tui::log_step("Reading metadata (" + name + " " + version + ")...", "ok");

    // Check deps
    fs::path deps_path = tmpdir / "DEPS";
    if (fs::exists(deps_path)) {
        std::ifstream df(deps_path);
        std::string deps_line;
        if (std::getline(df, deps_line) && !deps_line.empty()) {
            tui::log_step("Checking dependencies...");
            std::vector<std::string> missing;
            std::istringstream ss(deps_line);
            std::string dep;
            while (std::getline(ss, dep, ',')) {
                dep.erase(0, dep.find_first_not_of(' '));
                dep.erase(dep.find_last_not_of(' ') + 1);
                if (!dep.empty() && !db::exists(dep))
                    missing.push_back(dep);
            }
            if (!missing.empty()) {
                std::string m;
                for (const auto& d : missing) m += d + " ";
                tui::warn("Missing dependencies: " + m);
            } else {
                tui::log_step("Dependencies satisfied...", "ok");
            }
        }
    }

    // Run INSTALL script if present
    fs::path install_script = tmpdir / "INSTALL";
    if (fs::exists(install_script)) {
        tui::log_step("Running install script...");
        fs::permissions(install_script, fs::perms::owner_exec, fs::perm_options::add);
        int ret = std::system(install_script.c_str());
        if (ret != 0) {
            fs::remove_all(tmpdir);
            tui::done_err("INSTALL script failed");
        }
        tui::log_step("Install script...", "ok");
    }

    // Copy files
    fs::path files_dir = tmpdir / "files";
    std::vector<std::string> installed_files;
    if (fs::is_directory(files_dir)) {
        auto src_files = collect_files(files_dir);
        copy_with_progress(src_files, files_dir, "/", name);
        tui::log_step("Copying files...", "ok");

        for (const auto& src : src_files) {
            std::string rel = fs::relative(src, files_dir).string();
            installed_files.push_back("/" + rel);
        }
    }

    db::save(name, version, "warp", installed_files);
    tui::log_step("Registering package...", "ok");
    fs::remove_all(tmpdir);
}

void from_tarxz(const fs::path& file) {
    tui::log_step("Detecting format...", "ok");
    tui::warn("No WARPINFO — raw mode");
    tui::println("");

    std::string name    = format::name_from_file(file);
    std::string version = format::version_from_file(file);
    tui::log_step("Extracting " + name + "...");

    fs::path tmpdir = format::extract_to_tmp(file);
    auto src_files  = collect_files(tmpdir);

    int total = static_cast<int>(src_files.size());
    int count = 0;
    std::vector<std::string> installed_files;

    for (const auto& src : src_files) {
        std::string rel = fs::relative(src, tmpdir).string();
        fs::path dst = fs::path("/") / rel;
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        installed_files.push_back(dst.string());
        ++count;
        tui::progress_bar(count * 100 / total, "Extracting");
    }

    tui::clear_progress();
    tui::log_step("Extraction...", "ok");

    db::save(name, version, "tarxz", installed_files);
    tui::log_step("Registering package...", "ok");
    tui::println("");
    tui::println("Installed to: /usr/local (paths from archive)");

    fs::remove_all(tmpdir);
}

} // namespace install
