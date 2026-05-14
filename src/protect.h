#pragma once
#include <string>

namespace protect {

bool is_protected(const std::string& name);
void add(const std::string& name);
void remove(const std::string& name);
void list();

}
