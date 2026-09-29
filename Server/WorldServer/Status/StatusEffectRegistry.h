#pragma once

#include "Shared/Status/StatusEffectDefinition.h"
#include "Shared/Status/StatusEffectTypes.h"

#include <cstddef>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 → 阶段23 23.22：StatusEffectRegistry —— 状态效果定义注册表。
// 生产从 Data/Game/statuses.json 加载（WorldServer::Start 注入）；Definition
// 不能由 Client 上传（与 SkillRegistry 同构）。
// ---------------------------------------------------------------------------
class StatusEffectRegistry {
public:
    StatusEffectRegistry();

    // 查找定义；未知 effectId 返回 nullptr（-> UnknownEffect）。
    const StatusEffectDefinition* FindEffect(StatusEffectId effectId) const;
    std::size_t Count() const { return m_effects.size(); }

    // 数据注入（WorldServer::Start）。
    void LoadFromDefinitions(std::vector<StatusEffectDefinition> effects);
    void LoadDefaults();

private:
    std::vector<StatusEffectDefinition> m_effects; // 稳定地址（指针指向元素）
};

} // namespace legend::world
