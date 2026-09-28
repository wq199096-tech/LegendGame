#pragma once

#include "Shared/Skill/SkillTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15 技能协议 payload（指令二十二~三十/三十一/六十七）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十六），
// 复用阶段9 PacketCodec / kMaxPacketPayload。
// MessageId：SkillCastRequest=260 / SkillCastResponse=261 / SkillCastStarted=262 /
// SkillCastCompleted=263 / SkillCastCancelled=264 / SkillImpactEvent=265 /
// ManaSnapshot=266（指令二十二）。
// ---------------------------------------------------------------------------

// SkillCastRequest(260)（指令二十三）：只发 requestId/skillId/targetType/
// targetEntityId；禁止传伤害/Mana/CD/CastTime/AOE 位置/命中结果（指令一/九十六/
// 九十七）。
struct SkillCastRequestPayload {
    std::uint64_t requestId = 0;
    std::uint32_t skillId = 0;
    std::uint8_t targetType = 0; // SkillTargetType
    std::uint64_t targetEntityId = 0;
};

// SkillCastResponse(261)（指令二十四）：请求回执；真正结果走 Started/Completed/
// Impact/CombatEvent。
struct SkillCastResponsePayload {
    std::uint64_t requestId = 0;
    std::uint32_t skillId = 0;
    bool accepted = false;
    std::uint8_t resultCode = 0; // SkillResultCode
    std::uint32_t currentMana = 0;
    std::string message;
};

// SkillCastStarted(262)（指令二十五）：Instant 技能也必须发送（指令三十九：
// 统一协议，紧接 Completed/Impact/CombatEvent）。
struct SkillCastStartedPayload {
    std::uint64_t castId = 0;
    std::uint64_t casterCharacterId = 0;
    std::uint32_t skillId = 0;
    std::uint8_t targetType = 0;    // SkillTargetType
    std::uint64_t targetEntityId = 0;
    std::uint32_t castTimeMs = 0;   // Instant = 0
    std::uint64_t serverTime = 0;
};

// SkillCastCompleted(263)（指令二十七）。
struct SkillCastCompletedPayload {
    std::uint64_t castId = 0;
    std::uint64_t casterCharacterId = 0;
    std::uint32_t skillId = 0;
    std::uint8_t targetType = 0;    // SkillTargetType
    std::uint64_t targetEntityId = 0;
    std::uint64_t serverTime = 0;
};

// SkillCastCancelled(264)（指令二十八）：reason = SkillCancelReason。
struct SkillCastCancelledPayload {
    std::uint64_t castId = 0;
    std::uint64_t casterCharacterId = 0;
    std::uint32_t skillId = 0;
    std::uint8_t reason = 0; // SkillCancelReason
    std::uint64_t serverTime = 0;
};

// SkillImpactEvent(265)（指令二十九）：把同一次技能命中的多个目标关联起来；
// targets 数量 <= kSkillImpactMaxTargets（指令三十：Decode 超过拒绝）。
struct SkillImpactEventPayload {
    std::uint64_t castId = 0;
    std::uint32_t skillId = 0;
    std::uint64_t casterCharacterId = 0;
    std::uint64_t serverTime = 0;
    std::vector<SkillImpactTarget> targets;
};

// ManaSnapshot(266)（指令六十七）：每 1s 发给玩家本人（指令六十八：无 Regen）。
struct ManaSnapshotPayload {
    std::uint32_t currentMana = 0;
    std::uint32_t maxMana = 0;
    std::uint64_t serverTime = 0;
};

bool EncodeSkillCastRequest(const SkillCastRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillCastRequest(const std::uint8_t* data, std::size_t size,
                            SkillCastRequestPayload& out, std::string& error);
bool EncodeSkillCastResponse(const SkillCastResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillCastResponse(const std::uint8_t* data, std::size_t size,
                             SkillCastResponsePayload& out, std::string& error);
bool EncodeSkillCastStarted(const SkillCastStartedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillCastStarted(const std::uint8_t* data, std::size_t size,
                            SkillCastStartedPayload& out, std::string& error);
bool EncodeSkillCastCompleted(const SkillCastCompletedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillCastCompleted(const std::uint8_t* data, std::size_t size,
                              SkillCastCompletedPayload& out, std::string& error);
bool EncodeSkillCastCancelled(const SkillCastCancelledPayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillCastCancelled(const std::uint8_t* data, std::size_t size,
                              SkillCastCancelledPayload& out, std::string& error);
bool EncodeSkillImpactEvent(const SkillImpactEventPayload& p, std::vector<std::uint8_t>& out);
bool DecodeSkillImpactEvent(const std::uint8_t* data, std::size_t size,
                            SkillImpactEventPayload& out, std::string& error);
bool EncodeManaSnapshot(const ManaSnapshotPayload& p, std::vector<std::uint8_t>& out);
bool DecodeManaSnapshot(const std::uint8_t* data, std::size_t size, ManaSnapshotPayload& out,
                        std::string& error);

} // namespace legend::world
