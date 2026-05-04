#pragma once
#include <string>
#include <filesystem>

namespace repo {

void sync();
void install(const std::string& pkg);
void search(const std::string& query);
void list_updates();
void upgrade();

void list_repos();
void add_repo(const std::string& url);
void remove_repo(int n);
void gen_index(const std::filesystem::path& dir);

// Returns {value, repo_url} searching all configured repos
std::pair<std::string, std::string> index_get_any(const std::string& pkg, const std::string& field);

} // namespace repo
