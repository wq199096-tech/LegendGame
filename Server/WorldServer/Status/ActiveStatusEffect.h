#pragma once

#include "Shared/Status/StatusEffectTypes.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 指令十二：ActiveStatusEffect —— 运行中的单个状态效果实例。
// instanceId 由 WorldServer 单调分配（指令十三：不能由 Client 提供）。
// ---------------------------------------------------------------------------
class ActiveStatusEffect {
public:
    ActiveStatusEffect() = default;
    ActiveStatusEffect(std::uint64_t instanceId, StatusEffectId effectId,
                       std::uint8_t sourceType, std::uint64_t sourceEntityId,
                       std::uint32_t sourceSkillId, std::uint8_t targetType,
                       std::uint64_t targetEntityId, std::uint8_t stacks,
                       std::chrono::steady_clock::time_point appliedTime,
                       std::chrono::steady_clock::time_point expireTime,
                       std::chrono::steady_clock::time_point nextTickTime)
        : m_instanceId(instanceId),
          m_effectId(effectId),
          m_sourceType(sourceType),
          m_sourceEntityId(sourceEntityId),
          m_sourceSkillId(sourceSkillId),
          m_targetType(targetType),
          m_targetEntityId(targetEntityId),
          m_stacks(stacks),
          m_appliedTime(appliedTime),
          m_expireTime(expireTime),
          m_nextTickTime(nextTickTime) {}

    std::uint64_t InstanceId() const { return m_instanceId; }
    StatusEffectId EffectId() const { return m_effectId; }
    std::uint8_t SourceType() const { return m_sourceType; }
    std::uint64_t SourceEntityId() const { return m_sourceEntityId; }
    std::uint32_t SourceSkillId() const { return m_sourceSkillId; }
    std::uint8_t TargetType() const { return m_targetType; }
    std::uint64_t TargetEntityId() const { return m_targetEntityId; }
    std::uint8_t Stacks() const { return m_stacks; }
    std::chrono::steady_clock::time_point AppliedTime() const { return m_appliedTime; }
    std::chrono::steady_clock::time_point ExpireTime() const { return m_expireTime; }
    std::chrono::steady_clock::time_point NextTickTime() const { return m_nextTickTime; }
    bool Active() const { return m_active; }

    // 指令七十二：后施加者覆盖 source。
    void SetSource(std::uint8_t sourceType, std::uint64_t sourceEntityId,
                   std::uint32_t sourceSkillId) {
        m_sourceType = sourceType;
        m_sourceEntityId = sourceEntityId;
        m_sourceSkillId = sourceSkillId;
    }
    void SetStacks(std::uint8_t stacks) { m_stacks = stacks; }
    void SetExpireTime(std::chrono::steady_clock::time_point t) { m_expireTime = t; }
    void SetNextTickTime(std::chrono::steady_clock::time_point t) { m_nextTickTime = t; }
    void SetActive(bool active) { m_active = active; }

private:
    std::uint64_t m_instanceId = 0;
    StatusEffectId m_effectId = 0;
    std::uint8_t m_sourceType = 0;
    std::uint64_t m_sourceEntityId = 0;
    std::uint32_t m_sourceSkillId = 0;
    std::uint8_t m_targetType = 0;
    std::uint64_t m_targetEntityId = 0;
    std::uint8_t m_stacks = 1;
    std::chrono::steady_clock::time_point m_appliedTime{};
    std::chrono::steady_clock::time_point m_expireTime{};
    std::chrono::steady_clock::time_point m_nextTickTime{};
    bool m_active = true;
};

} // namespace legend::world
