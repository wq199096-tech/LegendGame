#include "Server/WorldServer/Item/InventoryRepository.h"

#include "Engine/Debug/Logger.h"

namespace legend::world {

using legend::account::Database;
using legend::account::Statement;

std::uint64_t InventoryRepository::InsertItem(Database& db, std::uint64_t characterId,
                                              std::uint32_t definitionId, std::uint32_t quantity,
                                              std::int64_t slotIndex, std::int64_t createdAt) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "INSERT INTO inventory_items "
                      "(character_id, item_definition_id, quantity, slot_index, created_at) "
                      "VALUES (?, ?, ?, ?, ?);",
                      error)) {
        LOG_ERROR("[Inventory] InsertItem prepare failed: " + error);
        return 0;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
    stmt.BindInt64(2, static_cast<std::int64_t>(definitionId));
    stmt.BindInt64(3, static_cast<std::int64_t>(quantity));
    stmt.BindInt64(4, slotIndex);
    stmt.BindInt64(5, createdAt);
    stmt.Step(error); // INSERT -> SQLITE_DONE（Step false + 无 error = 成功）
    if (!error.empty()) {
        LOG_ERROR("[Inventory] InsertItem step failed: " + error);
        return 0;
    }
    const std::int64_t rowId = db.LastInsertRowid();
    return rowId > 0 ? static_cast<std::uint64_t>(rowId) : 0;
}

bool InventoryRepository::UpdateQuantity(Database& db, std::uint64_t instanceId,
                                         std::uint32_t quantity) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(),
                      "UPDATE inventory_items SET quantity = ? WHERE instance_id = ?;", error)) {
        LOG_ERROR("[Inventory] UpdateQuantity prepare failed: " + error);
        return false;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(quantity));
    stmt.BindInt64(2, static_cast<std::int64_t>(instanceId));
    stmt.Step(error); // UPDATE -> SQLITE_DONE
    if (!error.empty()) {
        LOG_ERROR("[Inventory] UpdateQuantity step failed: " + error);
        return false;
    }
    return true;
}

bool InventoryRepository::DeleteItem(Database& db, std::uint64_t instanceId) {
    Statement stmt;
    std::string error;
    if (!stmt.Prepare(db.Handle(), "DELETE FROM inventory_items WHERE instance_id = ?;", error)) {
        LOG_ERROR("[Inventory] DeleteItem prepare failed: " + error);
        return false;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(instanceId));
    stmt.Step(error); // DELETE -> SQLITE_DONE
    if (!error.empty()) {
        LOG_ERROR("[Inventory] DeleteItem step failed: " + error);
        return false;
    }
    return true;
}

bool InventoryRepository::RunEquipTransaction(Database& db, const EquipTransaction& tx) {
    std::string error;
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    // 1) 新装备行 -> 装备槽码。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE inventory_items SET slot_index = ? WHERE instance_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, tx.equipmentSlotCode);
        stmt.BindInt64(2, static_cast<std::int64_t>(tx.newItemInstanceId));
        stmt.Step(error); // UPDATE/INSERT -> SQLITE_DONE
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    // 2) 被替换的旧装备 -> 释放出的背包槽（无替换则跳过）。
    if (tx.hadPrevious) {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE inventory_items SET slot_index = ? WHERE instance_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, tx.freedBagSlotIndex);
        stmt.BindInt64(2, static_cast<std::int64_t>(tx.previousInstanceId));
        stmt.Step(error); // UPDATE/INSERT -> SQLITE_DONE
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    // 3) character_equipment UPSERT。
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "INSERT INTO character_equipment "
                          "(character_id, equipment_slot, item_instance_id) VALUES (?, ?, ?) "
                          "ON CONFLICT(character_id, equipment_slot) "
                          "DO UPDATE SET item_instance_id = excluded.item_instance_id;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        // character_id 由调用方写入（EquipmentSlot 编码还原成 1/2 存储）。
        stmt.BindInt64(1, static_cast<std::int64_t>(tx.characterId));
        stmt.BindInt64(2, tx.equipmentSlotCode - kEquippedSlotBase);
        stmt.BindInt64(3, static_cast<std::int64_t>(tx.newItemInstanceId));
        stmt.Step(error); // UPDATE/INSERT -> SQLITE_DONE
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool InventoryRepository::RunUnequipTransaction(Database& db, const UnequipTransaction& tx) {
    std::string error;
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE inventory_items SET slot_index = ? WHERE instance_id = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, tx.bagSlotIndex);
        stmt.BindInt64(2, static_cast<std::int64_t>(tx.instanceId));
        stmt.Step(error); // UPDATE/INSERT -> SQLITE_DONE
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "DELETE FROM character_equipment "
                          "WHERE character_id = (SELECT character_id FROM inventory_items "
                          "WHERE instance_id = ?) AND equipment_slot = ?;",
                          error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, static_cast<std::int64_t>(tx.instanceId));
        stmt.BindInt64(2, tx.equipmentSlotCode - kEquippedSlotBase);
        stmt.Step(error); // UPDATE/INSERT -> SQLITE_DONE
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return true;
}

