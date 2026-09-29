#include "Tools/MapEditor/Source/WorldDocument.h"

#include <algorithm>

using legend::world::LoadWorldData;
using legend::world::MakeDefaultWorldData;
using legend::world::SaveWorldData;
using legend::world::ValidateWorldData;

namespace legend::editor {

// ---------------------------------------------------------------------------
// File 操作
// ---------------------------------------------------------------------------

bool WorldDocument::Load(const std::string& dir, std::string& error) {
    WorldDataSet loaded;
    if (!LoadWorldData(dir, loaded, error)) {
        return false;
    }
    if (!ValidateWorldData(loaded, error)) {
        error = "world data validation failed: " + error;
        return false;
    }
    m_data = std::move(loaded);
    m_dir = dir;
    m_undo.clear();
    m_redo.clear();
    m_selection = {};
    m_dirty = false;
    Revalidate();
    return true;
}

bool WorldDocument::Save(std::string& error) {
    if (HasErrors()) {
        error = "cannot save: validation errors present"; // 22.13：Error 禁存
        return false;
    }
    if (!SaveWorldData(m_dir, m_data, error)) {
        return false;
    }
    m_dirty = false;
    m_undo.clear();
    m_redo.clear();
    return true;
}

void WorldDocument::NewFromDefaults() {
    m_data = MakeDefaultWorldData();
    m_dir.clear(); // Save As 决定目标目录
    m_undo.clear();
    m_redo.clear();
    m_selection = {};
    m_dirty = true;
    Revalidate();
}

// ---------------------------------------------------------------------------
// Undo / Redo（22.4：快照式，100 步）
// ---------------------------------------------------------------------------

bool WorldDocument::Undo() {
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

bool WorldDocument::Redo() {
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

void WorldDocument::SetSelection(ObjectType type, std::uint32_t id) {
    m_selection.type = type;
    m_selection.id = id;
}

bool WorldDocument::RemoveSelected() {
    const Selection sel = m_selection;
    if (sel.type == ObjectType::None) {
        return false;
    }
    bool removed = false;
    Mutate([&](WorldDataSet& data) {
        switch (sel.type) {
            case ObjectType::Map:
                removed = std::erase_if(data.maps, [&](const MapDefinition& m) {
                             return m.mapId == static_cast<std::uint16_t>(sel.id);
                         }) > 0;
                break;
            case ObjectType::Npc: {
                removed = std::erase_if(data.npcs, [&](const NpcDefinition& n) {
                             return n.npcDefinitionId == sel.id;
                         }) > 0;
                if (removed) {
                    // 对话内嵌于 NPC（dialogueId = npcDefinitionId）→ 一并移除。
                    std::erase_if(data.dialogues, [&](const DialogueDefinition& d) {
                        return d.dialogueId == sel.id;
                    });
                }
                break;
            }
            case ObjectType::Spawn:
                removed = std::erase_if(data.monsterSpawns,
                                        [&](const MonsterSpawnDefinition& s) {
                                            return s.spawnId == sel.id;
                                        }) > 0;
                break;
            case ObjectType::Portal:
                removed = std::erase_if(data.portals, [&](const PortalDefinition& p) {
                             return p.portalId == sel.id;
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

bool WorldDocument::DuplicateSelected() {
    const Selection sel = m_selection;
    if (sel.type == ObjectType::None) {
        return false;
    }
    bool duplicated = false;
    Mutate([&](WorldDataSet& data) {
        switch (sel.type) {
            case ObjectType::Map: {
                const auto* src = FindMap(static_cast<std::uint16_t>(sel.id));
                if (src != nullptr) {
                    MapDefinition copy = *src;
                    copy.mapId = SuggestMapId();
                    copy.name += " Copy";
                    data.maps.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Npc: {
                const auto* src = FindNpc(sel.id);
                if (src != nullptr) {
                    NpcDefinition copy = *src;
                    copy.npcDefinitionId = SuggestNpcId();
                    copy.name += " Copy";
                    copy.spawnX += 30.0f; // 错开放置，避免完全重叠
                    data.npcs.push_back(copy);
                    const auto* dialogue = [&]() -> const DialogueDefinition* {
                        for (const auto& d : data.dialogues) {
                            if (d.dialogueId == sel.id) {
                                return &d;
                            }
                        }
                        return nullptr;
                    }();
                    if (dialogue != nullptr) {
                        data.dialogues.push_back({copy.npcDefinitionId, dialogue->title,
                                                  dialogue->text});
                    }
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Spawn: {
                const auto* src = FindSpawn(sel.id);
                if (src != nullptr) {
                    MonsterSpawnDefinition copy = *src;
                    copy.spawnId = SuggestSpawnId();
                    copy.centerX += 60.0f;
                    data.monsterSpawns.push_back(copy);
                    duplicated = true;
                }
                break;
            }
            case ObjectType::Portal: {
                const auto* src = FindPortal(sel.id);
                if (src != nullptr) {
                    PortalDefinition copy = *src;
                    copy.portalId = SuggestPortalId();
                    copy.x += 60.0f;
                    data.portals.push_back(copy);
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
// ID 建议（max+1；不产生重复 ID，22.13）
// ---------------------------------------------------------------------------

std::uint16_t WorldDocument::SuggestMapId() const {
    std::uint16_t maxId = 0;
    for (const auto& map : m_data.maps) {
        maxId = std::max(maxId, map.mapId);
    }
    return static_cast<std::uint16_t>(maxId + 1);
}

std::uint32_t WorldDocument::SuggestNpcId() const {
    std::uint32_t maxId = 5000; // 阶段约定基数：5001~5004
    for (const auto& npc : m_data.npcs) {
        maxId = std::max(maxId, npc.npcDefinitionId);
    }
    return maxId + 1;
}

std::uint32_t WorldDocument::SuggestSpawnId() const {
    std::uint32_t maxId = 2000; // 迁移数据 2001(map2)/3001(map3)
    for (const auto& spawn : m_data.monsterSpawns) {
        maxId = std::max(maxId, spawn.spawnId);
    }
    return maxId + 1;
}

std::uint32_t WorldDocument::SuggestPortalId() const {
    std::uint32_t maxId = 8000; // 阶段约定基数：8001~8004
    for (const auto& portal : m_data.portals) {
        maxId = std::max(maxId, portal.portalId);
    }
    return maxId + 1;
}

// ---------------------------------------------------------------------------
// 查找
// ---------------------------------------------------------------------------

const MapDefinition* WorldDocument::FindMap(std::uint16_t mapId) const {
    for (const auto& map : m_data.maps) {
        if (map.mapId == mapId) {
            return &map;
        }
    }
    return nullptr;
}

const NpcDefinition* WorldDocument::FindNpc(std::uint32_t npcId) const {
    for (const auto& npc : m_data.npcs) {
        if (npc.npcDefinitionId == npcId) {
            return &npc;
        }
    }
    return nullptr;
}

const MonsterSpawnDefinition* WorldDocument::FindSpawn(std::uint32_t spawnId) const {
    for (const auto& spawn : m_data.monsterSpawns) {
        if (spawn.spawnId == spawnId) {
            return &spawn;
        }
    }
    return nullptr;
}

const PortalDefinition* WorldDocument::FindPortal(std::uint32_t portalId) const {
    for (const auto& portal : m_data.portals) {
        if (portal.portalId == portalId) {
            return &portal;
        }
    }
    return nullptr;
}

void WorldDocument::Revalidate() {
    std::string error;
    m_errors.clear();
    if (!ValidateWorldData(m_data, error)) {
        m_errors.push_back(error);
    }
}

} // namespace legend::editor
