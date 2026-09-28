#pragma once

#include "Shared/Combat/CombatTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段14：Combat 协议 payload（指令二/十~十四/四十九/五十）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十二），
// 复用阶段9 PacketCodec / kMaxPacketPayload。
// MessageId：PlayerAttackRequest=250 / PlayerAttackResponse=251 / CombatEvent=252 /
// EntityHealthSnapshot=253 / MonsterDeath=254 / PlayerDeath=256
//（255 已被 ErrorResponse 占用，指令十允许微调编号不冲突）。
// ---------------------------------------------------------------------------

// PlayerAttackRequest(250)（指令二）：只发目标类型/ID，禁止坐标/hitbox/damage（指令三）。
// 阶段14 只允许 targetEntityType=Monster（指令二/八十三）。
struct PlayerAttackRequestPayload {
    std::uint64_t requestId = 0;
    std::uint8_t targetEntityType = 0; // CombatEntityType
    std::uint64_t targetEntityId = 0;
};

// PlayerAttackResponse(251)（指令十一）：只回执"请求是否被接受"；
// 真正伤害结果走 CombatEvent。
struct PlayerAttackResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // CombatResultCode
    std::uint64_t targetEntityId = 0;
    std::string message;
};

// CombatEvent(252)（指令十二）：服务器生成的战斗事件广播（eventId 单调，指令十三）。
// 阶段15 指令三十二：增加 sourceType（BasicAttack=1/Skill=2）与 sourceId
//（普通攻击 0 / 技能 skillId）——技能伤害仍产生 CombatEvent，不绕开阶段14 协议。
struct CombatEventPayload {
    std::uint64_t eventId = 0;
    std::uint8_t attackerType = 0; // CombatEntityType
    std::uint64_t attackerId = 0;
    std::uint8_t targetType = 0; // CombatEntityType
    std::uint64_t targetId = 0;
    std::uint32_t damage = 0;
    std::uint32_t targetHpAfter = 0;
    std::uint32_t targetMaxHp = 0;
    bool killed = false;
    std::uint64_t serverTime = 0;
    std::uint8_t sourceType = 1;   // CombatSource（默认 BasicAttack 兼容阶段14）
    std::uint64_t sourceId = 0;    // 普通攻击 0 / 技能 skillId
};

// EntityHealthSnapshot(253)（指令十四）：1s 纠偏快照（单条，指令六十九）。
struct EntityHealthSnapshotPayload {
    std::uint8_t entityType = 0; // CombatEntityType
    std::uint64_t entityId = 0;
    std::uint32_t currentHp = 0;
    std::uint32_t maxHp = 0;
    bool alive = true;
    std::uint64_t serverTime = 0;
};

// MonsterDeath(254)（指令五十）。
struct MonsterDeathPayload {
    std::uint64_t entityId = 0;
    std::uint64_t killerCharacterId = 0;
    std::uint64_t serverTime = 0;
};

// PlayerDeath(256)（指令四十九）。
struct PlayerDeathPayload {
    std::uint64_t characterId = 0;
    std::uint8_t killerType = 0; // CombatEntityType
    std::uint64_t killerId = 0;
    std::uint64_t serverTime = 0;
};

bool EncodePlayerAttackRequest(const PlayerAttackRequestPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodePlayerAttackRequest(const std::uint8_t* data, std::size_t size,
                               PlayerAttackRequestPayload& out, std::string& error);
bool EncodePlayerAttackResponse(const PlayerAttackResponsePayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodePlayerAttackResponse(const std::uint8_t* data, std::size_t size,
                                PlayerAttackResponsePayload& out, std::string& error);
bool EncodeCombatEvent(const CombatEventPayload& p, std::vector<std::uint8_t>& out);
bool DecodeCombatEvent(const std::uint8_t* data, std::size_t size, CombatEventPayload& out,
                       std::string& error);
bool EncodeEntityHealthSnapshot(const EntityHealthSnapshotPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeEntityHealthSnapshot(const std::uint8_t* data, std::size_t size,
                                EntityHealthSnapshotPayload& out, std::string& error);
bool EncodeMonsterDeath(const MonsterDeathPayload& p, std::vector<std::uint8_t>& out);
bool DecodeMonsterDeath(const std::uint8_t* data, std::size_t size, MonsterDeathPayload& out,
                        std::string& error);
bool EncodePlayerDeath(const PlayerDeathPayload& p, std::vector<std::uint8_t>& out);
bool DecodePlayerDeath(const std::uint8_t* data, std::size_t size, PlayerDeathPayload& out,
                       std::string& error);

} // namespace legend::world
