#pragma once

#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "Client/World/MonsterCharacter.h"
#include "Client/World/MonsterDefinition.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Map/MapTypes.h"

namespace legend::map {
class Map;
}

namespace legend::item {
class ItemDatabase;
}

namespace legend::resource {
class ResourceManager;
}

namespace legend::world {

// 怪物生成器：根据地图 monsterSpawns 数据 + monster.json 模板生成怪物。
// 模板资源（MonsterDefinition / CharacterDefinition / Clips / SpriteSheet）按模板 id 缓存共享，
// 每只怪独立 AnimationPlayer（在 MonsterCharacter 内部）。
class MonsterSpawner {
public:
    // 加载 monster.json 模板注册表 + 预加载全部模板资源（仅3类，开销小）
    bool Initialize(const std::string& assetsRoot, legend::resource::ResourceManager& resources);

    // 生成一个出生区域的全部怪物（区域内部随机找合法位置，失败的位置记 Warning 不死循环）。
    // 返回生成的怪物列表（WorldActorManager 接管所有权）。
    std::vector<std::unique_ptr<MonsterCharacter>> SpawnArea(const map::MapSpawnArea& area,
                                                             const map::Map& map,
                                                             std::mt19937& rng) const;

    // 按模板 id 生成单只怪物（重生用）：区域内随机合法点；失败返回 nullptr
    std::unique_ptr<MonsterCharacter> SpawnSingle(const std::string& templateId,
                                                  const map::MapSpawnArea& area,
                                                  const map::Map& map,
                                                  std::mt19937& rng) const;

    const MonsterDefinition* GetDefinition(const std::string& templateId) const;
    bool HasDefinition(const std::string& templateId) const {
        return m_templates.count(templateId) > 0;
    }
    std::size_t TemplateCount() const { return m_templates.size(); }

    // 阶段6：剔除指向不存在 Item 的 loot entry（ItemDatabase 加载后统一调用，不崩游戏）
    void ValidateLootEntries(const item::ItemDatabase& items);

private:
    struct TemplateAssets {
        MonsterDefinition definition;
        legend::animation::CharacterDefinition charDefinition;
        std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips;
        std::shared_ptr<legend::animation::SpriteSheet> sheet;
    };

    bool LoadTemplateAssets(MonsterDefinition definition, TemplateAssets& out) const;

    std::unordered_map<std::string, TemplateAssets> m_templates;
    std::string m_assetsRoot;
    legend::resource::ResourceManager* m_resources = nullptr;
};

} // namespace legend::world
