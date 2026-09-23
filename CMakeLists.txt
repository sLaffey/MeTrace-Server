cmake_minimum_required(VERSION 3.22.1)

project(
    MeTrace-Server
    VERSION 0.0.1
    DESCRIPTION "MeTrace backend server"
    LANGUAGES CXX
)

# ---------- 全局编译设置 ----------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，方便 clangd 等工具
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 统一产物输出目录：build/bin/server
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 单配置生成器（Makefile/Ninja）默认走 Release，避免默认空构建类型
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------- 第三方依赖 ----------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# 功能开关（这些 HTTPLIB_* 变量必须在 MakeAvailable 之前设置才会生效）
option(METRACE_ENABLE_HTTPS "启用 httplib 的 OpenSSL(HTTPS) 支持" OFF)

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE ${METRACE_ENABLE_HTTPS} CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE ON CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE) # 需要额外依赖，按需开启
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)   # 同上

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.57.1
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG        3.4.0
)

FetchContent_MakeAvailable(httplib nlohmann_json SQLiteCpp)

# httplib 在 Linux 上依赖 pthread
find_package(Threads REQUIRED)

# ---------- 目标 ----------
# 自动收集 src/ 下的所有源文件：以后新增 db/xxxRepository.cpp、service/xxxService.cpp
# 之类文件无需再改这里。CONFIGURE_DEPENDS 保证增删文件后重新构建会自动重新运行 cmake。
file(GLOB_RECURSE METRACE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(
    server
    ${METRACE_SOURCES}
)

# 让 #include "db/Database.h" 这类项目内相对路径可用（头文件统一放 include/）
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
    server
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        Threads::Threads
        SQLiteCpp
)

# 编译告警
if(MSVC)
    target_compile_options(server PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Release 下开启 LTO（编译器不支持时自动跳过）
include(CheckIPOSupported)
check_ipo_supported(RESULT METRACE_IPO_SUPPORTED OUTPUT METRACE_IPO_ERROR)
if(METRACE_IPO_SUPPORTED AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET server PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif(NOT METRACE_IPO_SUPPORTED)
    message(STATUS "IPO/LTO 不可用，已跳过: ${METRACE_IPO_ERROR}")
endif()

# ---------- 安装 / 打包 ----------
include(GNUInstallDirs)
install(TARGETS server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
include(CPack)