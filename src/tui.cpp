#include "tui.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <sys/ioctl.h>
#include <unistd.h>

namespace tui {

bool quiet      = false;
bool use_color  = true;
bool use_progress = true;

static const char* c(const char* code) {
    return use_color ? code : "";
}

static int term_width() {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return static_cast<int>(w.ws_col);
    if (const char* cols = getenv("COLUMNS"))
        return std::atoi(cols);
    return 80;
}

void log_step(const std::string& msg, const std::string& status) {
    if (quiet) return;

    if (status == "warn") {
        std::cout << "  " << c("\033[0;33m") << "⚠ " << msg << c("\033[0m") << "\n";
        return;
    }

    std::string line = msg;
    while (static_cast<int>(line.size()) < 45)
        line += ' ';

    if (status == "ok")
        std::cout << line << " " << c("\033[0;32m") << "✓" << c("\033[0m") << "\n";
    else if (status == "err")
        std::cout << line << " " << c("\033[0;31m") << "✗" << c("\033[0m") << "\n";
    else
        std::cout << msg << "\n";
}

void progress_bar(int percent, const std::string& /*action*/) {
    if (quiet || !use_progress) return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int tw = term_width();

    std::string prefix = "Progress: " + std::to_string(percent) + "% ";
    int bar_width = tw - static_cast<int>(prefix.size()) - 3;
    if (bar_width < 10) bar_width = 10;

    int filled = percent * bar_width / 100;
    std::string bar(filled, '#');
    std::string empty(bar_width - filled, ' ');

    std::cout << "\r" << prefix << "[" << bar << empty << "]" << std::flush;
}

void clear_progress() {
    if (quiet) return;
    int tw = term_width();
    std::cout << "\r" << std::string(tw, ' ') << "\r" << std::flush;
}

void done_ok() {
    std::cout << c("\033[0;32m") << c("\033[1m") << "Gotowe" << c("\033[0m") << "\n";
}

[[noreturn]] void done_err(const std::string& msg) {
    std::cerr << c("\033[0;31m") << "ERROR: " << msg << c("\033[0m") << "\n";
    std::exit(1);
}

void warn(const std::string& msg) {
    if (quiet) return;
    std::cout << c("\033[0;33m") << "⚠ " << msg << c("\033[0m") << "\n";
}

void println(const std::string& msg) {
    if (quiet) return;
    std::cout << msg << "\n";
}

} // namespace tui
