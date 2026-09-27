#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/Skill/SkillDefinition.h"

namespace legend::skill {

// 技能定义库：程序启动加载 skills.json 一次（与 ItemDatabase 同模式：
// 不允许每次施法重新解析 JSON）。非法 Definition 跳过 + LOG，不崩溃。
// 加载失败必须让 World 初始化失败（阶段8指令九十九）。
class SkillDatabase {
public:
    bool LoadFromFile(const std::string& filePath);

    const SkillDefinition* Get(const std::string& id) const; // 不存在返回 nullptr
    bool Exists(const std::string& id) const { return m_skills.count(id) > 0; }
    const std::unordered_map<std::string, SkillDefinition>& GetAll() const { return m_skills; }
    std::size_t Count() const { return m_skills.size(); }

    // 测试专用：程序化注入定义（仅供自动测试/SkillChecks 使用）
    void AddTestSkill(SkillDefinition definition) {
        m_skills[definition.id] = std::move(definition);
    }

private:
    std::unordered_map<std::string, SkillDefinition> m_skills;
};

} // namespace legend::skill
