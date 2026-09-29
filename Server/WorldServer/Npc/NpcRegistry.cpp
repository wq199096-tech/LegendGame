#include "Server/WorldServer/Npc/NpcRegistry.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Server/WorldServer/Npc/ShopService.h"
#include "Server/WorldServer/Npc/TeleportService.h"

namespace legend::world {

const NpcRegistry& NpcRegistry::Instance() {
    static const NpcRegistry registry;
    return registry;
}

NpcRegistry::NpcRegistry() {
    // ------------------------------------------------------------------
    // 阶段20 指令九：4 个固定测试 NPC。
    // ------------------------------------------------------------------
    {
        NpcDefinition npc;
        npc.npcDefinitionId = 5001;
        npc.name = "Village Elder";
        npc.npcType = NpcType::QuestGiver;
        npc.mapId = 1;
        npc.spawnX = 300.0f;
        npc.spawnY = 300.0f;
        npc.interactionRange = 120.0f;
        npc.dialogueId = 5001;
        npc.shopId = 0;
        npc.teleportId = 0;
        npc.questIds = {4001, 4002, 4003, 4005};
        npc.visualId = 5001;
        m_npcs.push_back(std::move(npc));
    }
    {
        NpcDefinition npc;
        npc.npcDefinitionId = 5002;
        npc.name = "General Merchant";
        npc.npcType = NpcType::Merchant;
        npc.mapId = 1;
        npc.spawnX = 450.0f;
        npc.spawnY = 300.0f;
        npc.interactionRange = 120.0f;
        npc.dialogueId = 5002;
        npc.shopId = 6001;
        npc.teleportId = 0;
        npc.visualId = 5002;
        m_npcs.push_back(std::move(npc));
    }
    {
        NpcDefinition npc;
        npc.npcDefinitionId = 5003;
        npc.name = "Wayfarer";
        npc.npcType = NpcType::Teleporter;
        npc.mapId = 1;
        npc.spawnX = 600.0f;
        npc.spawnY = 300.0f;
        npc.interactionRange = 120.0f;
        npc.dialogueId = 5003;
        npc.teleportId = 7001;
        npc.visualId = 5003;
        m_npcs.push_back(std::move(npc));
    }
    {
        NpcDefinition npc;
        npc.npcDefinitionId = 5004;
        npc.name = "Explorer Guide";
        npc.npcType = NpcType::MultiFunction;
        npc.mapId = 1;
        npc.spawnX = 750.0f;
        npc.spawnY = 300.0f;
        npc.interactionRange = 120.0f;
        npc.dialogueId = 5004;
        npc.teleportId = 7002;
        npc.questIds = {4004};
        npc.visualId = 5004;
        m_npcs.push_back(std::move(npc));
    }
    // Dialogue 基础文本（dialogueId = npcDefinitionId）。
    m_dialogues.push_back({5001, "Village Elder", "The slimes have been restless lately. Will you help us?"});
    m_dialogues.push_back({5002, "General Merchant", "Welcome! Finest goods in the village."});
    m_dialogues.push_back({5003, "Wayfarer", "I can take you anywhere, for a price."});
    m_dialogues.push_back({5004, "Explorer Guide", "Looking for adventure? The far plains await."});
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

} // namespace legend::world
