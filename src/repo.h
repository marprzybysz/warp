#pragma once
#include <string>

namespace repo {

void sync();
void install(const std::string& pkg);
void search(const std::string& query);
void list_updates();
void upgrade();

} // namespace repo
