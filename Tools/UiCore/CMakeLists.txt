# ---------------------------------------------------------------------------
# UiCore：Studio 与六服务器管理台共用的 UI 基建（Stage25.6）。
# legend_imgui：Dear ImGui 1.91.8 核心 + SDL3 / OpenGL3 后端（自 MapEditor 抽取）。
# legend_ui_core：EditorTheme 统一主题 + EditorStrings 中文字符串（零业务依赖）。
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${imgui_SOURCE_DIR})

add_library(legend_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(legend_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(legend_imgui
    PUBLIC
        SDL3::SDL3
)

if(WIN32)
    target_link_libraries(legend_imgui PRIVATE opengl32)
endif()

add_library(legend_ui_core STATIC
    Source/EditorTheme.cpp
)

# 头文件以仓库根相对路径引用（Tools/UiCore/Source/...），消费者同样获得该路径
target_include_directories(legend_ui_core PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(legend_ui_core PUBLIC cxx_std_20)
target_link_libraries(legend_ui_core PUBLIC legend_imgui)

if(MSVC)
    target_compile_options(legend_ui_core PRIVATE /utf-8)
endif()
