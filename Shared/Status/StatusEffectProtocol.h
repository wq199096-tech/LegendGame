#pragma once

#include "Shared/Status/StatusEffectTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 指令五十一~五十六：状态效果协议 payload。
// MessageId：StatusEffectApplied=270 / StatusEffectUpdated=271 /
// StatusEffectRemoved=272 / StatusEffectSnapshot=273。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十二）；
// Snapshot 数量上限 32，count 超过拒绝（指令五十六/一百二十七）。
// targetType 取 CombatEntityType（Player=1 / Monster=2）。
// ---------------------------------------------------------------------------

// StatusEffectApplied(270)（指令五十二）。
struct StatusEffectAppliedPayload {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t targetType = 0;        // CombatEntityType
    std::uint64_t targetEntityId = 0;
    std::uint8_t sourceType = 0;        // CombatEntityType
    std::uint64_t sourceEntityId = 0;
    std::uint32_t sourceSkillId = 0;
    std::uint8_t stacks = 1;
    std::uint32_t durationMs = 0;
    std::uint32_t remainingMs = 0;
    std::uint64_t serverTime = 0;
};

// StatusEffectUpdated(271)（指令五十三）：Refresh / stack 变化。
struct StatusEffectUpdatedPayload {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t targetType = 0;
    std::uint64_t targetEntityId = 0;
    std::uint8_t stacks = 1;
    std::uint32_t remainingMs = 0;
    std::uint64_t serverTime = 0;
};

// StatusEffectRemoved(272)（指令五十四）：reason = StatusRemovedReason。
struct StatusEffectRemovedPayload {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t targetType = 0;
    std::uint64_t targetEntityId = 0;
    std::uint8_t reason = 0;
    std::uint64_t serverTime = 0;
};

// StatusEffectSnapshot(273)（指令五十五）：每 2s 纠偏（指令五十九）。
struct StatusEffectSnapshotEntry {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t stacks = 1;
    std::uint32_t remainingMs = 0;
};

struct StatusEffectSnapshotPayload {
    std::uint8_t targetType = 0;
    std::uint64_t targetEntityId = 0;
    std::uint64_t serverTime = 0;
    std::vector<StatusEffectSnapshotEntry> effects; // count <= 32
};

bool EncodeStatusEffectApplied(const StatusEffectAppliedPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeStatusEffectApplied(const std::uint8_t* data, std::size_t size,
                               StatusEffectAppliedPayload& out, std::string& error);
bool EncodeStatusEffectUpdated(const StatusEffectUpdatedPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeStatusEffectUpdated(const std::uint8_t* data, std::size_t size,
                               StatusEffectUpdatedPayload& out, std::string& error);
bool EncodeStatusEffectRemoved(const StatusEffectRemovedPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeStatusEffectRemoved(const std::uint8_t* data, std::size_t size,
                               StatusEffectRemovedPayload& out, std::string& error);
bool EncodeStatusEffectSnapshot(const StatusEffectSnapshotPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeStatusEffectSnapshot(const std::uint8_t* data, std::size_t size,
                                StatusEffectSnapshotPayload& out, std::string& error);

} // namespace legend::world
