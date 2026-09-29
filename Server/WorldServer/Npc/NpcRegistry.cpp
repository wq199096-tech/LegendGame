#include "Server/WorldServer/Npc/NpcRegistry.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Server/WorldServer/Npc/ShopService.h"
#include "Server/WorldServer/Npc/TeleportService.h"
#include "Shared/WorldData/WorldDataJson.h"

namespace legend::world {

const NpcRegistry& NpcRegistry::Instance() {
    static const NpcRegistry registry;
    return registry;
}

NpcRegistry& NpcRegistry::Mutable() {
    return const_cast<NpcRegistry&>(Instance());
}

NpcRegistry::NpcRegistry() {
    // 阶段22 22.11：硬编码迁入 Data/World（出厂数据由 MakeDefaultWorldData 提供，
    // WorldServer::Initialize 启动时统一 LoadDefaults/LoadFromDefinitions）。
}

const NpcDefinition* NpcRegistry::FindNpc(NpcDefinitionId npcDefinitionId) const {
    for (const auto& npc : m_npcs) {
        if (npc.npcDefinitionId == npcDefinitionId) {
            return &npc;
        }
    }
    return nullptr;
}

const DialogueDefinition* NpcRegistry::FindDialogue(std::uint32_t dialogueId) const {
    for (const auto& dialogue : m_dialogues) {
        if (dialogue.dialogueId == dialogueId) {
            return &dialogue;
        }
    }
    return nullptr;
}

bool NpcRegistry::ValidateNpcs(const QuestRegistry& questRegistry, std::string& error) const {
    // 1) npcDefinitionId 唯一。
    for (std::size_t i = 0; i < m_npcs.size(); ++i) {
        for (std::size_t j = i + 1; j < m_npcs.size(); ++j) {
            if (m_npcs[i].npcDefinitionId == m_npcs[j].npcDefinitionId) {
                error = "duplicate npcDefinitionId " + std::to_string(m_npcs[i].npcDefinitionId);
                return false;
            }
        }
    }
    for (const auto& npc : m_npcs) {
        // 2) 引用的任务存在于 QuestRegistry。
        for (const QuestId questId : npc.questIds) {
            if (questRegistry.FindQuest(questId) == nullptr) {
                error = "npc " + std::to_string(npc.npcDefinitionId) + " references quest " +
                        std::to_string(questId) + " not found";
                return false;
            }
        }
        // 3) shop/teleport 引用有效。
        if (npc.shopId != 0 && ShopRegistry::Instance().FindShop(npc.shopId) == nullptr) {
            error = "npc " + std::to_string(npc.npcDefinitionId) + " references shop " +
                    std::to_string(npc.shopId) + " not found";
            return false;
        }
        if (npc.teleportId != 0 &&
            TeleportRegistry::Instance().FindTeleport(npc.teleportId) == nullptr) {
            error = "npc " + std::to_string(npc.npcDefinitionId) + " references teleport " +
                    std::to_string(npc.teleportId) + " not found";
            return false;
        }
        // 4) dialogue 存在（有功能的 NPC 必须有对话）。
        if (FindDialogue(npc.dialogueId) == nullptr) {
            error = "npc " + std::to_string(npc.npcDefinitionId) + " dialogue " +
                    std::to_string(npc.dialogueId) + " not found";
            return false;
        }
    }
    return true;
}

void NpcRegistry::LoadFromDefinitions(std::vector<NpcDefinition> npcs,
                                      std::vector<DialogueDefinition> dialogues) {
    Mutable().m_npcs = std::move(npcs);
    Mutable().m_dialogues = std::move(dialogues);
}

void NpcRegistry::LoadDefaults() {
    WorldDataSet defaults = MakeDefaultWorldData();
    LoadFromDefinitions(std::move(defaults.npcs), std::move(defaults.dialogues));
}

} // namespace legend::world
