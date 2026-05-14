#pragma once
#include <string>

namespace diag {

void check();
void fix();
void orphans();
void show_log();
void rollback(const std::string& name);
void remove_cache(const std::string& name);
void verify(const std::string& path);
void push(const std::string& path);
void scan_system();
void scan_system_quiet();
void autoremove();
void clean_cache();

} // namespace diag
