#pragma once
#include <string>
#include <filesystem>

namespace config {

struct Config {
    std::string language    = "en";
    bool        color       = true;
    bool        confirm     = true;

    std::string repo        = "https://repo.flow.org/core";
    int         mirrors     = 3;
    int         timeout     = 30;

    bool        quiet       = false;
    bool        progress    = true;
    bool        summary     = true;

    std::string cache_dir   = "/var/cache/warp";
    int         keep_days   = 7;
};

extern Config cfg;

void load(const std::filesystem::path& path = "");

} // namespace config
