#include "build.h"
#include "tui.h"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#include <regex>
#include <cstdio>
#include <array>

namespace build {

namespace fs = std::filesystem;

static std::string read_field(const fs::path& info, const std::string& key) {
    std::ifstream f(info);
    std::string line;
    while (std::getline(f, line))
        if (line.rfind(key + "=", 0) == 0)
            return line.substr(key.size() + 1);
    return "";
}

static std::string prompt(const std::string& label, const std::string& def = "") {
    if (!def.empty())
        std::cout << "  " << label << " [" << def << "]: ";
    else
        std::cout << "  " << label << ": ";
    std::string val;
    std::getline(std::cin, val);
    return val.empty() ? def : val;
}

// Run ldd on a binary, return set of library basenames
static std::set<std::string> ldd_deps(const fs::path& bin) {
    std::set<std::string> deps;
    std::string cmd = "ldd " + bin.string() + " 2>/dev/null";
    std::array<char, 256> buf{};
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return deps;
    while (fgets(buf.data(), buf.size(), pipe)) {
        std::string line(buf.data());
        // Extract library name like libgtk-3.so.0
        static const std::regex lib_re(R"(lib(\S+)\.so)");
        std::smatch m;
        if (std::regex_search(line, m, lib_re))
            deps.insert(m[1].str());
    }
    pclose(pipe);
    return deps;
}

static bool is_elf(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    char magic[4]{};
    f.read(magic, 4);
    return magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

static void pack_warp(const fs::path& src_dir, const std::string& output_name) {
    struct archive* a = archive_write_new();
    archive_write_add_filter_xz(a);
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, output_name.c_str());

    struct archive* disk = archive_read_disk_new();
    archive_read_disk_set_standard_lookup(disk);

    for (const auto& entry_path : fs::recursive_directory_iterator(src_dir)) {
        struct archive_entry* entry = archive_entry_new();
        std::string rel = fs::relative(entry_path.path(), src_dir).string();
        archive_entry_set_pathname(entry, rel.c_str());
        archive_read_disk_entry_from_file(disk, entry, -1, nullptr);
        archive_write_header(a, entry);

        if (entry_path.is_regular_file()) {
            std::ifstream f(entry_path.path(), std::ios::binary);
            std::vector<char> buf(8192);
            while (f.read(buf.data(), buf.size()) || f.gcount())
                archive_write_data(a, buf.data(), static_cast<size_t>(f.gcount()));
        }
        archive_entry_free(entry);
    }

    archive_read_free(disk);
    archive_write_close(a);
    archive_write_free(a);
}

void create_pkg(const fs::path& src_dir_in) {
    if (!fs::is_directory(src_dir_in))
        tui::done_err("Not a directory: " + src_dir_in.string());

    fs::path src_dir = src_dir_in;
    fs::path files_dir = src_dir / "files";

    // If no files/ subdir, treat whole folder as files/
    fs::path tmp_wrap;
    if (!fs::is_directory(files_dir)) {
        tui::warn("No files/ subdir — treating entire folder as files/");
        tmp_wrap = fs::temp_directory_path() / ("warp-build." + std::to_string(getpid()));
        fs::create_directories(tmp_wrap / "files");
        for (const auto& e : fs::directory_iterator(src_dir))
            fs::copy(e.path(), tmp_wrap / "files" / e.path().filename(),
                     fs::copy_options::recursive);
        src_dir = tmp_wrap;
        files_dir = src_dir / "files";
    }

    tui::log_step("Checking structure...", "ok");

    // Read or create WARPINFO
    fs::path info_path = src_dir / "WARPINFO";
    std::string name, version, arch = "x86_64", maintainer, license, description;

    if (fs::exists(info_path)) {
        name        = read_field(info_path, "name");
        version     = read_field(info_path, "version");
        arch        = read_field(info_path, "arch");
        maintainer  = read_field(info_path, "maintainer");
        license     = read_field(info_path, "license");
        description = read_field(info_path, "description");
        tui::log_step("Read WARPINFO (" + name + " " + version + ")...", "ok");
    } else {
        tui::println("No WARPINFO found — please fill in metadata:\n");
        name        = prompt("name");
        version     = prompt("version");
        arch        = prompt("arch", "x86_64");
        maintainer  = prompt("maintainer");
        license     = prompt("license");
        description = prompt("description");
        tui::println("");
    }

    if (name.empty() || version.empty())
        tui::done_err("name and version are required");

    // Auto-detect deps via ldd
    tui::log_step("Detecting dependencies (ldd)...");
    std::set<std::string> all_deps;
    for (const auto& entry : fs::recursive_directory_iterator(files_dir)) {
        if (entry.is_regular_file() && is_elf(entry.path())) {
            auto d = ldd_deps(entry.path());
            all_deps.insert(d.begin(), d.end());
        }
    }

    std::string deps_str;
    for (const auto& d : all_deps)
        deps_str += (deps_str.empty() ? "" : ",") + d;

    if (!deps_str.empty())
        tui::log_step("Detected: " + deps_str, "ok");
    else
        tui::log_step("No dependencies detected...", "ok");

    if (!tui::quiet) {
        std::cout << "  Dependencies [" << deps_str << "]: ";
        std::string user_deps;
        std::getline(std::cin, user_deps);
        if (!user_deps.empty()) deps_str = user_deps;
        std::cout << "\n";
    }

    // Write WARPINFO and DEPS
    {
        std::ofstream info(info_path);
        info << "name="        << name        << "\n"
             << "version="     << version     << "\n"
             << "arch="        << arch        << "\n"
             << "deps="        << deps_str    << "\n"
             << "maintainer="  << maintainer  << "\n"
             << "license="     << license     << "\n"
             << "description=" << description << "\n";
    }
    {
        std::ofstream deps_f(src_dir / "DEPS");
        deps_f << deps_str << "\n";
    }
    tui::log_step("Saved WARPINFO and DEPS...", "ok");

    // Pack
    std::string output = name + "-" + version + "-" + arch + ".wrp";
    tui::log_step("Packing " + output + "...");
    pack_warp(src_dir, output);
    tui::log_step("Package ready: " + output, "ok");

    // SHA256
    std::string sha_cmd = "sha256sum " + output + " | cut -d' ' -f1";
    std::array<char, 128> sha_buf{};
    std::string sha256;
    FILE* p = popen(sha_cmd.c_str(), "r");
    if (p) { fgets(sha_buf.data(), sha_buf.size(), p); pclose(p); sha256 = sha_buf.data(); }
    sha256.erase(sha256.find_last_not_of(" \n\r") + 1);

    std::ofstream sha_f(output + ".sha256");
    sha_f << sha256 << "  " << output << "\n";
    tui::log_step("SHA256: " + sha256.substr(0, 16) + "...", "ok");

    if (!tmp_wrap.empty()) fs::remove_all(tmp_wrap);

    tui::println("");
    tui::println("Size: " + std::to_string(fs::file_size(output) / 1024) + " KB");
    tui::println("File: " + output);
}

} // namespace build
