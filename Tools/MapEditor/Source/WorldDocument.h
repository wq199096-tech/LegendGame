#pragma once

#include "Shared/WorldData/WorldDataJson.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace legend::editor {

using legend::world::DialogueDefinition;
using legend::world::MapDefinition;
using legend::world::MonsterSpawnDefinition;
using legend::world::NpcDefinition;
using legend::world::PortalDefinition;
using legend::world::WorldDataSet;

// ---------------------------------------------------------------------------
// 阶段22 22.4~22.16：WorldDocument —— World Editor 文档模型（UI 无关，可单测）。
// 持有 WorldDataSet + Undo/Redo（快照式 100 步）+ Dirty + 实时 Validation。
// 所有修改必须走 Mutate()（自动压 undo 栈 + 重校验），绕过则 Undo/校验失效。
// ---------------------------------------------------------------------------
class WorldDocument {
public:
    enum class ObjectType : std::uint8_t {
        None = 0,
        Map = 1,
        Npc = 2,
        Spawn = 3,
        Portal = 4,
    };

    struct Selection {
        ObjectType type = ObjectType::None;
        std::uint32_t id = 0;
    };

    // ---- File ----
    bool Load(const std::string& dir, std::string& error);
    bool Save(std::string& error);
    void NewFromDefaults(); // File > New World（以 22.18 迁移数据为起点）

    // ---- 修改入口（唯一合法通道）----
    template <typename F>
    void Mutate(F&& op) {
        m_redo.clear();
        m_undo.push_back(m_data);
        if (m_undo.size() > kMaxUndoSteps) {
            m_undo.erase(m_undo.begin());
        }
        op(m_data);
        m_dirty = true;
        Revalidate();
    }

    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }

    // ---- 选中对象操作（22.4：Delete / Ctrl+D Duplicate）----
    bool RemoveSelected();
    bool DuplicateSelected(); // 自动分配新的可用 ID（22.13：不产生重复 ID）

    // ---- 只读访问 ----
    const WorldDataSet& Data() const { return m_data; }
    const Selection& GetSelection() const { return m_selection; }
    void SetSelection(ObjectType type, std::uint32_t id);
    void ClearSelection() { m_selection = {}; }

    const std::vector<std::string>& ValidationErrors() const { return m_errors; }
    bool HasErrors() const { return !m_errors.empty(); }
    bool IsDirty() const { return m_dirty; }
    const std::string& Directory() const { return m_dir; }
    void SetDirectory(const std::string& dir) { m_dir = dir; } // Save As 重定向

    // ---- ID 分配建议（max+1；分类基数对齐阶段约定）----
    std::uint16_t SuggestMapId() const;
    std::uint32_t SuggestNpcId() const;
    std::uint32_t SuggestSpawnId() const;
    std::uint32_t SuggestPortalId() const;

    // ---- 查找（编辑器绘制/Inspector 用）----
    const MapDefinition* FindMap(std::uint16_t mapId) const;
    const NpcDefinition* FindNpc(std::uint32_t npcId) const;
    const MonsterSpawnDefinition* FindSpawn(std::uint32_t spawnId) const;
    const PortalDefinition* FindPortal(std::uint32_t portalId) const;

    static constexpr std::size_t kMaxUndoSteps = 100; // 22.4：至少 100 步

private:
    void Revalidate();

    WorldDataSet m_data;
    std::vector<WorldDataSet> m_undo;
    std::vector<WorldDataSet> m_redo;
    Selection m_selection;
    std::vector<std::string> m_errors;
    std::string m_dir;
    bool m_dirty = false;
};

} // namespace legend::editor
