#pragma once
#include <filesystem>

namespace install {

void from_warp(const std::filesystem::path& file);
void from_tarxz(const std::filesystem::path& file);

} // namespace install
