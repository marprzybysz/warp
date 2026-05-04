#pragma once
#include <filesystem>

namespace build {

void create_pkg(const std::filesystem::path& src_dir);
void build_from_source(const std::filesystem::path& dir, bool install_after);

} // namespace build
