#pragma once

#include <string>

#include "Engine/Entity/Direction8.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Skill/SkillDefinition.h"

namespace legend::skill {

// 运行中施法上下文（阶段8指令二十七）：
// - SingleTarget 只保存 targetId（EntityId），事件触发时重新 Registry Resolve，
//   绝不跨帧保存 MonsterCharacter*（指令二十八：防悬空指针）
// - attackSnapshot：施法开始时锁定 Final Attack（指令七十五/七十六），
//   施法中换装/升级不改变已开始技能的伤害
struct SkillCastContext {
    std::string skillId;
    legend::entity::EntityId casterId = legend::entity::kInvalidEntityId;
    legend::entity::EntityId targetId = legend::entity::kInvalidEntityId; // SelfArea 无效值
    legend::entity::Direction8 lockedDirection = legend::entity::Direction8::South;
    float attackSnapshot = 0.0f;
    bool started = false;       // 施法进行中
    bool eventConsumed = false; // skill_hit exactly-once（指令三十二）
    SkillDefinition definition; // 定义快照（animationEvent 比较用，不查库）

    bool IsActive() const { return started; }
    void Clear() { *this = SkillCastContext{}; }
};

} // namespace legend::skill
