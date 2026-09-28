#pragma once

#include "Shared/Status/StatusEffectDefinition.h"
#include "Shared/Status/StatusEffectTypes.h"

#include <cstddef>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16：StatusEffectRegistry —— 状态效果定义注册表。
// 硬编码五个定义（指令八十五），不上 JSON、不进数据库；Definition 不能由
// Client 上传（与阶段15 SkillRegistry 同构）。
// ---------------------------------------------------------------------------
class StatusEffectRegistry {
public:
    StatusEffectRegistry();

    // 查找定义；未知 effectId 返回 nullptr（-> UnknownEffect）。
    const StatusEffectDefinition* FindEffect(StatusEffectId effectId) const;
    std::size_t Count() const { return m_effects.size(); }

private:
    std::vector<StatusEffectDefinition> m_effects; // 稳定地址（指针指向元素）
};

} // namespace legend::world
