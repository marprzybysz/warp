cmake_minimum_required(VERSION 3.15)
project(warp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE WARP_PATCH_NUM
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT WARP_PATCH_NUM)
    set(WARP_PATCH_NUM "0")
endif()
pkg_check_modules(LIBARCHIVE REQUIRED libarchive)
pkg_check_modules(LIBCURL REQUIRED libcurl)

add_executable(warp
    src/main.cpp
    src/tui.cpp
    src/config.cpp
    src/db.cpp
    src/format.cpp
    src/dep.cpp
    src/install.cpp
    src/remove.cpp
    src/build.cpp
    src/repo.cpp
    src/diag.cpp
    src/protect.cpp
)

target_link_libraries(warp PRIVATE ${LIBARCHIVE_LIBRARIES} ${LIBCURL_LIBRARIES})

target_include_directories(warp PRIVATE src ${LIBARCHIVE_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS})
target_compile_options(warp PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(warp PRIVATE WARP_PATCH_NUM="${WARP_PATCH_NUM}")

install(TARGETS warp RUNTIME DESTINATION bin)
install(FILES conf/warp.conf DESTINATION /etc/warp)
install(FILES conf/protected DESTINATION /etc/warp)
install(FILES man/warp.1 DESTINATION share/man/man1)
