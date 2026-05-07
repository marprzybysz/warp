#include "dep.h"
#include "repo.h"
#include "db.h"
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace dep {

namespace {

void dfs(const std::string& pkg,
         std::unordered_set<std::string>& visited,
         std::unordered_set<std::string>& in_stack,
         std::vector<std::string>& order) {
    if (in_stack.count(pkg))
        throw std::runtime_error("Circular dependency involving: " + pkg);
    if (visited.count(pkg))
        return;

    in_stack.insert(pkg);

    // For already-installed packages assume their deps are satisfied;
    // skip INDEX lookup to avoid failing on system libs not in any index.
    if (!db::exists(pkg)) {
        auto [deps_str, _] = repo::index_get_any(pkg, "deps");
        if (!deps_str.empty()) {
            std::istringstream ss(deps_str);
            std::string dep;
            while (std::getline(ss, dep, ',')) {
                dep.erase(0, dep.find_first_not_of(' '));
                dep.erase(dep.find_last_not_of(' ') + 1);
                if (!dep.empty())
                    dfs(dep, visited, in_stack, order);
            }
        }
    }

    in_stack.erase(pkg);
    visited.insert(pkg);
    order.push_back(pkg);
}

} // namespace

std::vector<std::string> resolve(const std::string& pkg) {
    std::unordered_set<std::string> visited, in_stack;
    std::vector<std::string> order;
    dfs(pkg, visited, in_stack, order);
    return order;
}

} // namespace dep
