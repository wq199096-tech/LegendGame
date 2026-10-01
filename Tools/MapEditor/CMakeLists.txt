# ---------------------------------------------------------------------------
# LegendMapEditor：地图编辑器 Prototype（使用 Dear ImGui 开发工具 UI）
# legend_imgui / EditorTheme / EditorStrings 已抽取至 Tools/UiCore（Stage25.6，
# 与六服务器管理台共用同一套 UI 基建与主题）。
# ---------------------------------------------------------------------------
add_executable(LegendMapEditor
    Source/Main.cpp
    Source/EditorApp.cpp
    Source/WorldDocument.cpp
    Source/GameDataDocument.cpp
)

target_link_libraries(LegendMapEditor PRIVATE legend_engine legend_ui_core LegendSharedNetwork LegendVisualData)

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
