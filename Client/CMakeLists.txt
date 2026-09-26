# ---------------------------------------------------------------------------
# LegendClient：游戏客户端可执行程序
# ---------------------------------------------------------------------------
add_executable(LegendClient
    Source/Main.cpp
    Source/LegendApp.cpp
    Source/TestScene.cpp
)

target_link_libraries(LegendClient PRIVATE legend_engine)

if(MSVC)
    target_compile_options(LegendClient PRIVATE /utf-8)
endif()

# 在 Visual Studio 中按 F5 调试时，以工程根目录为工作目录（Assets 可被找到）
set_target_properties(LegendClient PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
)

# 将 SDL3.dll / SDL3_image.dll 等运行时 DLL 复制到 exe 旁边
if(WIN32)
    add_custom_command(TARGET LegendClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendClient>
            $<TARGET_FILE_DIR:LegendClient>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendClient.exe"
    )
endif()
