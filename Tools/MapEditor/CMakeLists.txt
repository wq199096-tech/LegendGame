# ---------------------------------------------------------------------------
# LegendMapEditor：地图编辑器 Prototype（使用 Dear ImGui 开发工具 UI）
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

add_executable(LegendMapEditor
    Source/Main.cpp
    Source/EditorApp.cpp
    Source/EditorTheme.cpp
    Source/WorldDocument.cpp
    Source/GameDataDocument.cpp
)

target_link_libraries(LegendMapEditor PRIVATE legend_engine legend_imgui LegendSharedNetwork LegendVisualData)

if(MSVC)
    target_compile_options(LegendMapEditor PRIVATE /utf-8)
endif()

set_target_properties(LegendMapEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

if(WIN32)
    add_custom_command(TARGET LegendMapEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendMapEditor>
            $<TARGET_FILE_DIR:LegendMapEditor>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendMapEditor.exe"
    )
endif()
