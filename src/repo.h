#pragma once
#include <string>

namespace repo {

void sync();
void install(const std::string& pkg);
void search(const std::string& query);

} // namespace repo
