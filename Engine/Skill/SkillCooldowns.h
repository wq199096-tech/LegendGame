#pragma once

#include <string>
#include <unordered_map>

namespace legend::skill {

// 技能冷却运行时状态：skillId -> 剩余秒数（阶段8指令十八）。
// SkillDefinition 是静态数据，绝不往里存 CD。每帧 Update 递减并 clamp 0（无负值）。
class SkillCooldowns {
public:
    void StartCooldown(const std::string& skillId, float duration);
    // 每帧：所有 remaining -= deltaTime，最低 0
    void Update(float deltaTime);
    bool IsReady(const std::string& skillId) const; // 无记录视为 Ready
    float GetRemaining(const std::string& skillId) const; // 无记录返回 0
    void Reset(const std::string& skillId);
    void ResetAll();

private:
    std::unordered_map<std::string, float> m_remaining;
};

} // namespace legend::skill
