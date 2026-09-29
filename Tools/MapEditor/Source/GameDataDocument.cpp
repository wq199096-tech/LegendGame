#include "Tools/MapEditor/Source/GameDataDocument.h"

#include <algorithm>

using legend::world::LoadGameData;
using legend::world::MakeDefaultGameData;
using legend::world::MakeDefaultWorldData;
using legend::world::SaveGameData;
using legend::world::ValidateGameData;

namespace legend::editor {

// ---------------------------------------------------------------------------
// File 操作
// ---------------------------------------------------------------------------

bool GameDataDocument::Load(const std::string& gameDir, const std::string& worldDir,
                            std::string& error) {
    GameDataSet game;
    WorldDataSet world;
    if (!LoadGameData(gameDir, game, error)) {
        return false;
    }
    // World 数据只读加载（交叉引用校验源；失败不阻塞——校验时 NPC/Map 缺失会报错）。
    std::string worldError;
    if (!LoadWorldData(worldDir, world, worldError)) {
        world = MakeDefaultWorldData(); // fallback：出厂 World 数据参与交叉校验
    }
    if (!ValidateGameData(game, world, error)) {
        error = "game data validation failed: " + error;
        return false;
    }
    m_data = std::move(game);
    m_world = std::move(world);
    m_dir = gameDir;
    m_worldDir = worldDir;
    m_undo.clear();
    m_redo.clear();
    m_selection = {};
    m_dirty = false;
    Revalidate();
    return true;
}

bool GameDataDocument::Save(std::string& error) {
    if (HasErrors()) {
        error = "cannot save: validation errors present"; // 22.13 同规则
        return false;
    }
    if (!SaveGameData(m_dir, m_data, error)) {
        return false;
    }
    m_dirty = false;
    m_undo.clear();
    m_redo.clear();
    return true;
}

void GameDataDocument::NewFromDefaults() {
    m_data = MakeDefaultGameData();
    m_world = MakeDefaultWorldData();
    m_dir.clear();
    m_undo.clear();
    m_redo.clear();
    m_selection = {};
    m_dirty = true;
    Revalidate();
}

bool GameDataDocument::ReloadFromDisk(std::string& error) {
    // 23.15：Dev-only Reload。若存在未保存修改，调用方（UI）应先确认；
    // Definition 修改可能破坏运行时状态 → UI 提示 Server Restart（不自动推送服务器）。
    const std::string dir = m_dir;
    const std::string worldDir = m_worldDir;
    if (dir.empty()) {
        error = "no directory to reload from";
        return false;
    }
    const bool wasDirty = m_dirty;
    if (!Load(dir, worldDir, error)) {
        return false;
    }
    if (wasDirty) {
        error = "reloaded from disk (discarded unsaved edits); restart WorldServer to apply";
    }
    return true;
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

bool GameDataDocument::Undo() {
    if (m_undo.empty()) {
        return false;
    }
    m_redo.push_back(m_data);
    m_data = std::move(m_undo.back());
    m_undo.pop_back();
    m_dirty = true;
    Revalidate();
    return true;
}

bool GameDataDocument::Redo() {
    if (m_redo.empty()) {
        return false;
    }
    m_undo.push_back(m_data);
    m_data = std::move(m_redo.back());
    m_redo.pop_back();
    m_dirty = true;
    Revalidate();
    return true;
}

// ---------------------------------------------------------------------------
// 选中对象操作
// ---------------------------------------------------------------------------

bool GameDataDocument::RemoveSelected() {
    const Selection sel = m_selection;
    if (sel.type == ObjectType::None) {
        return false;
    }
    bool removed = false;
    Mutate([&](GameDataSet& d) {
        switch (sel.type) {
            case ObjectType::Item:
                removed = std::erase_if(d.items, [&](const ItemDefinition& v) {
                              return v.definitionId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Monster:
                removed = std::erase_if(d.monsters, [&](const MonsterDefinition& v) {
                              return v.monsterTypeId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Skill:
                removed = std::erase_if(d.skills, [&](const SkillDefinition& v) {
                              return v.skillId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Status:
                removed = std::erase_if(d.statuses, [&](const StatusEffectDefinition& v) {
                              return v.effectId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Quest:
                removed = std::erase_if(d.quests, [&](const QuestDefinition& v) {
                              return v.questId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Shop:
                removed = std::erase_if(d.shops, [&](const ShopDefinition& v) {
                              return v.shopId == sel.id;
                          }) > 0;
                break;
            case ObjectType::Teleport:
                removed = std::erase_if(d.teleports, [&](const TeleportDefinition& v) {
                              return v.teleportId == sel.id;
                          }) > 0;
                break;
            case ObjectType::LootTable:
                removed = std::erase_if(d.lootTables, [&](const LootTableDefinition& v) {
                              return v.lootTableId == sel.id;
                          }) > 0;
                break;
            case ObjectType::None:
                break;
        }
    });
    if (removed) {
        m_selection = {};
    }
    return removed;
}

bool GameDataDocument::DuplicateSelected() {
    const Selection sel = m_selection;
    if (sel.type == ObjectType::None) {
        return false;
    }
    bool duplicated = false;
    Mutate([&](GameDataSet& d) {
        switch (sel.type) {
            case ObjectType::Item: {
                const auto* src = FindItem(sel.id);
                if (src != nullptr) {
                    ItemDefinition copy = *src;
                    copy.definitionId = SuggestItemId();
                    copy.name += " Copy";
                    d.items.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Monster: {
                const auto* src = FindMonster(sel.id);
                if (src != nullptr) {
                    MonsterDefinition copy = *src;
                    copy.monsterTypeId = SuggestMonsterId();
                    copy.name += " Copy";
                    d.monsters.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Skill: {
                const auto* src = FindSkill(sel.id);
                if (src != nullptr) {
                    SkillDefinition copy = *src;
                    copy.skillId = SuggestSkillId();
                    copy.name += " Copy";
                    d.skills.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Status: {
                const auto* src = FindStatus(sel.id);
                if (src != nullptr) {
                    StatusEffectDefinition copy = *src;
                    copy.effectId = SuggestStatusId();
                    copy.name += " Copy";
                    d.statuses.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Quest: {
                const auto* src = FindQuest(sel.id);
                if (src != nullptr) {
                    QuestDefinition copy = *src;
                    copy.questId = SuggestQuestId();
                    copy.name += " Copy";
                    for (auto& objective : copy.objectives) {
                        objective.objectiveId = objective.objectiveId % 100000 + 600000;
                    }
                    d.quests.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Shop: {
                const auto* src = FindShop(sel.id);
                if (src != nullptr) {
                    ShopDefinition copy = *src;
                    copy.shopId = SuggestShopId();
                    copy.name += " Copy";
                    d.shops.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Teleport: {
                const auto* src = FindTeleport(sel.id);
                if (src != nullptr) {
                    TeleportDefinition copy = *src;
                    copy.teleportId = SuggestTeleportId();
                    copy.name += " Copy";
                    d.teleports.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::LootTable: {
                const auto* src = FindLootTable(sel.id);
                if (src != nullptr) {
                    LootTableDefinition copy = *src;
                    copy.lootTableId = SuggestLootTableId();
                    copy.name += " Copy";
                    d.lootTables.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::None:
                break;
        }
    });
    return duplicated;
}

// ---------------------------------------------------------------------------
// Search（23.12：ID/Name 匹配）
// ---------------------------------------------------------------------------

bool GameDataDocument::MatchesSearch(ObjectType type, std::uint32_t id, const std::string& name,
                                     const std::string& search) const {
    if (search.empty()) {
        return true;
    }
    // 23.12：大小写不敏感匹配（ID 或 Name 子串）。
    const auto containsCI = [](const std::string& text, const std::string& needle) {
        if (needle.empty()) {
            return true;
        }
        const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                                    [](char a, char b) {
                                        return std::tolower(static_cast<unsigned char>(a)) ==
                                               std::tolower(static_cast<unsigned char>(b));
                                    });
        return it != text.end();
    };
    if (containsCI(std::to_string(id), search)) {
        return true;
    }
    return containsCI(name, search);
}

// ---------------------------------------------------------------------------
// ID 建议（23.13/23.14：max+1，分类基数对齐阶段约定）
// ---------------------------------------------------------------------------

namespace {
template <typename Container, typename GetId>
std::uint32_t SuggestFrom(const Container& container, GetId getId, std::uint32_t base) {
    std::uint32_t maxId = base;
    for (const auto& element : container) {
        maxId = std::max(maxId, getId(element));
    }
    return maxId + 1;
}
} // namespace

std::uint32_t GameDataDocument::SuggestItemId() const {
    return SuggestFrom(m_data.items, [](const ItemDefinition& v) { return v.definitionId; },
                       3000);
}

std::uint32_t GameDataDocument::SuggestMonsterId() const {
    return SuggestFrom(m_data.monsters,
                       [](const MonsterDefinition& v) { return v.monsterTypeId; }, 1);
}

std::uint32_t GameDataDocument::SuggestSkillId() const {
    return SuggestFrom(m_data.skills, [](const SkillDefinition& v) { return v.skillId; }, 1000);
}

std::uint32_t GameDataDocument::SuggestStatusId() const {
    return SuggestFrom(m_data.statuses,
                       [](const StatusEffectDefinition& v) { return v.effectId; }, 2000);
}

std::uint32_t GameDataDocument::SuggestQuestId() const {
    return SuggestFrom(m_data.quests, [](const QuestDefinition& v) { return v.questId; }, 4000);
}

std::uint32_t GameDataDocument::SuggestShopId() const {
    return SuggestFrom(m_data.shops, [](const ShopDefinition& v) { return v.shopId; }, 6000);
}

std::uint32_t GameDataDocument::SuggestTeleportId() const {
    return SuggestFrom(m_data.teleports,
                       [](const TeleportDefinition& v) { return v.teleportId; }, 7000);
}

std::uint32_t GameDataDocument::SuggestLootTableId() const {
    return SuggestFrom(m_data.lootTables,
                       [](const LootTableDefinition& v) { return v.lootTableId; }, 0);
}

// ---------------------------------------------------------------------------
// 查找
// ---------------------------------------------------------------------------

const ItemDefinition* GameDataDocument::FindItem(std::uint32_t id) const {
    for (const auto& v : m_data.items) {
        if (v.definitionId == id) {
            return &v;
        }
    }
    return nullptr;
}

const MonsterDefinition* GameDataDocument::FindMonster(std::uint32_t id) const {
    for (const auto& v : m_data.monsters) {
        if (v.monsterTypeId == id) {
            return &v;
        }
    }
    return nullptr;
}

const SkillDefinition* GameDataDocument::FindSkill(std::uint32_t id) const {
    for (const auto& v : m_data.skills) {
        if (v.skillId == id) {
            return &v;
        }
    }
    return nullptr;
}

const StatusEffectDefinition* GameDataDocument::FindStatus(std::uint32_t id) const {
    for (const auto& v : m_data.statuses) {
        if (v.effectId == id) {
            return &v;
        }
    }
    return nullptr;
}

const QuestDefinition* GameDataDocument::FindQuest(std::uint32_t id) const {
    for (const auto& v : m_data.quests) {
        if (v.questId == id) {
            return &v;
        }
    }
    return nullptr;
}

const ShopDefinition* GameDataDocument::FindShop(std::uint32_t id) const {
    for (const auto& v : m_data.shops) {
        if (v.shopId == id) {
            return &v;
        }
    }
    return nullptr;
}

const TeleportDefinition* GameDataDocument::FindTeleport(std::uint32_t id) const {
    for (const auto& v : m_data.teleports) {
        if (v.teleportId == id) {
            return &v;
        }
    }
    return nullptr;
}

const LootTableDefinition* GameDataDocument::FindLootTable(std::uint32_t id) const {
    for (const auto& v : m_data.lootTables) {
        if (v.lootTableId == id) {
            return &v;
        }
    }
    return nullptr;
}

void GameDataDocument::Revalidate() {
    std::string error;
    m_errors.clear();
    if (!ValidateGameData(m_data, m_world, error)) {
        m_errors.push_back(error);
    }
}

} // namespace legend::editor
