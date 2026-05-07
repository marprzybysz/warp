#pragma once
#include <filesystem>

namespace install {

// resolve_deps=false when repo.cpp already resolved the full dep tree upfront.
void from_warp(const std::filesystem::path& file, bool resolve_deps = true);
void from_tarxz(const std::filesystem::path& file);

} // namespace install
