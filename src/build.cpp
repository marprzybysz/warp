#include "build.h"
#include "install.h"
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
#include <cstdlib>
#include <array>
#include <curl/curl.h>

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
        std::string rel  = fs::relative(entry_path.path(), src_dir).string();
        std::string full = entry_path.path().string();
        archive_entry_set_pathname(entry, rel.c_str());
        archive_entry_copy_sourcepath(entry, full.c_str());
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

// ─── build_from_source ───────────────────────────────────────────────────────

static size_t curl_write_file(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

static bool download_source(const std::string& url, const fs::path& dest) {
    FILE* f = fopen(dest.c_str(), "wb");
    if (!f) return false;
    CURL* curl = curl_easy_init();
    if (!curl) { fclose(f); return false; }
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "warp/0.1.0");
    curl_easy_setopt(curl, CURLOPT_CAINFO,         "/etc/ssl/certs/ca-certificates.crt");
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(f);
    if (res != CURLE_OK) { fs::remove(dest); return false; }
    return true;
}

static std::string sha256_of(const fs::path& file) {
    std::array<char, 128> buf{};
    FILE* p = popen(("sha256sum " + file.string() + " | cut -d' ' -f1").c_str(), "r");
    if (!p) return "";
    fgets(buf.data(), buf.size(), p);
    pclose(p);
    std::string s(buf.data());
    s.erase(s.find_last_not_of(" \n\r") + 1);
    return s;
}

static std::string warpbuild_field(const fs::path& wb, const std::string& key) {
    std::ifstream f(wb);
    std::string line;
    while (std::getline(f, line))
        if (line.rfind(key + "=", 0) == 0)
            return line.substr(key.size() + 1);
    return "";
}

