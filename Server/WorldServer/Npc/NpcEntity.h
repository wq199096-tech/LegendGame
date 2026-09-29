#pragma once

#include "Shared/Npc/NpcDefinition.h"
#include "Shared/Npc/NpcTypes.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令八：NpcEntity —— NPC runtime 实体。
// 阶段20 NPC：不移动 / 不死亡 / 不参与 Combat；DefinitionId 与 Runtime EntityId
// 严格区分（指令五）。附带 Dialogue/Shop 会话结构（存于 PlayerSession，指令二十一/四十一）。
// ---------------------------------------------------------------------------
class NpcEntity {
public:
    NpcEntity() = default;
    NpcEntity(std::uint64_t entityId, const NpcDefinition* definition)
        : m_entityId(entityId), m_definition(definition) {}

    std::uint64_t EntityId() const { return m_entityId; }
    NpcDefinitionId DefinitionId() const { return m_definition ? m_definition->npcDefinitionId : 0; }
    const NpcDefinition* Definition() const { return m_definition; }
    std::uint16_t MapId() const { return m_definition ? m_definition->mapId : 1; }
    float X() const { return m_definition ? m_definition->spawnX : 0.0f; }
    float Y() const { return m_definition ? m_definition->spawnY : 0.0f; }
    bool Active() const { return m_active && m_definition != nullptr; }
    void SetActive(bool active) { m_active = active; }

private:
    std::uint64_t m_entityId = 0;
    const NpcDefinition* m_definition = nullptr; // Registry 只读定义（构造后稳定）
    bool m_active = true;
};

// 阶段20 指令二十一：Dialogue Session（存 PlayerSession；30s TTL，指令二十二）。
struct ActiveDialogueSession {
    std::uint64_t sessionId = 0;
    std::uint64_t npcEntityId = 0;
    std::chrono::steady_clock::time_point openedAt{};
};

// 阶段20 指令四十一：Shop Session（必须经有效 Dialogue Session 打开；30s TTL）。
struct ActiveShopSession {
    std::uint64_t sessionId = 0;
    std::uint64_t npcEntityId = 0;
    std::uint32_t shopId = 0;
    std::chrono::steady_clock::time_point openedAt{};
};

} // namespace legend::world
