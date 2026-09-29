#pragma once

#include "Shared/Dialogue/DialogueDefinition.h"
#include "Shared/Npc/NpcDefinition.h"
#include "Shared/Npc/NpcTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

class QuestRegistry;

// ---------------------------------------------------------------------------
// 阶段20 指令九/十：NpcRegistry —— 4 个固定测试 NPC（5001~5004）硬编码注册表
// （附带 Dialogue 基础文本注册表，dialogueId = npcDefinitionId）。
// 只读单例；启动 ValidateNpcs 校验（npcId 唯一/任务存在于 QuestRegistry/shop/teleport 引用有效）。
// ---------------------------------------------------------------------------
class NpcRegistry {
public:
    static const NpcRegistry& Instance();

    NpcRegistry();

    std::size_t Count() const { return m_npcs.size(); }
    const NpcDefinition* FindNpc(NpcDefinitionId npcDefinitionId) const;
    const std::vector<NpcDefinition>& AllNpcs() const { return m_npcs; }

    // Dialogue 基础文本（指令二十三/二十四：title/text；Options 动态生成）。
    const DialogueDefinition* FindDialogue(std::uint32_t dialogueId) const;

    // 启动校验（指令十二类比）。
    bool ValidateNpcs(const QuestRegistry& questRegistry, std::string& error) const;

private:
    std::vector<NpcDefinition> m_npcs;
    std::vector<DialogueDefinition> m_dialogues;
};

} // namespace legend::world