bool InventoryRepository::LoadInventory(Database& db, std::uint64_t characterId,
                                        std::vector<InventoryRow>& out, std::string& error) {
    out.clear();
    Statement stmt;
    if (!stmt.Prepare(db.Handle(),
                      "SELECT instance_id, item_definition_id, quantity, slot_index, created_at "
                      "FROM inventory_items WHERE character_id = ? ORDER BY instance_id;",
                      error)) {
        return false;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
    while (stmt.Step(error)) {
        InventoryRow row;
        row.instanceId = static_cast<std::uint64_t>(stmt.ColumnInt64(0));
        row.definitionId = static_cast<std::uint32_t>(stmt.ColumnInt64(1));
        row.quantity = static_cast<std::uint32_t>(stmt.ColumnInt64(2));
        row.slotIndex = stmt.ColumnInt64(3);
        row.createdAt = stmt.ColumnInt64(4);
        out.push_back(row);
    }
    return error.empty();
}

bool InventoryRepository::LoadEquipment(Database& db, std::uint64_t characterId,
                                        std::vector<EquipmentBinding>& out, std::string& error) {
    out.clear();
    Statement stmt;
    if (!stmt.Prepare(db.Handle(),
                      "SELECT equipment_slot, item_instance_id FROM character_equipment "
                      "WHERE character_id = ?;",
                      error)) {
        return false;
    }
    stmt.BindInt64(1, static_cast<std::int64_t>(characterId));
    while (stmt.Step(error)) {
        EquipmentBinding binding;
        binding.slot = stmt.ColumnInt64(0);
        binding.instanceId = static_cast<std::uint64_t>(stmt.ColumnInt64(1));
        out.push_back(binding);
    }
    return error.empty();
}

// ---------------------------------------------------------------------------
// 阶段25.5：商店/入包事务（DbServer RPC 与 World legacy 路径共用实现）。
// ---------------------------------------------------------------------------
std::uint64_t InventoryRepository::InsertOrMergeItem(Database& db, std::uint64_t characterId,
                                                     std::uint32_t definitionId,
                                                     std::uint32_t quantity,
                                                     std::int64_t slotIndex, std::int64_t createdAt,
                                                     bool mergedIntoStack,
                                                     std::uint64_t mergeInstanceId,
                                                     std::uint32_t mergeQuantity) {
    if (mergedIntoStack) {
        return mergeInstanceId != 0 && UpdateQuantity(db, mergeInstanceId, mergeQuantity)
                   ? mergeInstanceId
                   : 0;
    }
    return InsertItem(db, characterId, definitionId, quantity, slotIndex, createdAt);
}

bool InventoryRepository::RunShopBuyTransaction(Database& db, const ShopBuyTransaction& tx,
                                                std::uint64_t& outNewInstanceId,
                                                std::string& error) {
    outNewInstanceId = 0;
    // BEGIN -> Gold 扣除 -> Inventory 写入 -> COMMIT（指令四十六原子性）。
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE characters SET gold = ? WHERE id = ?;", error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, tx.newGold);
        stmt.BindInt64(2, static_cast<std::int64_t>(tx.characterId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    const std::uint64_t newInstanceId =
        InsertOrMergeItem(db, tx.characterId, tx.itemDefinitionId, tx.quantity, tx.bagSlotIndex,
                          legend::account::UnixNow(), tx.mergedIntoStack, tx.mergeInstanceId,
                          tx.mergeQuantity);
    if (newInstanceId == 0) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    outNewInstanceId = newInstanceId;
    return true;
}

bool InventoryRepository::RunShopSellTransaction(Database& db, const ShopSellTransaction& tx,
                                                 std::string& error) {
    // BEGIN -> Gold 增加 -> Inventory 扣除 -> COMMIT（指令五十四原子性）。
    if (!db.Execute("BEGIN IMMEDIATE;", error)) {
        return false;
    }
    {
        Statement stmt;
        if (!stmt.Prepare(db.Handle(),
                          "UPDATE characters SET gold = ? WHERE id = ?;", error)) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
        stmt.BindInt64(1, tx.newGold);
        stmt.BindInt64(2, static_cast<std::int64_t>(tx.characterId));
        stmt.Step(error);
        if (!error.empty()) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    {
        bool ok = false;
        if (tx.wholeStack) {
            ok = DeleteItem(db, tx.instanceId);
        } else {
            Statement stmt;
            ok = stmt.Prepare(db.Handle(),
                              "UPDATE inventory_items SET quantity = quantity - ? "
                              "WHERE instance_id = ?;",
                              error);
            if (ok) {
                stmt.BindInt64(1, static_cast<std::int64_t>(tx.quantity));
                stmt.BindInt64(2, static_cast<std::int64_t>(tx.instanceId));
                stmt.Step(error);
                ok = error.empty();
            }
        }
        if (!ok) {
            db.Execute("ROLLBACK;", error);
            return false;
        }
    }
    if (!db.Execute("COMMIT;", error)) {
        db.Execute("ROLLBACK;", error);
        return false;
    }
    return true;
}

} // namespace legend::world
