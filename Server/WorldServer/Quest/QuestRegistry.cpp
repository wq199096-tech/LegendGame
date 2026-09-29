#include "Server/WorldServer/Quest/QuestRegistry.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Shared/GameData/GameDataJson.h"

namespace legend::world {

const QuestRegistry& QuestRegistry::Instance() {
    static const QuestRegistry registry;
    return registry;
}

QuestRegistry& QuestRegistry::Mutable() {
    return const_cast<QuestRegistry&>(Instance());
}

QuestRegistry::QuestRegistry() {
    // 阶段23 23.22：构造即填充出厂默认（DB Worker 离线推进等早期调用点依赖）。
    // 注意：必须直接填充成员——经 Mutable()/Instance() 会在 MSVC magic-static
    // 初始化中重入死锁（构造期调用 LoadDefaults→Mutable→Instance）。
    m_quests = MakeDefaultGameData().quests;
}

const QuestDefinition* QuestRegistry::FindQuest(QuestId questId) const {
    for (const auto& quest : m_quests) {
        if (quest.questId == questId) {
            return &quest;
        }
    }
    return nullptr;
}

bool QuestRegistry::ValidateDefinitions(const ItemRegistry* rewardItemRegistry,
                                        std::string& error) const {
    // 1) questId 唯一。
    for (std::size_t i = 0; i < m_quests.size(); ++i) {
        for (std::size_t j = i + 1; j < m_quests.size(); ++j) {
            if (m_quests[i].questId == m_quests[j].questId) {
                error = "duplicate questId " + std::to_string(m_quests[i].questId);
                return false;
            }
        }
    }
    for (const auto& quest : m_quests) {
        // 2) objectiveId 唯一 + requiredCount > 0（指令十二）。
        for (std::size_t a = 0; a < quest.objectives.size(); ++a) {
            if (quest.objectives[a].requiredCount == 0) {
                error = "quest " + std::to_string(quest.questId) + " objective " +
                        std::to_string(quest.objectives[a].objectiveId) + " requiredCount == 0";
                return false;
            }
            for (std::size_t b = a + 1; b < quest.objectives.size(); ++b) {
                if (quest.objectives[a].objectiveId == quest.objectives[b].objectiveId) {
                    error = "quest " + std::to_string(quest.questId) + " duplicate objectiveId " +
                            std::to_string(quest.objectives[a].objectiveId);
                    return false;
                }
            }
        }
        // 3) 前置任务存在（指令十二；前置不校验递归闭环——阶段19 手工配置 5 条）。
        if (quest.prerequisiteQuestId != 0 && FindQuest(quest.prerequisiteQuestId) == nullptr) {
            error = "quest " + std::to_string(quest.questId) + " prerequisite " +
                    std::to_string(quest.prerequisiteQuestId) + " not found";
            return false;
        }
        // 4) 奖励 Item 存在于 ItemRegistry（指令十二）。
        if (quest.reward.itemDefinitionId != 0 && rewardItemRegistry != nullptr &&
            rewardItemRegistry->Find(quest.reward.itemDefinitionId) == nullptr) {
            error = "quest " + std::to_string(quest.questId) + " reward item " +
                    std::to_string(quest.reward.itemDefinitionId) + " not in ItemRegistry";
            return false;
        }
        // 5) repeatable 阶段19 全部 false（指令十）。
        if (quest.repeatable) {
            error = "quest " + std::to_string(quest.questId) + " must not be repeatable (stage 19)";
            return false;
        }
    }
    return true;
}


void QuestRegistry::LoadFromDefinitions(std::vector<QuestDefinition> quests) {
    Mutable().m_quests = std::move(quests);
}

void QuestRegistry::LoadDefaults() {
    LoadFromDefinitions(MakeDefaultGameData().quests);
}

} // namespace legend::world