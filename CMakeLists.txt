cmake_minimum_required(VERSION 3.15)
project(warp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)

add_executable(warp
    src/main.cpp
    src/tui.cpp
    src/config.cpp
    src/db.cpp
    src/format.cpp
    src/install.cpp
    src/remove.cpp
    src/build.cpp
    src/repo.cpp
    src/diag.cpp
)

# uname() in main.cpp
target_link_libraries(warp PRIVATE ${LIBARCHIVE_LIBRARIES})

target_include_directories(warp PRIVATE src ${LIBARCHIVE_INCLUDE_DIRS})
target_compile_options(warp PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS warp RUNTIME DESTINATION bin)
install(FILES conf/warp.conf DESTINATION /etc/warp)
