#include "remove.h"
#include "tui.h"
#include "db.h"
#include <filesystem>

namespace remove_pkg {

namespace fs = std::filesystem;

void remove(const std::string& name, bool /*with_deps*/) {
    if (!db::exists(name))
        tui::done_err("Package '" + name + "' is not installed");

    tui::log_step("Reading file list for " + name + "...");
    auto files = db::get_files(name);

    if (!files.empty()) {
        int total = static_cast<int>(files.size());
        int count = 0;
        for (const auto& f : files) {
            if (fs::is_regular_file(f))
                fs::remove(f);
            ++count;
            tui::progress_bar(count * 100 / total, "Removing " + name);
        }
        tui::clear_progress();

        // Clean up empty parent directories
        for (const auto& f : files) {
            fs::path parent = fs::path(f).parent_path();
            std::error_code ec;
            fs::remove(parent, ec);  // silently fails if not empty
        }
    }

    tui::log_step("Removing files...", "ok");
    db::remove(name);
    tui::log_step("Package record removed...", "ok");
}

} // namespace remove_pkg
