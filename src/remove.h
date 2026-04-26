#pragma once
#include <string>

namespace remove_pkg {

void remove(const std::string& name, bool with_deps = false);

} // namespace remove_pkg
