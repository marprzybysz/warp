#pragma once
#include <string>
#include <filesystem>

namespace format {

enum class Type { Warp, TarXz, Unknown };

Type detect(const std::filesystem::path& file);

std::string name_from_file(const std::filesystem::path& file);
std::string version_from_file(const std::filesystem::path& file);

// Extract archive to a temporary directory; returns the tmp path.
// Caller is responsible for cleanup (fs::remove_all).
std::filesystem::path extract_to_tmp(const std::filesystem::path& file);

} // namespace format
