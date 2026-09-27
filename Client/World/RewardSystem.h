#pragma once

// 全局命名空间类（非 legend::world），必须前向声明在全局作用域
class PlayerCharacter;

namespace legend::combat {
class CombatSystem;
}

namespace legend::entity {
class ActorRegistry;
}

namespace legend::item {
class ItemDatabase;
}

namespace legend::world {

class LootManager;
class MonsterSpawner;

// 奖励系统（游戏规则上层）：消费 CombatSystem 的 DeathEvent -> Exp + Loot。
// Combat 只产生事实（谁死了 / 谁杀的），奖励不属于 Combat。
// DeathEvent 在 Alive->Dead 一刻产生一次且一次消费 -> 经验/掉落天然 exactly-once。
class RewardSystem {
public:
    // 消费并清空 recentDeaths。victim=Monster 且 killer=Player：
    // PlayerProgression.AddExp(monster.expReward) + LootManager.SpawnMonsterLoot(deathPos)。
    // 返回处理的死亡事件数（供自动测试统计）。
    int ProcessDeathEvents(combat::CombatSystem& combat, const entity::ActorRegistry& registry,
                           PlayerCharacter* player, LootManager& loot,
                           const MonsterSpawner& spawner, const item::ItemDatabase& items);
};

} // namespace legend::world
