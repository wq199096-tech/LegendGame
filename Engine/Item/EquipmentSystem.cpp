#include "Engine/Item/EquipmentSystem.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Item/ItemDatabase.h"

namespace legend::item {

namespace {
// 失败时把已取出的新装备放回背包（尽力而为：背包此刻必有刚腾出的空格）
void RollbackToInventory(Inventory& inventory, const ItemInstance& instance) {
    (void)inventory.AddInstance(instance);
}
} // namespace

EquipmentOpResult EquipmentSystem::Equip(const ItemDatabase& items, Inventory& inventory,
                                         EquipmentComponent& equipment,
                                         ItemInstanceId instanceId) {
    // 1) 从背包取出实例（instanceId 保持；不存在 -> 失败，状态不变）
    auto taken = inventory.TakeInstance(instanceId);
    if (!taken.has_value()) {
        return {false, "instance not found"};
    }
    // 2) 阶段7.1 全局唯一性保护：装备栏已持有同 instanceId -> 拒绝（防损坏数据）
    if (equipment.ContainsInstanceId(taken->instanceId)) {
        RollbackToInventory(inventory, *taken);
        return {false, "instance already equipped"};
    }
    // 3) 严格类型校验（阶段7.1 指令三：五重全部成立，否则回滚）
    const ItemDefinition* definition = items.Get(taken->definitionId);
    if (definition == nullptr || !definition->IsValid() || !definition->IsEquipmentValid() ||
        !definition->hasEquipment || definition->type != ItemType::Equipment ||
        taken->quantity != 1) {
        RollbackToInventory(inventory, *taken);
        return {false, "invalid definition"};
    }
    const EquipmentSlotType slot = definition->equipment.slot;

    // 4) 事务式 Swap：先取出槽内旧装备（槽腾空），再把旧装备放回背包
    //    （新装备刚从背包取出，背包必有空格可接收旧装备——FullInventorySwapCheck 边界：
    //    B 腾出的原位置正好接收 A，背包满时 Swap 依然成功）
    auto oldEquipped = equipment.TakeEquipped(slot);
    if (oldEquipped.has_value()) {
        const ItemDefinition* oldDefinition = items.Get(oldEquipped->definitionId);
        if (!inventory.AddInstance(*oldEquipped, oldDefinition)) {
            // 背包满且放不回旧装备：完全回滚（旧装备回槽、新装备回背包，不丢任何一件）
            if (!equipment.TrySetEquipped(slot, *oldEquipped)) {
                LOG_ERROR("EquipmentSystem: swap rollback failed, slot occupied unexpectedly.");
            }
            RollbackToInventory(inventory, *taken);
            return {false, "inventory full"};
        }
    }

    // 5) 新装备入槽（防覆盖：此时槽必为空，TrySet 失败即内部状态异常 -> 回滚）
    if (!equipment.TrySetEquipped(slot, *taken)) {
        LOG_ERROR("EquipmentSystem: slot unexpectedly occupied on final equip.");
        if (oldEquipped.has_value()) {
            RollbackToInventory(inventory, *oldEquipped);
        } else {
            RollbackToInventory(inventory, *taken);
        }
        return {false, "slot occupied"};
    }
    return {true, "ok"};
}

EquipmentOpResult EquipmentSystem::Unequip(const ItemDatabase& items, Inventory& inventory,
                                           EquipmentComponent& equipment,
                                           EquipmentSlotType slot) {
    // 1) 取出槽内装备（空槽 -> 失败）
    auto equipped = equipment.TakeEquipped(slot);
    if (!equipped.has_value()) {
        return {false, "slot empty"};
    }
    // 2) 放回背包；背包满 -> 装备放回槽位（不丢装备，阶段7 指令十七）。
    //    AddInstance 自身拒绝重复 instanceId（阶段7.1 双保险）
    const ItemDefinition* definition = items.Get(equipped->definitionId);
    if (!inventory.AddInstance(*equipped, definition)) {
        if (!equipment.TrySetEquipped(slot, *equipped)) {
            LOG_ERROR("EquipmentSystem: unequip rollback failed, slot occupied unexpectedly.");
        }
        return {false, "inventory full"};
    }
    return {true, "ok"};
}

EquipmentComparison EquipmentSystem::Compare(const ItemDefinition& candidate,
                                             const ItemDefinition* current) {
    // 只输出差值（阶段7 指令三十五：不给 Better 结论）
    EquipmentComparison result;
    result.attackDelta = candidate.hasEquipment ? candidate.equipment.attackBonus : 0.0f;
    result.defenseDelta = candidate.hasEquipment ? candidate.equipment.defenseBonus : 0.0f;
    result.maxHpDelta = candidate.hasEquipment ? candidate.equipment.maxHpBonus : 0.0f;
    if (current != nullptr && current->hasEquipment) {
        result.attackDelta -= current->equipment.attackBonus;
        result.defenseDelta -= current->equipment.defenseBonus;
        result.maxHpDelta -= current->equipment.maxHpBonus;
    }
    return result;
}

} // namespace legend::item
