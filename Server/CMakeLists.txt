# ---------------------------------------------------------------------------
# LegendGateway / LegendLoginServer：纯 Console 服务器（阶段9 指令八十六）
# 不 link legend_engine（SDL/OpenGL）——Logger.cpp 直接编入（仅依赖 std）。
# 阶段10：拆分为 Core 静态库（Gateway/Login），供可执行文件与测试目标共用。
# ---------------------------------------------------------------------------
# Logger 单独成库（Gateway/Login/测试共用，避免多库重复编译）
add_library(LegendServerCommon STATIC
    ../Engine/Debug/Logger.cpp
    Common/ServerConfig.cpp
)
target_include_directories(LegendServerCommon PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendServerCommon PUBLIC cxx_std_20)
target_link_libraries(LegendServerCommon PUBLIC LegendSharedNetwork nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(LegendServerCommon PRIVATE /utf-8)
endif()

add_library(LegendInternalClients STATIC
    Common/PersistenceClient.cpp
    Common/LogClient.cpp
)
target_include_directories(LegendInternalClients PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendInternalClients PUBLIC cxx_std_20)
target_link_libraries(LegendInternalClients PUBLIC LegendNetworkTransport LegendServerCommon)
if(MSVC)
    target_compile_options(LegendInternalClients PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendAccountData：SQLite 数据层（Database/Schema/Repository/DbWorker/PasswordHasher）
# 阶段11：LoginServer 与 WorldServer 共用同一数据库抽象（WAL，指令二十二/二十五）。
# ---------------------------------------------------------------------------
add_library(LegendAccountData STATIC
    LoginServer/Account/AccountRepository.cpp
    LoginServer/Account/AccountService.cpp
    LoginServer/Account/PasswordHasher.cpp
    LoginServer/Account/DbWorker.cpp
    LoginServer/Account/CharacterService.cpp
    LoginServer/Account/SessionService.cpp
    LoginServer/Account/TicketStore.cpp
    LoginServer/Account/Database/Database.cpp
    LoginServer/Account/Database/DatabaseSchema.cpp
)
target_include_directories(LegendAccountData PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendAccountData PUBLIC cxx_std_20)
target_link_libraries(LegendAccountData
    PUBLIC
        LegendNetworkTransport
        LegendServerCommon
        sqlite3_core
        sodium
)
if(MSVC)
    target_compile_options(LegendAccountData PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendLoginCore：LoginServer + 账号/角色/Session 服务（阶段10 指令五十五）
# ---------------------------------------------------------------------------
add_library(LegendLoginCore STATIC
    LoginServer/LoginServer.cpp
)
target_include_directories(LegendLoginCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendLoginCore PUBLIC cxx_std_20)
target_link_libraries(LegendLoginCore
    PUBLIC
        LegendNetworkTransport
        LegendServerCommon
        LegendAccountData
        LegendInternalClients
)
if(MSVC)
    target_compile_options(LegendLoginCore PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# LegendGatewayCore：Gateway（握手/心跳/转发/Session 生命周期——阶段10 指令五十四）
# ---------------------------------------------------------------------------
add_library(LegendGatewayCore STATIC
    Gateway/GatewayServer.cpp
    Gateway/GatewaySession.cpp
)
target_include_directories(LegendGatewayCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendGatewayCore PUBLIC cxx_std_20)
target_link_libraries(LegendGatewayCore
    PUBLIC
        LegendNetworkTransport
        LegendServerCommon
)
if(MSVC)
    target_compile_options(LegendGatewayCore PRIVATE /utf-8)
endif()

# ---------------------------------------------------------------------------
# 可执行文件
# ---------------------------------------------------------------------------
add_executable(LegendGateway
    ../Apps/GatewayMain.cpp
)
target_link_libraries(LegendGateway PRIVATE LegendGatewayCore)

if(MSVC)
    target_compile_options(LegendGateway PRIVATE /utf-8)
endif()

add_executable(LegendLoginServer
    ../Apps/LoginServerMain.cpp
)
target_link_libraries(LegendLoginServer PRIVATE LegendLoginCore)

if(MSVC)
    target_compile_options(LegendLoginServer PRIVATE /utf-8)
    # sodium.dll 等运行时 DLL 复制到 exe 旁（阶段10 数据库链路依赖 libsodium）
    add_custom_command(TARGET LegendLoginServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendLoginServer>
            $<TARGET_FILE_DIR:LegendLoginServer>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendLoginServer.exe"
    )
endif()

# ---------------------------------------------------------------------------
# LegendWorldCore / LegendWorldServer：世界服务（阶段11 指令一/六十一）
# 纯 Console，不链接 SDL/OpenGL/Renderer/GameScene（指令一百零九）。
# ---------------------------------------------------------------------------
add_library(LegendWorldCore STATIC
    WorldServer/WorldServer.cpp
    WorldServer/WorldSession.cpp
    WorldServer/WorldManager.cpp
    WorldServer/WorldMapManager.cpp
    WorldServer/PlayerSession.cpp
    WorldServer/AOI/WorldSpatialGrid.cpp
    WorldServer/Monster/MonsterDefinition.cpp
    WorldServer/Monster/MonsterEntity.cpp
    WorldServer/Monster/MonsterManager.cpp
    WorldServer/Monster/MonsterSpatialGrid.cpp
    WorldServer/Monster/MonsterAi.cpp
    WorldServer/Combat/DamageCalculator.cpp
    WorldServer/Combat/CombatService.cpp
    WorldServer/Skill/SkillRegistry.cpp
    WorldServer/Skill/SkillService.cpp
    WorldServer/Status/StatusEffectRegistry.cpp
    WorldServer/Status/StatusEffectService.cpp
    WorldServer/Status/StatusEffectContainer.cpp
    WorldServer/Progression/ProgressionService.cpp
    WorldServer/Progression/RewardService.cpp
    WorldServer/Item/ItemRegistry.cpp
    WorldServer/Item/DropRoller.cpp
    WorldServer/Item/WorldItemDrop.cpp
    WorldServer/Item/InventoryContainer.cpp
    WorldServer/Item/InventoryRepository.cpp
    WorldServer/Monster/MonsterRespawnManager.cpp
    WorldServer/Monster/MonsterSpawnRegistry.cpp
    WorldServer/Monster/MonsterDefinitionRegistry.cpp
    WorldServer/Item/LootTableRegistry.cpp
    WorldServer/Quest/PlayerQuestContainer.cpp
    WorldServer/Quest/QuestRegistry.cpp
    WorldServer/Quest/QuestService.cpp
    WorldServer/Quest/QuestRepository.cpp
    WorldServer/Npc/NpcRegistry.cpp
    WorldServer/Npc/NpcManager.cpp
    WorldServer/Npc/NpcSpatialGrid.cpp
    WorldServer/Npc/NpcInteractionService.cpp
    WorldServer/Npc/ShopService.cpp
    WorldServer/Npc/TeleportService.cpp
    WorldServer/Map/MapRegistry.cpp
    WorldServer/Map/MapTransitionService.cpp
    WorldServer/Map/RespawnService.cpp
    WorldServer/Portal/PortalRegistry.cpp
    WorldServer/Portal/PortalManager.cpp
    WorldServer/Portal/PortalSpatialGrid.cpp
)
target_include_directories(LegendWorldCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendWorldCore PUBLIC cxx_std_20)
target_link_libraries(LegendWorldCore
    PUBLIC
        LegendNetworkTransport
        LegendServerCommon
        LegendAccountData
        LegendInternalClients
)
if(MSVC)
    target_compile_options(LegendWorldCore PRIVATE /utf-8)
endif()

add_executable(LegendWorldServer
    ../Apps/WorldServerMain.cpp
)
target_link_libraries(LegendWorldServer PRIVATE LegendWorldCore)

if(MSVC)
    target_compile_options(LegendWorldServer PRIVATE /utf-8)
    add_custom_command(TARGET LegendWorldServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:LegendWorldServer>
            $<TARGET_FILE_DIR:LegendWorldServer>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to LegendWorldServer.exe"
    )
endif()

# Stage25.5 formal internal services.
add_library(LegendDbCore STATIC
    Db/DbServer.cpp
)
target_include_directories(LegendDbCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendDbCore PUBLIC cxx_std_20)
target_link_libraries(LegendDbCore PUBLIC LegendAccountData LegendNetworkTransport LegendServerCommon)

add_executable(LegendDbServer ../Apps/DbServerMain.cpp)
target_link_libraries(LegendDbServer PRIVATE LegendDbCore)

add_library(LegendCharacterCore STATIC
    Character/CharacterServer.cpp
)
target_include_directories(LegendCharacterCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendCharacterCore PUBLIC cxx_std_20)
target_link_libraries(LegendCharacterCore PUBLIC LegendInternalClients LegendAccountData LegendNetworkTransport LegendServerCommon)

add_executable(LegendCharacterServer ../Apps/CharacterServerMain.cpp)
target_link_libraries(LegendCharacterServer PRIVATE LegendCharacterCore)

add_library(LegendLogCore STATIC
    Log/LogServer.cpp
)
target_include_directories(LegendLogCore PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_features(LegendLogCore PUBLIC cxx_std_20)
target_link_libraries(LegendLogCore PUBLIC LegendNetworkTransport LegendServerCommon nlohmann_json::nlohmann_json)

add_executable(LegendLogServer ../Apps/LogServerMain.cpp)
target_link_libraries(LegendLogServer PRIVATE LegendLogCore)

if(MSVC)
    foreach(target LegendDbCore LegendDbServer LegendCharacterCore LegendCharacterServer LegendLogCore LegendLogServer)
        target_compile_options(${target} PRIVATE /utf-8)
    endforeach()
    foreach(target LegendDbServer LegendCharacterServer)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}>
                $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()
