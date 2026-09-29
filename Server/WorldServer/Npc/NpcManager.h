#pragma once

#include "Server/WorldServer/Npc/NpcEntity.h"
#include "Shared/Npc/NpcTypes.h"

#include <cstdint>
#include <map>
#include <vector>

namespace legend::world {

class NpcSpatialGrid;
class NpcDefinition;

// ---------------------------------------------------------------------------
// 阶段20 指令十：NpcManager —— NPC runtime 容器（io 线程独占，无锁）。
// 启动时从 NpcRegistry 生成全部 NPC（Runtime EntityId 单调 uint64，不依赖固定值）。
// ---------------------------------------------------------------------------
class NpcManager {
public:
    // 从 Registry 生成全部 NPC（WorldServer::Start 调用一次；返回生成数量）。
    std::size_t SpawnFromRegistry();

    const NpcEntity* Find(std::uint64_t npcEntityId) const;
    std::size_t Count() const { return m_npcs.size(); }

    // 供 SpatialGrid 构建使用（NPC 静态不移动——Add 一次即可）。
    void AddToGrid(NpcSpatialGrid& grid) const;

private:
    std::map<std::uint64_t, NpcEntity> m_npcs;
    std::uint64_t m_nextEntityId = 1; // 指令十：Runtime EntityId 单调，不依赖固定值
};

} // namespace legend::world
