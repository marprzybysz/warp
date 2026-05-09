#include "tui.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tui {

bool quiet      = false;
bool use_color  = true;
bool queue_mode = false;

static int _progress_pct = -1;

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

static int term_height() {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0)
        return static_cast<int>(w.ws_row);
    if (const char* rows = getenv("LINES"))
        return std::atoi(rows);
    return 24;
}

static std::string _progress_label;

static std::string draw_bar(int pct, const std::string& label) {
    int tw = term_width();

    // label padded to 20 chars, pct suffix " 100%"
    std::string lbl = label.empty() ? "Working" : label;
    if (static_cast<int>(lbl.size()) > 20) lbl = lbl.substr(0, 20);
    while (static_cast<int>(lbl.size()) < 20) lbl += ' ';

    std::string pct_str = " " + std::to_string(pct) + "%";
    // [label] [bar] pct_str
    int bar_width = tw - 20 - 4 - static_cast<int>(pct_str.size());
    if (bar_width < 8) bar_width = 8;

    int filled = pct * bar_width / 100;
    if (filled < 0) filled = 0;
    if (filled > bar_width) filled = bar_width;

    std::string filled_str, empty_str;
    for (int i = 0; i < filled; ++i)            filled_str += "\xe2\x96\x88"; // █
    for (int i = filled; i < bar_width; ++i)    empty_str  += "\xe2\x96\x91"; // ░

    // label cyan, fill green, rest default
    std::string out;
    if (use_color) {
        out += "\033[0;36m";   // cyan
        out += lbl;
        out += "\033[0m [";
        out += "\033[0;32m";   // green
        out += filled_str;
        out += "\033[0m";
        out += empty_str;
        out += "]";
        out += pct_str;
    } else {
        out += lbl + " [" + filled_str + empty_str + "]" + pct_str;
    }
    return out;
}

static void erase_bar() {
    if (_progress_pct < 0) return;
    int th = term_height();
    std::cout << "\033[s\033[" << th << ";1H\033[2K\033[u" << std::flush;
}

static void redraw_bar() {
    if (_progress_pct < 0) return;
    int th = term_height();
    std::cout << "\033[s\033[" << th << ";1H\033[2K"
              << draw_bar(_progress_pct, _progress_label) << "\033[u" << std::flush;
}

void progress_bar(int percent, const std::string& action) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    _progress_pct   = percent;
    if (!action.empty()) _progress_label = action;
    int th = term_height();
    std::cout << "\033[s\033[" << th << ";1H\033[2K"
              << draw_bar(percent, _progress_label) << "\033[u" << std::flush;
}

void clear_progress() {
    erase_bar();
    _progress_pct = -1;
}

void log_step(const std::string& msg, const std::string& status) {
    if (quiet) return;
    erase_bar();

    if (status == "warn") {
        std::cout << "  " << c("\033[0;33m") << "⚠ " << msg << c("\033[0m") << "\n";
    } else {
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
    redraw_bar();
}

void queue_msg(const std::string& msg) {
    erase_bar();
    std::cout << msg << "\n";
    redraw_bar();
}

void done_ok() {
    if (queue_mode) return;
    clear_progress();
    std::cout << c("\033[0;32m") << c("\033[1m") << "Done!" << c("\033[0m") << "\n";
}

[[noreturn]] void done_err(const std::string& msg) {
    clear_progress();
    std::cerr << c("\033[0;31m") << "ERROR: " << msg << c("\033[0m") << "\n";
    std::exit(1);
}

void warn(const std::string& msg) {
    if (quiet) return;
    erase_bar();
    std::cout << c("\033[0;33m") << "⚠ " << msg << c("\033[0m") << "\n";
    redraw_bar();
}

void println(const std::string& msg) {
    if (quiet) return;
    erase_bar();
    std::cout << msg << "\n";
    redraw_bar();
}

std::string format_size_bytes(uintmax_t bytes) {
    long long kb = static_cast<long long>(bytes) / 1024;
    if (kb < 1024) {
        return std::to_string(kb) + " kB";
    } else if (kb < 1048576) {
        long long h = kb * 100 / 1024;
        std::ostringstream ss;
        ss << (h / 100) << "." << std::setfill('0') << std::setw(2) << (h % 100) << " MB";
        return ss.str();
    } else {
        long long h = kb * 100 / 1048576;
        std::ostringstream ss;
        ss << (h / 100) << "." << std::setfill('0') << std::setw(2) << (h % 100) << " GB";
        return ss.str();
    }
}

std::string format_size(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return "?";
    long long kb = st.st_size / 1024;
    if (kb < 1024) {
        return std::to_string(kb) + " kB";
    } else if (kb < 1048576) {
        long long h = kb * 100 / 1024;
        std::ostringstream ss;
        ss << (h / 100) << "." << std::setfill('0') << std::setw(2) << (h % 100) << " MB";
        return ss.str();
    } else {
        long long h = kb * 100 / 1048576;
        std::ostringstream ss;
        ss << (h / 100) << "." << std::setfill('0') << std::setw(2) << (h % 100) << " GB";
        return ss.str();
    }
}

} // namespace tui
