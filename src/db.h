#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace db {

void init(const std::filesystem::path& db_path);

bool exists(const std::string& name);

void save(const std::string& name,
          const std::string& version,
          const std::string& source,
          const std::vector<std::string>& files = {});

std::string get_info(const std::string& name);
std::vector<std::string> get_files(const std::string& name);
void remove(const std::string& name);

struct PkgEntry {
    std::string name;
    std::string version;
};
std::vector<PkgEntry> list_all();

std::string owner_of(const std::string& filepath);

extern std::filesystem::path db_root;

} // namespace db
