#pragma once

// ---------------------------------------------------------------------------
// 阶段25：客户端物品展示目录（Display-Only）。
// 只加载 Data/Game/items.json 的【展示字段】（名称/类型/图标/攻防加成/槽位），
// 一切服务器权威数值（实际攻防结算/掉落/购买结果）仍来自服务器事件。
// 与 VisualDataCatalog 同纪律；缺失文件不致命（fallback 图标/空名）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <string>

namespace legend::ui {

struct ItemDisplay {
    std::uint32_t definitionId = 0;
    std::string name;
    std::string type;      // Weapon / Armor / Material
    std::string equipSlot; // Weapon / Armor / None
    std::string iconKey;   // asset_manifest assetId（item_*）
    std::uint32_t attackBonus = 0;
    std::uint32_t defenseBonus = 0;
    std::uint32_t maxStack = 1;
    bool isEquipment() const { return type == "Weapon" || type == "Armor"; }
};

class ItemDisplayCatalog {
public:
    // dataRoot = 含 Game/ 子目录的 Data 目录；items.json 缺失/非法 -> false + error
    //（调用方决定是否降级为空目录）。
    bool Load(const std::string& dataRoot, std::string& error);

    const ItemDisplay* Find(std::uint32_t definitionId) const;
    std::string IconAsset(std::uint32_t definitionId) const; // 无记录返回 ""
    std::string DisplayName(std::uint32_t definitionId) const; // 无记录返回 ""
    std::size_t Count() const { return m_items.size(); }
    void Clear() { m_items.clear(); }

private:
    std::map<std::uint32_t, ItemDisplay> m_items;
};

} // namespace legend::ui
