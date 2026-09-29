#pragma once

#include "Shared/Npc/NpcTypes.h"

#include <cstdint>
#include <string>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段20 指令十五：RemoteNpcEntity —— 客户端 NPC 镜像（只显示服务器 Spawn 的 NPC）。
// ---------------------------------------------------------------------------
struct RemoteNpcEntity {
    std::uint64_t npcEntityId = 0;
    std::uint32_t npcDefinitionId = 0;
    std::string name;
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    world::NpcType type = world::NpcType::QuestGiver;
    std::uint32_t visualId = 0;
    world::NpcQuestMarker questMarker = world::NpcQuestMarker::None; // per-player
    // 阶段20 指令七十三：服务器断开/失效时客户端本地关闭 UI 的标记。
    bool alive = true;
};

} // namespace legend::client