void build_from_source(const fs::path& dir, bool install_after) {
    if (!fs::is_directory(dir))
        tui::done_err("Not a directory: " + dir.string());

    fs::path abs_dir  = fs::absolute(dir);
    fs::path warpbuild = abs_dir / "WARPBUILD";
    if (!fs::exists(warpbuild))
        tui::done_err("WARPBUILD not found in " + dir.string());

    tui::log_step("Reading WARPBUILD...");

    std::string pkgname     = warpbuild_field(warpbuild, "pkgname");
    std::string pkgver      = warpbuild_field(warpbuild, "pkgver");
    std::string arch        = warpbuild_field(warpbuild, "arch");
    std::string deps        = warpbuild_field(warpbuild, "deps");
    std::string makedeps    = warpbuild_field(warpbuild, "makedeps");
    std::string license     = warpbuild_field(warpbuild, "license");
    std::string description = warpbuild_field(warpbuild, "description");
    std::string source_url  = warpbuild_field(warpbuild, "source");
    std::string expected_sha = warpbuild_field(warpbuild, "sha256");
    if (arch.empty()) arch = "x86_64";

    if (pkgname.empty() || pkgver.empty())
        tui::done_err("pkgname and pkgver are required in WARPBUILD");

    tui::log_step("Package: " + pkgname + " " + pkgver, "ok");

    // Check makedeps
    if (!makedeps.empty()) {
        tui::log_step("Checking build dependencies...");
        std::istringstream ss(makedeps);
        std::string md;
        while (std::getline(ss, md, ',')) {
            md.erase(0, md.find_first_not_of(' '));
            md.erase(md.find_last_not_of(' ') + 1);
            if (md.empty()) continue;
            if (system(("command -v " + md + " >/dev/null 2>&1").c_str()) != 0)
                tui::done_err("Missing build dep: " + md + " — install it first");
        }
        tui::log_step("Build dependencies OK...", "ok");
    }

    // Workspace
    fs::path workdir = fs::temp_directory_path() / ("warp-build-src." + std::to_string(getpid()));
    fs::path srcdir  = workdir / "src";
    fs::path destdir = workdir / "dest";
    fs::create_directories(srcdir);
    fs::create_directories(destdir);

    // Download or copy source
    if (!source_url.empty()) {
        if (source_url.rfind("http", 0) == 0) {
            tui::log_step("Downloading source...");
            fs::path tarball = workdir / fs::path(source_url).filename();
            if (!download_source(source_url, tarball)) {
                fs::remove_all(workdir);
                tui::done_err("Failed to download: " + source_url);
            }
            tui::log_step("Source downloaded...", "ok");

            if (!expected_sha.empty()) {
                tui::log_step("Verifying checksum...");
                if (sha256_of(tarball) != expected_sha) {
                    fs::remove_all(workdir);
                    tui::done_err("SHA256 mismatch — corrupted source");
                }
                tui::log_step("SHA256 OK...", "ok");
            }

            tui::log_step("Extracting source...");
            std::string extract_cmd = "tar -xf " + tarball.string() +
                                      " -C " + srcdir.string() +
                                      " --strip-components=1 2>/dev/null || "
                                      "tar -xf " + tarball.string() +
                                      " -C " + srcdir.string() + " 2>/dev/null";
            if (system(extract_cmd.c_str()) != 0) {
                fs::remove_all(workdir);
                tui::done_err("Failed to extract source");
            }
            tui::log_step("Source extracted...", "ok");
        } else {
            // Local path
            fs::path local = abs_dir / source_url;
            if (fs::is_directory(local))
                for (const auto& e : fs::directory_iterator(local))
                    fs::copy(e.path(), srcdir / e.path().filename(), fs::copy_options::recursive);
            else if (fs::exists(local))
                system(("tar -xf " + local.string() + " -C " + srcdir.string() + " --strip-components=1 2>/dev/null || tar -xf " + local.string() + " -C " + srcdir.string()).c_str());
            else {
                fs::remove_all(workdir);
                tui::done_err("Source not found: " + local.string());
            }
            tui::log_step("Source ready...", "ok");
        }
    } else {
        for (const auto& e : fs::directory_iterator(dir))
            fs::copy(e.path(), srcdir / e.path().filename(),
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        tui::log_step("Using local directory as source...", "ok");
    }

    // Determine nproc
    std::array<char, 8> np_buf{};
    std::string nproc = "1";
    FILE* np = popen("nproc 2>/dev/null", "r");
    if (np) { fgets(np_buf.data(), np_buf.size(), np); pclose(np); nproc = np_buf.data(); }
    nproc.erase(nproc.find_last_not_of(" \n\r") + 1);

    // Set env
    setenv("DESTDIR",    destdir.c_str(), 1);
    setenv("PREFIX",     "/usr", 1);
    setenv("MAKEFLAGS",  ("-j" + nproc).c_str(), 1);

    // Run build()
    std::string wb_abs = warpbuild.string();
    tui::log_step("Building...");
    std::string build_cmd = "cd " + srcdir.string() +
                            " && source " + wb_abs +
                            " && build";
    if (system(("bash -c '" + build_cmd + "'").c_str()) != 0) {
        fs::remove_all(workdir);
        tui::done_err("build() function failed");
    }
    tui::log_step("Build complete...", "ok");

    // Run package()
    tui::log_step("Staging files...");
    std::string pkg_cmd = "cd " + srcdir.string() +
                          " && source " + wb_abs +
                          " && package";
    if (system(("bash -c '" + pkg_cmd + "'").c_str()) != 0) {
        fs::remove_all(workdir);
        tui::done_err("package() function failed");
    }
    tui::log_step("Files staged...", "ok");

    // Build .wrp structure
    tui::log_step("Creating package structure...");
    fs::path staging = workdir / "pkg";
    fs::create_directories(staging / "files");
    for (const auto& e : fs::directory_iterator(destdir))
        fs::copy(e.path(), staging / "files" / e.path().filename(),
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);

    {
        std::ofstream wi(staging / "WARPINFO");
        wi << "name="        << pkgname     << "\n"
           << "version="     << pkgver      << "\n"
           << "arch="        << arch        << "\n"
           << "deps="        << deps        << "\n"
           << "license="     << license     << "\n"
           << "description=" << description << "\n";
    }
    { std::ofstream df(staging / "DEPS"); df << deps << "\n"; }

    if (fs::exists(dir / "INSTALL")) fs::copy_file(dir / "INSTALL", staging / "INSTALL", fs::copy_options::overwrite_existing);
    if (fs::exists(dir / "REMOVE"))  fs::copy_file(dir / "REMOVE",  staging / "REMOVE",  fs::copy_options::overwrite_existing);

    // Pack
    tui::log_step("Packing .wrp...");
    std::string output = pkgname + "-" + pkgver + "-" + arch + ".wrp";
    pack_warp(staging, output);

    std::string sha = sha256_of(output);
    { std::ofstream sf(output + ".sha256"); sf << sha << "  " << output << "\n"; }
    tui::log_step("Package ready: " + output, "ok");
    tui::log_step("SHA256: " + sha.substr(0, 16) + "...", "ok");

    fs::remove_all(workdir);

    tui::println("");
    tui::println("Size: " + std::to_string(fs::file_size(output) / 1024) + " KB");
    tui::println("File: " + output);
    tui::println("");

    if (install_after)
        ::install::from_warp(fs::path(output));
}

} // namespace build
