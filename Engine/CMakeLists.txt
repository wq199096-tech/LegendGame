# ---------------------------------------------------------------------------
# legend_engine：自研引擎静态库
# ---------------------------------------------------------------------------
set(LEGEND_ENGINE_SOURCES
    Core/Application.cpp
    Core/Engine.cpp
    Core/Timer.cpp
    Core/Window.cpp
    Debug/Logger.cpp
    Input/InputManager.cpp
    Math/Matrix4x4.cpp
    Render/Camera2D.cpp
    Render/GLApi.cpp
    Render/Renderer.cpp
    Render/Shader.cpp
    Render/Texture.cpp
    Resource/ResourceManager.cpp
    Scene/GameObject.cpp
    Scene/Scene.cpp
    Scene/SceneManager.cpp
)

add_library(legend_engine STATIC ${LEGEND_ENGINE_SOURCES})

# 我方源码含中文注释，MSVC 需要 /utf-8（第三方库源码不受影响）
if(MSVC)
    target_compile_options(legend_engine PRIVATE /utf-8)
endif()

# 以工程根目录为 include 基准（#include "Engine/..."、"Shared/..."）
target_include_directories(legend_engine PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(legend_engine
    PUBLIC
        SDL3::SDL3
)

if(WIN32)
    target_link_libraries(legend_engine PUBLIC opengl32)
endif()
