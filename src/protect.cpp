#include "protect.h"
#include "tui.h"
#include <fstream>
#include <string>
#include <unordered_set>
#include <filesystem>
#include <iostream>

namespace protect {

namespace fs = std::filesystem;

static const fs::path PROTECT_FILE = "/etc/warp/protected";

static std::unordered_set<std::string> load() {
    std::unordered_set<std::string> set;
    std::ifstream f(PROTECT_FILE);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.front() != '#')
            set.insert(line);
    }
    return set;
}

bool is_protected(const std::string& name) {
    return load().count(name) > 0;
}

void add(const std::string& name) {
    if (name.empty()) tui::done_err("Provide a package name");
    auto set = load();
    if (set.count(name)) {
        tui::println(name + " is already protected");
        return;
    }
    fs::create_directories(PROTECT_FILE.parent_path());
    std::ofstream f(PROTECT_FILE, std::ios::app);
    f << name << "\n";
    tui::println("Protected: " + name);
}

void remove(const std::string& name) {
    if (name.empty()) tui::done_err("Provide a package name");
    auto set = load();
    if (!set.count(name)) {
        tui::println(name + " is not in the protected list");
        return;
    }
    set.erase(name);
    fs::create_directories(PROTECT_FILE.parent_path());
    std::ofstream f(PROTECT_FILE, std::ios::trunc);
    for (const auto& p : set)
        f << p << "\n";
    tui::println("Unprotected: " + name);
}

void list() {
    auto set = load();
    if (set.empty()) { tui::println("No protected packages"); return; }
    tui::println("Protected packages:");
    for (const auto& p : set)
        tui::println("  " + p);
}

}
