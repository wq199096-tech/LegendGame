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
    void ApplySnapshot(float serverX, float serverY, std::uint8_t state,
                       std::uint64_t targetCharacterId, std::uint64_t serverTime);
    // 指令五十三/五十四：插值 1-exp(-12*dt)；>300 直接 snap。
    void UpdateInterpolation(float deltaTime);

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
};

} // namespace legend::client
