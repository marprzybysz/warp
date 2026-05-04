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

} // namespace repo
