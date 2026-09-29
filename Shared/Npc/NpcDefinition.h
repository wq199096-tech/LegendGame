#pragma once

#include "Shared/Npc/NpcTypes.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ShopEntry/ShopDefinition 在 Shared/Shop/ShopDefinition.h（指令三十五模块划分）。
// TeleportDefinition 在 Shared/Teleport/TeleportDefinition.h（指令五十八模块划分）。

// NPC 定义（指令七；阶段22 22.6 增加 enabled 数据驱动开关）。
struct NpcDefinition {
    NpcDefinitionId npcDefinitionId = 0;
    std::string name;
    NpcType npcType = NpcType::QuestGiver;
    std::uint16_t mapId = 1;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    float interactionRange = kNpcDefaultInteractionRange;
    std::uint32_t dialogueId = 0;   // 0 = 无对话
    std::uint32_t shopId = 0;       // 0 = 无商店
    std::uint32_t teleportId = 0;   // 0 = 无传送
    std::vector<QuestId> questIds;  // 该 NPC 提供的任务（start/turnIn）
    std::uint32_t visualId = 0;     // Debug 表现用
    bool enabled = true;            // 22.6：disabled 不生成实体
};

} // namespace legend::world
