cmake_minimum_required(VERSION 3.24)

project(LegendGame VERSION 0.2.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# 全局编译设置
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一输出目录（多配置生成器会自动追加 Debug/Release 子目录）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Build/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Build/lib)

if(MSVC)
    add_compile_options(/MP /W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 第三方依赖（FetchContent 自动下载，无需手动安装）
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

# SDL3：窗口 / 事件 / OpenGL 上下文
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(SDL
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.2.8/SDL3-3.2.8.zip
    OVERRIDE_FIND_PACKAGE
)

# nlohmann/json：地图 JSON 序列化（header-only）
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
)

# Dear ImGui：仅用于 Map Editor 开发工具，不用于正式游戏 UI
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.8.zip
)

FetchContent_Declare(asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.zip
)

# 阶段10：SQLite amalgamation（账号/角色持久化；zip 无 CMakeLists，
# 用 FetchContent_Populate + 手工静态库，用户无需安装任何数据库软件）
FetchContent_Declare(sqlite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
)

# 阶段10：libsodium（Argon2id 密码哈希 + CSPRNG Session Token）
# 注意：libsodium 上游不提供 CMake 构建，使用官方 MSVC 预编译包
#（Debug/Release 动态库 + import lib + 头文件；windows-latest CI 兼容）
FetchContent_Declare(libsodium
    URL https://github.com/jedisct1/libsodium/releases/download/1.0.20-RELEASE/libsodium-1.0.20-msvc.zip
    URL_HASH SHA256=2ff97f9e3f5b341bdc808e698057bea1ae454f99e29ff6f9b62e14d0eb1b1baa
)

FetchContent_MakeAvailable(SDL nlohmann_json imgui asio)

# SQLite amalgamation：无 CMakeLists，手动编入静态库（线程安全默认 1）
FetchContent_GetProperties(sqlite3)
if(NOT sqlite3_POPULATED)
    FetchContent_Populate(sqlite3)
    add_library(sqlite3_core STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3_core PUBLIC ${sqlite3_SOURCE_DIR})
    if(MSVC)
        # amalgamation 体量大且来自第三方，关闭非致命告警
        target_compile_options(sqlite3_core PRIVATE /W0)
    endif()
endif()

FetchContent_GetProperties(libsodium)
if(NOT libsodium_POPULATED)
    FetchContent_Populate(libsodium)
endif()
if(NOT TARGET sodium)
    add_library(sodium SHARED IMPORTED GLOBAL)
    set_target_properties(sodium PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${libsodium_SOURCE_DIR}/include
        IMPORTED_CONFIGURATIONS "Debug;Release;MinSizeRel;RelWithDebInfo"
        IMPORTED_LOCATION_DEBUG ${libsodium_SOURCE_DIR}/x64/Debug/v143/dynamic/libsodium.dll
        IMPORTED_IMPLIB_DEBUG ${libsodium_SOURCE_DIR}/x64/Debug/v143/dynamic/libsodium.lib
        IMPORTED_LOCATION_RELEASE ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.dll
        IMPORTED_IMPLIB_RELEASE ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.lib
        IMPORTED_LOCATION_MINSIZEREL ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.dll
        IMPORTED_IMPLIB_MINSIZEREL ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.lib
        IMPORTED_LOCATION_RELWITHDEBINFO ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.dll
        IMPORTED_IMPLIB_RELWITHDEBINFO ${libsodium_SOURCE_DIR}/x64/Release/v143/dynamic/libsodium.lib
    )
endif()

# asio standalone：header-only 使用（不执行其 CMakeLists，避免安装/示例副作用）
add_library(asio_headers INTERFACE)
target_include_directories(asio_headers INTERFACE ${asio_SOURCE_DIR}/asio/include)
if(WIN32)
    target_compile_definitions(asio_headers INTERFACE
        ASIO_STANDALONE
        _WIN32_WINNT=0x0A00
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# 图片解码使用 ThirdParty/stb/stb_image.h（单头文件，公有领域）

# ---------------------------------------------------------------------------
# 项目模块
# ---------------------------------------------------------------------------
add_subdirectory(Shared)
add_subdirectory(Engine)
add_subdirectory(Engine/Network)
add_subdirectory(Server)
add_subdirectory(Client)
add_subdirectory(Tools/MapEditor)
# 阶段10 指令一百零五/一百零七：根目录 enable_testing —— ctest 从 Build 根
# 即可发现 NetworkTests + AccountTests（原先只在 Tests 子目录生效，根 ctest 找不到测试）
enable_testing()
add_subdirectory(Tests)
