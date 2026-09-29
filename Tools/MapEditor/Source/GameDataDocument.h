#pragma once

#include "Shared/GameData/GameDataJson.h"
#include "Shared/WorldData/WorldDataJson.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace legend::editor {

using legend::world::GameDataSet;
using legend::world::ItemDefinition;
using legend::world::LootTableDefinition;
using legend::world::MonsterDefinition;
using legend::world::QuestDefinition;
using legend::world::ShopDefinition;
using legend::world::SkillDefinition;
using legend::world::StatusEffectDefinition;
using legend::world::TeleportDefinition;
using legend::world::WorldDataSet;

// ---------------------------------------------------------------------------
// 阶段23：GameDataDocument —— Data Editor 文档模型（UI 无关，可单测）。
// 持有 GameDataSet + WorldDataSet（交叉引用校验 23.11 需要 NPC/Map/Spawn）+
// Undo/Redo（快照式 100 步）+ Dirty + 实时 Validation + Search/Sort（23.12）+
// Duplicate 建议 ID（23.13）+ 分类 ID 唯一（23.14）+ Reload From Disk（23.15）。
// 所有修改必须走 Mutate()。
// ---------------------------------------------------------------------------
class GameDataDocument {
public:
    enum class ObjectType : std::uint8_t {
        None = 0,
        Item = 1,
        Monster = 2,
        Skill = 3,
        Status = 4,
        Quest = 5,
        Shop = 6,
        Teleport = 7,
        LootTable = 8,
    };

    struct Selection {
        ObjectType type = ObjectType::None;
        std::uint32_t id = 0;
    };

    // ---- File ----
    bool Load(const std::string& gameDir, const std::string& worldDir, std::string& error);
    bool Save(std::string& error);
    void NewFromDefaults(); // 以 MakeDefaultGameData + MakeDefaultWorldData 为起点

    // 23.15：Dev-only Reload From Disk（本地开发模式）。
    bool ReloadFromDisk(std::string& error);

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

    // ---- 选中对象操作（23.13：Duplicate 自动建议新 ID，不产生重复）----
    bool RemoveSelected();
    bool DuplicateSelected();

    // ---- 只读访问 ----
    const GameDataSet& Data() const { return m_data; }
    const WorldDataSet& WorldData() const { return m_world; }
    const Selection& GetSelection() const { return m_selection; }
    void SetSelection(ObjectType type, std::uint32_t id) {
        m_selection.type = type;
        m_selection.id = id;
    }
    void ClearSelection() { m_selection = {}; }

    const std::vector<std::string>& ValidationErrors() const { return m_errors; }
    bool HasErrors() const { return !m_errors.empty(); }
    bool IsDirty() const { return m_dirty; }
    const std::string& Directory() const { return m_dir; }
    void SetDirectory(const std::string& dir) { m_dir = dir; }

    // 23.12：Search——按 ID/Name 过滤（type 内列出时用）。
    bool MatchesSearch(ObjectType type, std::uint32_t id, const std::string& name,
                       const std::string& search) const;

    // 23.14：分类 ID 唯一 + 建议（max+1，基数对齐阶段约定）。
    std::uint32_t SuggestItemId() const;
    std::uint32_t SuggestMonsterId() const;
    std::uint32_t SuggestSkillId() const;
    std::uint32_t SuggestStatusId() const;
    std::uint32_t SuggestQuestId() const;
    std::uint32_t SuggestShopId() const;
    std::uint32_t SuggestTeleportId() const;
    std::uint32_t SuggestLootTableId() const;

    // ---- 查找 ----
    const ItemDefinition* FindItem(std::uint32_t id) const;
    const MonsterDefinition* FindMonster(std::uint32_t id) const;
    const SkillDefinition* FindSkill(std::uint32_t id) const;
    const StatusEffectDefinition* FindStatus(std::uint32_t id) const;
    const QuestDefinition* FindQuest(std::uint32_t id) const;
    const ShopDefinition* FindShop(std::uint32_t id) const;
    const TeleportDefinition* FindTeleport(std::uint32_t id) const;
    const LootTableDefinition* FindLootTable(std::uint32_t id) const;

    static constexpr std::size_t kMaxUndoSteps = 100;

private:
    void Revalidate();

    GameDataSet m_data;
    WorldDataSet m_world; // 交叉引用源（只读；编辑 World 走 WorldDocument）
    std::vector<GameDataSet> m_undo;
    std::vector<GameDataSet> m_redo;
    Selection m_selection;
    std::vector<std::string> m_errors;
    std::string m_dir;
    std::string m_worldDir;
    bool m_dirty = false;
};

} // namespace legend::editor
