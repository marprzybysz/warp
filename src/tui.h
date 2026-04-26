#pragma once
#include <string>

namespace tui {

extern bool quiet;
extern bool use_color;
extern bool use_progress;

void log_step(const std::string& msg, const std::string& status = "");
void progress_bar(int percent, const std::string& action);
void clear_progress();
void done_ok();
[[noreturn]] void done_err(const std::string& msg);
void warn(const std::string& msg);
void println(const std::string& msg);

} // namespace tui
