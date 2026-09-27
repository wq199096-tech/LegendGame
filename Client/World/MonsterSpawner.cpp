#include "Client/World/MonsterSpawner.h"

#include "Client/World/SpawnArea.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/EntityIdAllocator.h"
#include "Engine/Map/Map.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::world {

bool MonsterSpawner::Initialize(const std::string& assetsRoot,
                                legend::resource::ResourceManager& resources) {
    m_assetsRoot = assetsRoot;
    m_resources = &resources;
    m_templates.clear();

    std::unordered_map<std::string, MonsterDefinition> definitions;
    if (!LoadMonsterRegistry(assetsRoot + "/Monsters/monster.json", definitions)) {
        LOG_ERROR("MonsterSpawner: monster registry load failed.");
        return false;
    }

    // 预加载全部模板资源（SpriteSheet/Clips 共享给同模板所有怪物实例）。
    // 单模板失败：LOG_ERROR + 跳过，继续加载其他模板；全部失败才初始化失败。
    int skipped = 0;
    for (auto& [id, definition] : definitions) {
        TemplateAssets assets;
        assets.definition = definition;
        if (!LoadTemplateAssets(definition, assets)) {
            LOG_ERROR("MonsterSpawner: template '" + id +
                      "' asset load failed, skipped (other templates continue).");
            ++skipped;
            continue;
        }
        m_templates[id] = std::move(assets);
    }
    if (m_templates.empty()) {
        LOG_ERROR("MonsterSpawner: all " + std::to_string(definitions.size()) +
                  " template(s) failed to load.");
        return false;
    }
    if (skipped > 0) {
        LOG_WARN("MonsterSpawner: " + std::to_string(skipped) + " template(s) skipped, " +
                 std::to_string(m_templates.size()) + " valid template(s) ready.");
    }
    LOG_INFO("MonsterSpawner initialized: " + std::to_string(m_templates.size()) +
             " template(s) ready.");
    return true;
}

bool MonsterSpawner::LoadTemplateAssets(MonsterDefinition definition,
                                        TemplateAssets& out) const {
    if (m_resources == nullptr) {
        return false;
    }
    if (!legend::animation::LoadCharacterDefinition(
            m_assetsRoot + "/" + definition.characterPath, out.charDefinition)) {
        return false;
    }
    auto clips = std::make_shared<const std::unordered_map<std::string, legend::animation::AnimationClip>>(
        legend::animation::LoadAnimationClips(m_assetsRoot + "/" +
                                              out.charDefinition.animationsPath));
    auto sheet = legend::animation::LoadSpriteSheet(out.charDefinition, *m_resources);
    if (clips->empty() || !sheet || !sheet->IsValid()) {
        LOG_ERROR("MonsterSpawner: clips/sheet load failed for '" + definition.id + "'.");
        return false;
    }
    out.clips = std::move(clips);
    out.sheet = std::move(sheet);
    return true;
}

std::vector<std::unique_ptr<MonsterCharacter>> MonsterSpawner::SpawnArea(
    const map::MapSpawnArea& area, const map::Map& map, std::mt19937& rng) const {
    std::vector<std::unique_ptr<MonsterCharacter>> spawned;
    const auto it = m_templates.find(area.monsterId);
    if (it == m_templates.end()) {
        LOG_WARN("MonsterSpawner: unknown monster template '" + area.monsterId +
                 "' (spawn area " + std::to_string(area.id) + "), skipped.");
        return spawned;
    }
    const TemplateAssets& assets = it->second;
    spawned.reserve(static_cast<std::size_t>(area.count));

    for (int i = 0; i < area.count; ++i) {
        math::Vector2 position{0.0f, 0.0f};
        if (!SpawnArea::FindWalkableSpawnPosition(map, assets.charDefinition.footprint, area,
                                                  rng, position)) {
            LOG_WARN("MonsterSpawner: spawn area " + std::to_string(area.id) + " (" +
                     area.monsterId + ") member " + std::to_string(i) +
                     ": no walkable position found, skipped.");
            continue;
        }
        auto monster = std::make_unique<MonsterCharacter>(
            legend::entity::EntityIdAllocator::Next(), assets.definition,
            assets.charDefinition, assets.clips, assets.sheet, area.id, position);
        spawned.push_back(std::move(monster));
    }
    return spawned;
}

std::unique_ptr<MonsterCharacter> MonsterSpawner::SpawnSingle(
    const std::string& templateId, const map::MapSpawnArea& area, const map::Map& map,
    std::mt19937& rng) const {
    const auto it = m_templates.find(templateId);
    if (it == m_templates.end()) {
        return nullptr; // 模板不存在/加载失败
    }
    const TemplateAssets& assets = it->second;
    math::Vector2 position{0.0f, 0.0f};
    if (!SpawnArea::FindWalkableSpawnPosition(map, assets.charDefinition.footprint, area, rng,
                                              position)) {
        LOG_WARN("MonsterSpawner: respawn find position failed for \x27" + templateId + "\x27.");
        return nullptr;
    }
    return std::make_unique<MonsterCharacter>(
        legend::entity::EntityIdAllocator::Next(), assets.definition, assets.charDefinition,
        assets.clips, assets.sheet, area.id, position);
}

const MonsterDefinition* MonsterSpawner::GetDefinition(const std::string& templateId) const {
    const auto it = m_templates.find(templateId);
    if (it == m_templates.end()) {
        return nullptr;
    }
    return &it->second.definition;
}

} // namespace legend::world
