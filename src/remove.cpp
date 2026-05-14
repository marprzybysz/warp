#include "remove.h"
#include "protect.h"
#include "tui.h"
#include "db.h"
#include <filesystem>

namespace remove_pkg {

namespace fs = std::filesystem;

void remove(const std::string& name, bool /*with_deps*/) {
    if (!db::exists(name))
        tui::done_err("Package '" + name + "' is not installed");

    if (protect::is_protected(name))
        tui::done_err("'" + name + "' is a protected package — use 'warp unprotect " + name + "' to override");

    auto files = db::get_files(name);

    if (!files.empty()) {
        int total = static_cast<int>(files.size());
        int count = 0;
        for (const auto& f : files) {
            if (fs::is_regular_file(f))
                fs::remove(f);
            ++count;
            tui::progress_bar(count * 90 / total);
        }
        for (const auto& f : files) {
            std::error_code ec;
            fs::remove(fs::path(f).parent_path(), ec);
        }
    }

    tui::progress_bar(95);
    db::remove(name);
    tui::progress_bar(100);
    db::log("remove", name);
    tui::done_ok();
}

} // namespace remove_pkg
