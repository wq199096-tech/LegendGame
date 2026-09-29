#include "Server/WorldServer/Quest/QuestRegistry.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Item/ItemRegistry.h"

namespace legend::world {

const QuestRegistry& QuestRegistry::Instance() {
    static const QuestRegistry registry;
    return registry;
}

QuestRegistry::QuestRegistry() {
    // ------------------------------------------------------------------
    // Quest 4001 Slime Hunter（指令十一）：Kill Training Slime x5，
    // 奖励 EXP 100 / Gold 20，无 Item，minLevel=1，无前置。
    // ------------------------------------------------------------------
    {
        QuestDefinition quest;
        quest.questId = kQuestIdSlimeHunter;
        quest.name = "Slime Hunter";
        quest.description = "Defeat 5 Training Slimes.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 0;
        quest.repeatable = false;
        QuestObjectiveDefinition kill;
        kill.objectiveId = 40011;
        kill.type = QuestObjectiveType::KillMonster;
        kill.targetId = kTrainingSlimeTypeId;
        kill.requiredCount = 5;
        quest.objectives.push_back(kill);
        quest.reward.exp = 100;
        quest.reward.gold = 20;
        m_quests.push_back(std::move(quest));
    }
    // ------------------------------------------------------------------
    // Quest 4002 Core Collector：Collect Slime Core x3（当前持有量型），
    // 奖励 EXP 80 / Gold 10，前置 4001 Completed。
    // ------------------------------------------------------------------
    {
        QuestDefinition quest;
        quest.questId = kQuestIdCoreCollector;
        quest.name = "Core Collector";
        quest.description = "Collect 3 Slime Cores.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = kQuestIdSlimeHunter;
        quest.repeatable = false;
        QuestObjectiveDefinition collect;
        collect.objectiveId = 40021;
        collect.type = QuestObjectiveType::CollectItem;
        collect.targetId = kItemSlimeCoreId;
        collect.requiredCount = 3;
        quest.objectives.push_back(collect);
        quest.reward.exp = 80;
        quest.reward.gold = 10;
        m_quests.push_back(std::move(quest));
    }
    // ------------------------------------------------------------------
    // Quest 4003 Growing Warrior：Reach Level 3（targetId=3，指令二十六），
    // 奖励 Gold 50，前置 4001。
    // ------------------------------------------------------------------
    {
        QuestDefinition quest;
        quest.questId = kQuestIdGrowingWarrior;
        quest.name = "Growing Warrior";
        quest.description = "Reach level 3.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = kQuestIdSlimeHunter;
        quest.repeatable = false;
        QuestObjectiveDefinition reach;
        reach.objectiveId = 40031;
        reach.type = QuestObjectiveType::ReachLevel;
        reach.targetId = 3;
        reach.requiredCount = 1;
        quest.objectives.push_back(reach);
        quest.reward.exp = 0;
        quest.reward.gold = 50;
        m_quests.push_back(std::move(quest));
    }
    // ------------------------------------------------------------------
    // Quest 4004 Explorer：Reach Area mapId=1 center(1500,1500) radius 100，
    // 奖励 EXP 50 / Gold 10，无前置。
    // ------------------------------------------------------------------
    {
        QuestDefinition quest;
        quest.questId = kQuestIdExplorer;
        quest.name = "Explorer";
        quest.description = "Explore the far plains (1500,1500).";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = 0;
        quest.repeatable = false;
        QuestObjectiveDefinition area;
        area.objectiveId = 40041;
        area.type = QuestObjectiveType::ReachArea;
        area.targetId = 0;
        area.requiredCount = 1;
        area.mapId = 1;
        area.areaX = 1500.0f;
        area.areaY = 1500.0f;
        area.areaRadius = 100.0f;
        quest.objectives.push_back(area);
        quest.reward.exp = 50;
        quest.reward.gold = 10;
        m_quests.push_back(std::move(quest));
    }
    // ------------------------------------------------------------------
    // Quest 4005 Slime Cleanup：多目标 Kill Slime x3 + Collect Core x2，
    // 奖励 EXP 150 / Gold 30 / Rusty Sword x1，前置 4002 Completed。
    // ------------------------------------------------------------------
    {
        QuestDefinition quest;
        quest.questId = kQuestIdSlimeCleanup;
        quest.name = "Slime Cleanup";
        quest.description = "Kill 3 slimes and gather 2 cores.";
        quest.minLevel = 1;
        quest.prerequisiteQuestId = kQuestIdCoreCollector;
        quest.repeatable = false;
        QuestObjectiveDefinition kill;
        kill.objectiveId = 40051;
        kill.type = QuestObjectiveType::KillMonster;
        kill.targetId = kTrainingSlimeTypeId;
        kill.requiredCount = 3;
        quest.objectives.push_back(kill);
        QuestObjectiveDefinition collect;
        collect.objectiveId = 40052;
        collect.type = QuestObjectiveType::CollectItem;
        collect.targetId = kItemSlimeCoreId;
        collect.requiredCount = 2;
        quest.objectives.push_back(collect);
        quest.reward.exp = 150;
        quest.reward.gold = 30;
        quest.reward.itemDefinitionId = kItemRustySwordId;
        quest.reward.itemQuantity = 1;
        m_quests.push_back(std::move(quest));
    }
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

} // namespace legend::world
