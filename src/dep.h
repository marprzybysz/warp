#pragma once
#include <string>
#include <vector>

namespace dep {

// Resolves the full dependency tree for pkg using the repo INDEX.
// Returns packages in topological order (dependencies first, pkg last).
// Already-installed packages are included so the caller can decide on upgrades.
// Throws std::runtime_error on circular dependency.
std::vector<std::string> resolve(const std::string& pkg);

} // namespace dep
