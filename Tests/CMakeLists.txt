# ---------------------------------------------------------------------------
# LegendNetworkTests / LegendAccountTests：阶段9/10 验收（独立 Console，
# 不依赖 SDL——指令一百二十九；Account 测试独立成套——指令一百零四）
# ---------------------------------------------------------------------------
add_executable(LegendNetworkTests
    NetworkChecks.cpp
)
target_include_directories(LegendNetworkTests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendNetworkTests PRIVATE cxx_std_20)
target_link_libraries(LegendNetworkTests PRIVATE LegendGatewayCore LegendLoginCore LegendClientAccount)

if(MSVC)
    target_compile_options(LegendNetworkTests PRIVATE /utf-8)
    # sodium.dll 复制到 exe 旁（LegendLoginCore 运行时依赖）
    add_custom_command(TARGET LegendNetworkTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendNetworkTests>
            $<TARGET_FILE_DIR:LegendNetworkTests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendNetworkTests.exe"
    )
endif()

add_executable(LegendAccountTests
    AccountChecks.cpp
)
target_include_directories(LegendAccountTests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendAccountTests PRIVATE cxx_std_20)
target_link_libraries(LegendAccountTests PRIVATE LegendGatewayCore LegendLoginCore LegendClientAccount)

if(MSVC)
    target_compile_options(LegendAccountTests PRIVATE /utf-8)
    # sodium.dll 复制到 exe 旁（Argon2id/随机数依赖）
    add_custom_command(TARGET LegendAccountTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendAccountTests>
            $<TARGET_FILE_DIR:LegendAccountTests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendAccountTests.exe"
    )
endif()

add_executable(LegendWorldTests
    WorldChecks.cpp
    WorldAoiChecks.cpp
    WorldMonsterChecks.cpp
    WorldCombatChecks.cpp
    WorldSkillChecks.cpp
    WorldStatusChecks.cpp
)
target_include_directories(LegendWorldTests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(LegendWorldTests PRIVATE cxx_std_20)
target_link_libraries(LegendWorldTests PRIVATE LegendGatewayCore LegendLoginCore LegendWorldCore LegendClientAccount)

if(MSVC)
    target_compile_options(LegendWorldTests PRIVATE /utf-8)
    add_custom_command(TARGET LegendWorldTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendWorldTests>
            $<TARGET_FILE_DIR:LegendWorldTests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendWorldTests.exe"
    )
endif()

enable_testing()
add_test(NAME NetworkTests COMMAND LegendNetworkTests)
add_test(NAME AccountTests COMMAND LegendAccountTests)
add_test(NAME WorldTests COMMAND LegendWorldTests)
