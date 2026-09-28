#pragma once

#include "Client/WorldNetwork/RemoteStatusEffectContainer.h"
#include "Shared/World/WorldProtocol.h"

#include <cstdint>
#include <string>

namespace legend::client {

// 阶段12 指令三十二：RemotePlayerEntity —— 服务器 Spawn/Snapshot 驱动的远程玩家。
// 只存状态与插值，禁止持有网络/渲染逻辑（渲染复用 GameScene Debug 绘制，指令四十）。
class RemotePlayerEntity {
public:
    // 指令三十四：不存在则由 Manager 创建；已存在则更新元数据与位置。
    void ApplySpawn(const world::PlayerSpawnPayload& spawn);
    // 指令三十六：只更新 server target，不瞬移 render（teleport 除外）。
    void ApplySnapshot(float serverX, float serverY, std::uint64_t serverTime);
    // 指令三十七/三十八：render = lerp(render, server, 1 - exp(-12*dt))；差 >300 直接 snap。
    void UpdateInterpolation(float deltaTime);

    std::uint64_t CharacterId() const { return m_characterId; }
    const std::string& Name() const { return m_name; }
    std::uint16_t ClassId() const { return m_classId; }
    std::uint16_t Gender() const { return m_gender; }
    std::uint32_t Level() const { return m_level; }
    std::uint16_t MapId() const { return m_mapId; }
    float ServerX() const { return m_serverX; }
    float ServerY() const { return m_serverY; }
    float RenderX() const { return m_renderX; }
    float RenderY() const { return m_renderY; }
    // 指令四十一：相邻两次服务器位置差估算移动（Walk/Idle；不同步动画帧，指令四十二）。
    bool IsMoving() const { return m_moving; }
    bool Active() const { return m_active; }
    std::uint64_t LastSnapshotServerTime() const { return m_lastSnapshotServerTime; }
    // 阶段14 指令六十四：远程玩家 HP（CombatEvent/HealthSnapshot 更新，指令六十五）。
    std::uint32_t CurrentHp() const { return m_currentHp; }
    std::uint32_t MaxHp() const { return m_maxHp; }
    bool Alive() const { return m_alive; }
    void ApplyHealth(std::uint32_t currentHp, std::uint32_t maxHp, bool alive) {
        m_currentHp = currentHp;
        m_maxHp = maxHp;
        m_alive = alive;
    }

    // 阶段15 指令五十七：远程玩家技能表现状态（Started/Completed/Cancelled 驱动；
    // 仅表现——完成必须等服务器 Impact，不做本地预测，指令五十八）。
    bool Casting() const { return m_casting; }
    std::uint32_t CastingSkillId() const { return m_castingSkillId; }
    std::uint64_t CastStartServerTime() const { return m_castStartServerTime; }
    std::uint32_t CastDurationMs() const { return m_castDurationMs; }
    void ApplyCastState(bool casting, std::uint32_t skillId, std::uint64_t startServerTime,
                        std::uint32_t durationMs) {
        m_casting = casting;
        m_castingSkillId = skillId;
        m_castStartServerTime = startServerTime;
        m_castDurationMs = durationMs;
    }

    // 阶段16 指令六十二：远程玩家状态容器（仅展示）。
    RemoteStatusEffectContainer& StatusEffects() { return m_statusEffects; }
    const RemoteStatusEffectContainer& StatusEffects() const { return m_statusEffects; }

private:
    std::uint64_t m_characterId = 0;
    std::string m_name;
    std::uint16_t m_classId = 0;
    std::uint16_t m_gender = 0;
    std::uint32_t m_level = 1;
    std::uint16_t m_mapId = 1;
    float m_serverX = 0.0f;
    float m_serverY = 0.0f;
    float m_renderX = 0.0f;
    float m_renderY = 0.0f;
    std::uint64_t m_lastSnapshotServerTime = 0;
    bool m_moving = false;
    bool m_active = false;
    // 阶段14 指令六十四：HP 状态。
    std::uint32_t m_currentHp = 100;
    std::uint32_t m_maxHp = 100;
    bool m_alive = true;
    // 阶段15 指令五十七：技能表现状态。
    bool m_casting = false;
    std::uint32_t m_castingSkillId = 0;
    std::uint64_t m_castStartServerTime = 0;
    std::uint32_t m_castDurationMs = 0;
    // 阶段16 指令六十二：状态容器。
    RemoteStatusEffectContainer m_statusEffects;
};

} // namespace legend::client
