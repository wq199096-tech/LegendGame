#pragma once

#include "Client/WorldNetwork/RemoteStatusEffect.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段16 指令六十一：RemoteStatusEffectContainer —— 客户端状态容器（仅展示）。
// key = instanceId（同实例去重，指令一百二十三）；未知 instanceId 的 Updated/
// Removed 忽略、不创建幽灵状态（指令一百二十四/一百二十五）；Snapshot 以服务
// 器列表为准做替换（指令一百二十六）。
// ---------------------------------------------------------------------------
class RemoteStatusEffectContainer {
public:
    // 指令五十二：Applied —— 已存在（同 instanceId）则忽略，保持唯一。
    void Apply(const RemoteStatusEffect& effect) {
        if (m_effects.find(effect.instanceId) == m_effects.end()) {
            m_effects[effect.instanceId] = effect;
        }
    }

    // 指令五十三：Updated —— 未知 instanceId 忽略（指令一百二十四）。
    void Update(std::uint64_t instanceId, std::uint8_t stacks, std::uint32_t remainingMs) {
        const auto it = m_effects.find(instanceId);
        if (it == m_effects.end()) {
            return;
        }
        it->second.stacks = stacks;
        it->second.remainingMs = remainingMs;
    }

    // 指令五十四：Removed —— 未知 instanceId 忽略（指令一百二十五）。
    void Remove(std::uint64_t instanceId) { m_effects.erase(instanceId); }

    // 指令一百二十六：Snapshot —— 以服务器列表为准（多余删除、缺少创建）。
    void SnapshotReplace(const std::vector<RemoteStatusEffect>& effects) {
        std::unordered_map<std::uint64_t, RemoteStatusEffect> next;
        for (const auto& effect : effects) {
            // 保留本地已知的 duration/source 元数据（若存在）。
            const auto it = m_effects.find(effect.instanceId);
            RemoteStatusEffect merged = effect;
            if (it != m_effects.end()) {
                merged.durationMs = it->second.durationMs;
                merged.sourceEntityId = it->second.sourceEntityId;
            }
            next[effect.instanceId] = merged;
        }
        m_effects.swap(next);
    }

    const RemoteStatusEffect* Find(std::uint64_t instanceId) const {
        const auto it = m_effects.find(instanceId);
        return it != m_effects.end() ? &it->second : nullptr;
    }
    std::size_t Count() const { return m_effects.size(); }
    void Clear() { m_effects.clear(); }
    const std::unordered_map<std::uint64_t, RemoteStatusEffect>& All() const { return m_effects; }

private:
    std::unordered_map<std::uint64_t, RemoteStatusEffect> m_effects;
};

} // namespace legend::client
