#include "format.h"
#include "tui.h"
#include <archive.h>
#include <archive_entry.h>
#include <regex>
#include <cstring>

namespace format {

namespace fs = std::filesystem;

static bool archive_has_warpinfo(const fs::path& file) {
    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, file.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        return false;
    }

    bool found = false;
    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string name = archive_entry_pathname(entry);
        // Strip leading ./ if present
        if (name.rfind("./", 0) == 0) name = name.substr(2);
        if (name == "WARPINFO") { found = true; break; }
        archive_read_data_skip(a);
    }

    archive_read_close(a);
    archive_read_free(a);
    return found;
}

Type detect(const fs::path& file) {
    if (!fs::exists(file)) return Type::Unknown;

    std::string ext = file.extension().string();
    if (ext == ".wrp") return Type::Warp;

    // For .tar.xz / .tar.gz / .tar.bz2 — peek inside for WARPINFO
    std::string fname = file.filename().string();
    if (fname.find(".tar.") != std::string::npos || ext == ".tgz") {
        return archive_has_warpinfo(file) ? Type::Warp : Type::TarXz;
    }

    return Type::Unknown;
}

std::string name_from_file(const fs::path& file) {
    std::string base = file.stem().string();
    // Remove second extension if double (e.g. .tar.xz → stem is still "foo-1.0.tar")
    if (base.find(".tar") != std::string::npos)
        base = base.substr(0, base.find(".tar"));
    // Remove version suffix: -1.0, -1.0.0, -1.0-x86_64
    static const std::regex ver_re(R"(-\d[^-]*.*)");
    return std::regex_replace(base, ver_re, "");
}

std::string version_from_file(const fs::path& file) {
    std::string base = file.stem().string();
    if (base.find(".tar") != std::string::npos)
        base = base.substr(0, base.find(".tar"));
    static const std::regex ver_re(R"(-(\d[^-]*))");
    std::smatch m;
    if (std::regex_search(base, m, ver_re))
        return m[1].str();
    return "unknown";
}

fs::path extract_to_tmp(const fs::path& file) {
    fs::path tmpdir = fs::temp_directory_path() / ("warp." + std::to_string(getpid()));
    fs::create_directories(tmpdir);

    struct archive* a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    struct archive* disk = archive_write_disk_new();
    archive_write_disk_set_options(disk,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(disk);

    if (archive_read_open_filename(a, file.c_str(), 10240) != ARCHIVE_OK) {
        archive_read_free(a);
        archive_write_free(disk);
        fs::remove_all(tmpdir);
        tui::done_err("Cannot open archive: " + file.string());
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        std::string pathname = archive_entry_pathname(entry);
        // Strip leading ./
        if (pathname.rfind("./", 0) == 0) pathname = pathname.substr(2);
        fs::path dest = tmpdir / pathname;
        archive_entry_set_pathname(entry, dest.c_str());

        archive_write_header(disk, entry);

        const void* buf;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &buf, &size, &offset) == ARCHIVE_OK)
            archive_write_data_block(disk, buf, size, offset);

        archive_write_finish_entry(disk);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(disk);
    archive_write_free(disk);

    return tmpdir;
}

} // namespace format
