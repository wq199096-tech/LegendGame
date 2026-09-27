#pragma once

#include <string>

#include "Engine/Skill/SkillDatabase.h"

namespace legend::skill {

// 玩家技能栏：4 个槽位（Slot 0~3），每槽可空。
// 默认绑定：1=power_slash 2=whirlwind 3=piercing_strike 4=heavy_strike。
// SetSlot 校验 skillId 必须存在于 SkillDatabase（不存在拒绝，阶段8指令十七）。
class SkillLoadout {
public:
    static constexpr int kSlotCount = 4;

    // 默认 Loadout：SkillDatabase 初始化成功后调用；
    // 某个默认 skillId 不存在 -> LOG_ERROR + 该槽保持空（不绑定死字符串）
    void InitializeDefaults(const SkillDatabase& database);

    // 设置槽位技能：index 越界或 skillId 不存在返回 false
    bool SetSlot(int index, const std::string& skillId, const SkillDatabase& database);
    void ClearSlot(int index); // 越界静默（无操作）
    // 槽位技能 id（空串=空槽；越界返回空串）
    const std::string& GetSkillId(int index) const;
    int GetSlotCount() const { return kSlotCount; }

private:
    std::string m_slots[kSlotCount];
};

} // namespace legend::skill
