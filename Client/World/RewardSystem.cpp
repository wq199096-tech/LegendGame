#include "Client/World/RewardSystem.h"

#include "Client/Character/PlayerCharacter.h"
#include "Client/Loot/LootManager.h"
#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Client/World/MonsterSpawner.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Combat/DeathEvent.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/ActorType.h"
#include "Engine/Item/ItemDatabase.h"

namespace legend::world {

int RewardSystem::ProcessDeathEvents(combat::CombatSystem& combat,
                                     const entity::ActorRegistry& registry,
                                     PlayerCharacter* player, LootManager& loot,
                                     const MonsterSpawner& spawner,
                                     const item::ItemDatabase& items) {
    const std::vector<combat::DeathEvent> deaths = combat.GetRecentDeaths(); // 拷贝后清空
    combat.ClearRecentDeaths(); // 一次消费，保证 exactly-once

    int processed = 0;
    for (const combat::DeathEvent& death : deaths) {
        // victim 必须是 Monster（死亡当帧尚未 Despawn，可 Resolve 模板）
        const entity::Character* victim = registry.Get(death.victimId);
        if (victim == nullptr || victim->GetActorType() != entity::ActorType::Monster) {
            continue; // Player 死亡等不做掉落/经验
        }
        // 击杀归属：killer 必须是 Player（不能任何怪死都发经验）
        if (player == nullptr || death.killerId != player->GetId()) {
            continue;
        }
        const auto* monster = static_cast<const MonsterCharacter*>(victim);
        const MonsterDefinition* definition = spawner.GetDefinition(monster->GetMonsterTemplateId());
        if (definition == nullptr) {
            LOG_WARN("[Reward] missing monster definition '" + monster->GetMonsterTemplateId() +
                     "', reward skipped.");
            continue;
        }
        // 1) 经验奖励（数据驱动 rewards.exp；内部连续升级 + 属性成长）
        if (definition->expReward > 0) {
            player->AddExperience(definition->expReward);
            LOG_INFO("[Reward] exp +" + std::to_string(definition->expReward) + " -> Player (kill " +
                     monster->GetName() + ")");
        }
        // 2) 掉落生成（立即，位置 = 死亡 Feet；不等 Despawn）
        loot.SpawnMonsterLoot(*definition, death.position);
        ++processed;
    }
    return processed;
}

} // namespace legend::world
