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
// 阶段20 指令九/十 → 阶段22 22.11 改造：NpcRegistry —— NPC 定义注册表
// （附带 Dialogue 基础文本注册表，dialogueId = npcDefinitionId）。
// 生产从 Data/World/npcs.json 加载（WorldServer::Initialize 统一注入）；
// 目录缺失时用 MakeDefaultWorldData 的出厂配置（22.18 迁移源）。
// 只读访问走 Instance()；数据注入走静态 Load*。
// 启动 ValidateNpcs 校验（npcId 唯一/任务存在于 QuestRegistry/shop/teleport 引用有效）。
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

    // 数据注入（WorldServer::Initialize / 测试 fixture）。
    static void LoadFromDefinitions(std::vector<NpcDefinition> npcs,
                                    std::vector<DialogueDefinition> dialogues);
    static void LoadDefaults();

private:
    static NpcRegistry& Mutable();

    std::vector<NpcDefinition> m_npcs;
    std::vector<DialogueDefinition> m_dialogues;
};

} // namespace legend::world
