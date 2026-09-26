cmake_minimum_required(VERSION 3.24)

project(LegendGame VERSION 0.1.0 LANGUAGES C CXX)

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

FetchContent_MakeAvailable(SDL)

# 图片解码使用 ThirdParty/stb/stb_image.h（单头文件，公有领域），不引入额外构建依赖

# ---------------------------------------------------------------------------
# 项目模块
# ---------------------------------------------------------------------------
add_subdirectory(Engine)
add_subdirectory(Client)
