#include "Engine/Skill/SkillLoadout.h"

#include "Engine/Debug/Logger.h"

namespace legend::skill {

namespace {
// 默认技能栏绑定（阶段8指令十五）
constexpr const char* kDefaultSkillIds[SkillLoadout::kSlotCount] = {
    "power_slash", "whirlwind", "piercing_strike", "heavy_strike"};
} // namespace

void SkillLoadout::InitializeDefaults(const SkillDatabase& database) {
    for (int i = 0; i < kSlotCount; ++i) {
        const std::string skillId = kDefaultSkillIds[i];
        if (!database.Exists(skillId)) {
            LOG_ERROR("SkillLoadout: default skill '" + skillId +
                      "' missing in SkillDatabase, slot " + std::to_string(i) +
                      " stays empty.");
            m_slots[i].clear();
            continue;
        }
        m_slots[i] = skillId;
    }
    LOG_INFO("SkillLoadout: defaults bound (4 slots).");
}

bool SkillLoadout::SetSlot(int index, const std::string& skillId,
                           const SkillDatabase& database) {
    if (index < 0 || index >= kSlotCount) {
        return false;
    }
    if (!database.Exists(skillId)) {
        LOG_WARN("SkillLoadout: SetSlot rejected, unknown skill id '" + skillId + "'.");
        return false;
    }
    // 同一技能允许放多个槽（阶段8指令十七：行为明确——允许）
    m_slots[index] = skillId;
    return true;
}

void SkillLoadout::ClearSlot(int index) {
    if (index < 0 || index >= kSlotCount) {
        return;
    }
    m_slots[index].clear();
}

const std::string& SkillLoadout::GetSkillId(int index) const {
    static const std::string kEmpty;
    if (index < 0 || index >= kSlotCount) {
        return kEmpty;
    }
    return m_slots[index];
}

} // namespace legend::skill
