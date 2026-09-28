#pragma once

#include "Shared/Monster/MonsterProtocol.h"
#include "Shared/Monster/MonsterTypes.h"

#include <cstdint>
#include <string>

namespace legend::client {

// 阶段13 指令五十一：RemoteMonsterEntity —— 服务器 MonsterSpawn/Snapshot 驱动的远程怪物。
// 只存状态与插值；怪物完全由 WorldServer 权威控制（指令一）。
class RemoteMonsterEntity {
public:
    // 指令九十八：已存在则更新（Manager 保证不重复实体）。
    void ApplySpawn(const world::MonsterSpawnPayload& spawn);
    // 指令五十三/五十四/六十一/六十二：只更新 server target；未知/已删实体由 Manager 过滤。
    // 阶段14 指令九十一：快照携带 HP（alive=false 的 Dead 怪继续快照）。
    void ApplySnapshot(const world::MonsterSnapshotEntry& entry, std::uint64_t serverTime);
    // 指令五十三/五十四：插值 1-exp(-12*dt)；>300 直接 snap。
    void UpdateInterpolation(float deltaTime);
    // 阶段14 指令六十五/六十六/六十七：CombatEvent 更新 HP（不做客户端预测伤害）。
    // eventId <= lastCombatEventId 乱序旧包忽略（指令六十七）。
    void ApplyCombatEvent(std::uint64_t eventId, std::uint32_t hpAfter, bool killed);
    // 阶段14 指令七十二：MonsterDeath 事件置 dead（保留实体直到 Despawn）。
    void SetAliveLocal(bool alive) { m_alive = alive; }
    // 阶段14 指令六十八：HealthSnapshot 纠偏（权威覆盖，无 eventId）。
    void ApplySnapshotHealth(std::uint32_t currentHp, std::uint32_t maxHp) {
        m_currentHp = currentHp;
        m_maxHp = maxHp;
    }

    std::uint64_t EntityId() const { return m_entityId; }
    std::uint32_t MonsterTypeId() const { return m_monsterTypeId; }
    const std::string& Name() const { return m_name; }
    std::uint32_t Level() const { return m_level; }
    std::uint16_t MapId() const { return m_mapId; }
    std::uint8_t State() const { return m_state; }
    std::uint64_t TargetCharacterId() const { return m_targetCharacterId; }
    float ServerX() const { return m_serverX; }
    float ServerY() const { return m_serverY; }
    float RenderX() const { return m_renderX; }
    float RenderY() const { return m_renderY; }
    bool Active() const { return m_active; }
    // 阶段14 指令六十三：远程怪物 HP。
    std::uint32_t CurrentHp() const { return m_currentHp; }
    std::uint32_t MaxHp() const { return m_maxHp; }
    bool Alive() const { return m_alive; }

private:
    std::uint64_t m_entityId = 0;
    std::uint32_t m_monsterTypeId = 0;
    std::string m_name;
    std::uint32_t m_level = 1;
    std::uint16_t m_mapId = 1;
    std::uint8_t m_state = 0;
    std::uint64_t m_targetCharacterId = 0;
    float m_serverX = 0.0f;
    float m_serverY = 0.0f;
    float m_renderX = 0.0f;
    float m_renderY = 0.0f;
    bool m_active = false;
    // 阶段14 指令六十三：HP 状态。
    std::uint32_t m_currentHp = 0;
    std::uint32_t m_maxHp = 0;
    bool m_alive = true;
    // 阶段14 指令六十七：CombatEvent 乱序保护。
    std::uint64_t m_lastCombatEventId = 0;
};

} // namespace legend::client
