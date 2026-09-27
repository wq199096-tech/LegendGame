# ---------------------------------------------------------------------------
# LegendClient：游戏客户端可执行程序
# ---------------------------------------------------------------------------
add_executable(LegendClient
    Source/Main.cpp
    Source/LegendApp.cpp
    Source/GameScene.cpp
    Network/GameNetworkClient.cpp
    Network/ClientNetworkController.cpp
    Source/ProgressionChecks.cpp
    Source/EquipmentChecks.cpp
    Source/SkillChecks.cpp
    Source/FallbackScene.cpp
    Character/PlayerCharacter.cpp
    Character/PlayerStatsComponent.cpp
    Combat/MonsterCombatController.cpp
    Combat/PlayerCombatController.cpp
    Character/PlayerController.cpp
    Loot/LootManager.cpp
    Loot/LootTable.cpp
    Progression/PlayerProgression.cpp
    Skill/PlayerSkillController.cpp
    Skill/SkillSystem.cpp
    World/AggroTable.cpp
    World/MonsterAIController.cpp
    World/MonsterCharacter.cpp
    World/MonsterDefinition.cpp
    World/MonsterSpawner.cpp
    World/NPCCharacter.cpp
    World/RewardSystem.cpp
    World/SpawnArea.cpp
    World/WorldActorManager.cpp
)

target_link_libraries(LegendClient PRIVATE legend_engine LegendNetworkTransport)

if(MSVC)
    target_compile_options(LegendClient PRIVATE /utf-8)
    # 临时：/MAP 生成链接清单用于崩溃地址符号定位
    target_link_options(LegendClient PRIVATE "/INCREMENTAL:NO" "/MAP:${PROJECT_SOURCE_DIR}/Build/LegendClient.map")
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
