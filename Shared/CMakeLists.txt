# ---------------------------------------------------------------------------
# LegendSharedNetwork：协议层静态库（Client/Gateway/LoginServer 共用）
# 依赖：仅 C++20 标准库（不依赖 SDL/OpenGL/Engine——Server 可安全链接）
# 注意：本文件位于 Shared/ 内，源路径相对 Shared/（与 Engine/CMakeLists 一致）
# ---------------------------------------------------------------------------
add_library(LegendSharedNetwork STATIC
    Network/ByteWriter.cpp
    Network/ByteReader.cpp
    Network/PacketCodec.cpp
    Network/NetworkErrors.cpp
    Network/Protocol.cpp
    Network/MessageId.cpp
    Account/AccountProtocol.cpp
    World/WorldProtocol.cpp
    Monster/MonsterProtocol.cpp
    Combat/CombatProtocol.cpp
    Skill/SkillProtocol.cpp
    Status/StatusEffectProtocol.cpp
    Progression/ProgressionProtocol.cpp
    Item/ItemProtocol.cpp
)

target_include_directories(LegendSharedNetwork PUBLIC ${PROJECT_SOURCE_DIR})

target_compile_features(LegendSharedNetwork PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(LegendSharedNetwork PRIVATE /utf-8)
endif()
