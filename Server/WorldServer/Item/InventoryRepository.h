#pragma once

#include "Server/LoginServer/Account/Database/Database.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// 兄弟命名空间：legend::world 内使用 legend::account::Database 必须限定
//（unqualified lookup 不进入兄弟命名空间）。
using AccountDatabase = legend::account::Database;

// ---------------------------------------------------------------------------
// 阶段18 指令八/九/五十四/五十五：InventoryRepository —— 背包/装备持久化。
// 只能在 DB Worker 线程调用（指令五十四：禁止 World io 线程同步 SQLite）。
// 装备中的物品保留 inventory_items 行，slot_index = kEquippedSlotBase + slot；
// Equip/Unequip/Pickup 一律事务化（指令五十五）。
// ---------------------------------------------------------------------------
class InventoryRepository {
public:
    // 装备槽在 inventory_items.slot_index 中的编码（与 EquipmentSlot 枚举对应）。
    static constexpr std::int64_t kEquippedSlotBase = 1000;
    static constexpr std::int64_t kWeaponSlotCode = kEquippedSlotBase + 1; // 1001
    static constexpr std::int64_t kArmorSlotCode = kEquippedSlotBase + 2;  // 1002

    struct InventoryRow {
        std::uint64_t instanceId = 0;
        std::uint32_t definitionId = 0;
        std::uint32_t quantity = 0;
        std::int64_t slotIndex = 0; // 0~39 = 背包；1001/1002 = 装备中
        std::int64_t createdAt = 0;
    };

    struct EquipmentBinding {
        std::int64_t slot = 0; // 1=Weapon 2=Armor
        std::uint64_t instanceId = 0;
    };

    // 拾取/合并后的堆叠写入（新 instance 返回持久 instanceId；MERGE 时用
    // UpdateQuantity）。任一失败返回 0。
    static std::uint64_t InsertItem(AccountDatabase& db, std::uint64_t characterId,
                                    std::uint32_t definitionId, std::uint32_t quantity,
                                    std::int64_t slotIndex, std::int64_t createdAt);
    // 并入已有堆叠（覆盖 quantity）。
    static bool UpdateQuantity(AccountDatabase& db, std::uint64_t instanceId, std::uint32_t quantity);
    // 删除实例（装备替换不删除——旧装备回背包；仅测试清理用）。
    static bool DeleteItem(AccountDatabase& db, std::uint64_t instanceId);

    // 阶段25.5：拾取/入包统一入口（Insert 新堆叠 或 Merge 覆盖数量）。
    // 返回持久 instanceId（Merge 返回 mergeInstanceId；失败返回 0）。
    static std::uint64_t InsertOrMergeItem(AccountDatabase& db, std::uint64_t characterId,
                                           std::uint32_t definitionId, std::uint32_t quantity,
                                           std::int64_t slotIndex, std::int64_t createdAt,
                                           bool mergedIntoStack, std::uint64_t mergeInstanceId,
                                           std::uint32_t mergeQuantity);

    // 阶段25.5：商店原子事务（SQL 从 WorldServer DbWorker 任务抽入——DbServer RPC
    // 与 legacy 路径共用同一实现，指令四十六/五十四：Gold 与 Inventory 一次提交）。
    struct ShopBuyTransaction {
        std::uint64_t characterId = 0;
        std::int64_t newGold = 0;
        std::uint32_t itemDefinitionId = 0;
        std::uint32_t quantity = 0;
        std::int64_t bagSlotIndex = 0;
        bool mergedIntoStack = false;
        std::uint64_t mergeInstanceId = 0;
        std::uint32_t mergeQuantity = 0;
    };
    static bool RunShopBuyTransaction(AccountDatabase& db, const ShopBuyTransaction& tx,
                                      std::uint64_t& outNewInstanceId, std::string& error);
    struct ShopSellTransaction {
        std::uint64_t characterId = 0;
        std::int64_t newGold = 0;
        std::uint64_t instanceId = 0;
        std::uint32_t quantity = 0;
        bool wholeStack = false;
    };
    static bool RunShopSellTransaction(AccountDatabase& db, const ShopSellTransaction& tx,
                                       std::string& error);

    // Equip 事务：新装备 slot_index -> 装备码；被替换旧装备 -> 释放出的背包槽；
    // character_equipment UPSERT。全部成功才提交（指令三十三/五十五）。
    struct EquipTransaction {
        std::uint64_t characterId = 0;
        std::uint64_t newItemInstanceId = 0;
        std::int64_t equipmentSlotCode = 0; // 1001/1002
        bool hadPrevious = false;
        std::uint64_t previousInstanceId = 0;
        std::int64_t freedBagSlotIndex = 0;
    };
    static bool RunEquipTransaction(AccountDatabase& db, const EquipTransaction& tx);

    // Unequip 事务：装备码 -> 背包槽；删除 character_equipment 绑定。
    struct UnequipTransaction {
        std::uint64_t instanceId = 0;
        std::int64_t equipmentSlotCode = 0;
        std::int64_t bagSlotIndex = 0;
    };
    static bool RunUnequipTransaction(AccountDatabase& db, const UnequipTransaction& tx);

    // 进入世界加载（指令二十八）。
    static bool LoadInventory(AccountDatabase& db, std::uint64_t characterId,
                              std::vector<InventoryRow>& out, std::string& error);
    static bool LoadEquipment(AccountDatabase& db, std::uint64_t characterId,
                              std::vector<EquipmentBinding>& out, std::string& error);
};

} // namespace legend::world

