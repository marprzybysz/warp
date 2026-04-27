#pragma once
#include <string>

namespace repo {

void sync();
void install(const std::string& pkg);
void search(const std::string& query);
void list_updates();
void upgrade();

void list_repos();
void add_repo(const std::string& url);
void remove_repo(int n);

} // namespace repo
