#pragma once

#include "Server/WorldServer/Status/ActiveStatusEffect.h"

#include "Shared/Status/StatusEffectTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 指令十五：StatusEffectContainer —— 状态容器（PlayerSession 与
// MonsterEntity 共用，避免两套重复逻辑）。key = effectId（同一 effectId 在
// 同一目标只允许一个 Active 实例，指令十四）。仅 World io 线程访问。
// 职责：Find / Apply / Remove / Clear / Snapshot / Count（指令十五）。
// 策略判定（refresh/stack）在 StatusEffectService，容器只做存取。
// ---------------------------------------------------------------------------
class StatusEffectContainer {
public:
    // 指令十五：Find —— 按 effectId 查找。
    ActiveStatusEffect* Find(StatusEffectId effectId) {
        const auto it = m_effects.find(effectId);
        return it != m_effects.end() ? &it->second : nullptr;
    }
    const ActiveStatusEffect* Find(StatusEffectId effectId) const {
        const auto it = m_effects.find(effectId);
        return it != m_effects.end() ? &it->second : nullptr;
    }

    // 指令十五：Apply —— 写入/覆盖实例（调用方已按策略决定 stacks/expire）。
    ActiveStatusEffect& Apply(const ActiveStatusEffect& effect) {
        auto& slot = m_effects[effect.EffectId()];
        slot = effect;
        return slot;
    }

    // 指令十五：Remove —— 按 effectId 移除；返回是否原本存在。
    bool Remove(StatusEffectId effectId) { return m_effects.erase(effectId) != 0; }

    // 指令十五：Clear —— 清空（死亡/重启用）。
    void Clear() { m_effects.clear(); }

    // 指令十五：Count。
    std::size_t Count() const { return m_effects.size(); }
    bool Empty() const { return m_effects.empty(); }

    // 指令十五：Snapshot —— 收集全部实例（服务端/测试用；发送侧另加 remaining）。
    std::vector<const ActiveStatusEffect*> Snapshot() const {
        std::vector<const ActiveStatusEffect*> result;
        result.reserve(m_effects.size());
        for (const auto& [effectId, effect] : m_effects) {
            result.push_back(&effect);
        }
        return result;
    }

    // 遍历（Tick 用，仅 io 线程）。
    std::unordered_map<StatusEffectId, ActiveStatusEffect>& All() { return m_effects; }
    const std::unordered_map<StatusEffectId, ActiveStatusEffect>& All() const {
        return m_effects;
    }

private:
    std::unordered_map<StatusEffectId, ActiveStatusEffect> m_effects;
};

} // namespace legend::world
