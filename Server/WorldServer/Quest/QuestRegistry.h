#pragma once

#include "Shared/Item/ItemTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Quest/QuestDefinition.h"
#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

class ItemRegistry;

// ---------------------------------------------------------------------------
// 阶段19 指令十二：QuestRegistry —— 硬编码 5 个固定测试任务（4001~4005）。
// 只读；启动时 ValidateDefinitions 校验（questId 唯一 / objectiveId 唯一 /
// 前置存在 / requiredCount>0 / 奖励 Item 存在于 ItemRegistry）。
// 线程安全：构造后只读，io 线程与 DB Worker 线程（离线推进）并发读安全。
// ---------------------------------------------------------------------------
class QuestRegistry {
public:
    // 硬编码注册表（只读单例——DB Worker 离线推进与测试也需要访问）。
    static const QuestRegistry& Instance();

    QuestRegistry();

    std::size_t Count() const { return m_quests.size(); }
    const QuestDefinition* FindQuest(QuestId questId) const;
    const std::vector<QuestDefinition>& AllQuests() const { return m_quests; }

    // 指令十二：启动校验。rewardItemRegistry 传 nullptr 时跳过奖励物品检查
    //（纯逻辑测试用）。
    bool ValidateDefinitions(const ItemRegistry* rewardItemRegistry, std::string& error) const;

private:
    std::vector<QuestDefinition> m_quests;
};

// 阶段19 固定测试任务 Id（指令十一）。
inline constexpr QuestId kQuestIdSlimeHunter = 4001;    // Kill Slime x5
inline constexpr QuestId kQuestIdCoreCollector = 4002;  // Collect Slime Core x3（前置 4001）
inline constexpr QuestId kQuestIdGrowingWarrior = 4003; // Reach Level 3（前置 4001）
inline constexpr QuestId kQuestIdExplorer = 4004;       // Reach Area(1500,1500) r100
inline constexpr QuestId kQuestIdSlimeCleanup = 4005;   // 多目标（前置 4002）

} // namespace legend::world
