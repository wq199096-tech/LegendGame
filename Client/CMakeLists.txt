# ---------------------------------------------------------------------------
# LegendClientAccount：客户端账号/角色控制器 + 网络客户端（阶段10 指令五十七）
# 阶段11：加入 WorldNetworkClient/WorldClientController（指令四十五）。
# 独立静态库：LegendClient 与 LegendAccountTests/LegendWorldTests 共用（不依赖 SDL）。
# ---------------------------------------------------------------------------
add_library(LegendClientAccount STATIC
    Network/GameNetworkClient.cpp
    Account/AccountClientController.cpp
    Account/CharacterSelectionController.cpp
    WorldNetwork/WorldNetworkClient.cpp
    WorldNetwork/WorldClientController.cpp
    WorldNetwork/ClientQuestModel.cpp
    WorldNetwork/RemoteNpcManager.cpp
    WorldNetwork/RemotePortalManager.cpp
    WorldNetwork/RemotePlayerEntity.cpp
    WorldNetwork/RemotePlayerManager.cpp
    WorldNetwork/RemoteMonsterEntity.cpp
    WorldNetwork/RemoteMonsterManager.cpp
    WorldNetwork/RemoteStatusEffectContainer.cpp
)
target_include_directories(LegendClientAccount PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendClientAccount PUBLIC cxx_std_20)
target_link_libraries(LegendClientAccount
    PUBLIC
        LegendNetworkTransport
        LegendServerCommon
)
if(MSVC)
    target_compile_options(LegendClientAccount PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendVisualData【阶段24】：视觉资产数据层（Data/Assets 解析/校验 + 统一
# AnimationPlayer + 展示目录）。纯数据/逻辑层（不依赖 SDL/OpenGL），
# LegendClient / LegendMapEditor / LegendWorldTests 三方共用。
# ---------------------------------------------------------------------------
add_library(LegendVisualData STATIC
    Visuals/VisualAssetData.cpp
    Visuals/AnimationPlayer.cpp
    Visuals/VisualDataCatalog.cpp
)
target_include_directories(LegendVisualData PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendVisualData PUBLIC cxx_std_20)
target_link_libraries(LegendVisualData PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(LegendVisualData PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendClientUi【阶段25】：正式 UI 数据模型层（UiTheme/UiModels/物品展示目录）。
# 纯数据/逻辑层（不依赖 SDL/OpenGL）——LegendClient 与 LegendWorldTests（UiModel
# 检查，指令六十七）共用。
# ---------------------------------------------------------------------------
add_library(LegendClientUi STATIC
    Ui/UiTheme.cpp
    Ui/UiModels.cpp
    Ui/ItemDisplayCatalog.cpp
    Ui/ChapterDisplayCatalog.cpp
)
target_include_directories(LegendClientUi PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendClientUi PUBLIC cxx_std_20)
target_link_libraries(LegendClientUi PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(LegendClientUi PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendClient：游戏客户端可执行程序
# ---------------------------------------------------------------------------
add_executable(LegendClient
    Source/Main.cpp
    Source/LegendApp.cpp
    Source/GameScene.cpp
    Network/ClientNetworkController.cpp
    Assets/AssetManager.cpp
    Audio/AudioRuntime.cpp
    Visuals/Font.cpp
    Visuals/VisualRuntime.cpp
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

target_link_libraries(LegendClient PRIVATE legend_engine LegendNetworkTransport LegendClientAccount LegendVisualData LegendClientUi)

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
