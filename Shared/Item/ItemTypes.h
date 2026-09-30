#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18：物品系统公共类型（Shared 层——Server/Client 共用，指令二/五/六）。
// 全部数值语义服务器权威；Client 只做镜像展示（指令二十九）。
// ---------------------------------------------------------------------------

// 物品大类（指令三：Weapon/Armor/Material 三类先行）。
enum class ItemType : std::uint8_t {
    None = 0,
    Weapon = 1,    // 3001 Rusty Sword
    Armor = 2,     // 3002 Cloth Armor
    Material = 3,  // 3003 Slime Core（可堆叠，无战斗属性）
};

// 装备槽（指令五：先 Weapon/Armor 两槽；Helmet/Ring 等后续扩展）。
enum class EquipmentSlot : std::uint8_t {
    None = 0,
    Weapon = 1,
    Armor = 2,
};

// 物品 definitionId（阶段18 固定三种，指令三；阶段25 指令十四：新物品 3010+）。
inline constexpr std::uint32_t kItemRustySwordId = 3001;
inline constexpr std::uint32_t kItemClothArmorId = 3002;
inline constexpr std::uint32_t kItemSlimeCoreId = 3003;
inline constexpr std::uint32_t kItemBronzeSwordId = 3010;       // 阶段25 指令十三
inline constexpr std::uint32_t kItemApprenticeStaffId = 3011;   // 阶段25 指令十三
inline constexpr std::uint32_t kItemSpiritTalismanId = 3012;    // 阶段25 指令十三
inline constexpr std::uint32_t kItemTravelerArmorId = 3013;     // 阶段25 指令十三

// 背包容量（指令六：每角色 40 格）。
inline constexpr std::size_t kInventorySlots = 40;
// 堆叠上限（指令二十六：Slime Core 同 definition 优先堆叠，最大 99）。
inline constexpr std::uint32_t kSlimeCoreMaxStack = 99;

// 掉落规则（指令十五/十六/二十二）。
inline constexpr std::uint32_t kItemOwnerLockMs = 10000; // 击杀者独占 10s
inline constexpr std::uint32_t kItemDropTtlMs = 60000;   // 60s 无人拾取服务器删除
inline constexpr float kItemPickupRange = 100.0f;        // 拾取距离 <= 100

// WorldItemDespawn 原因（指令四十二）。
enum class ItemDespawnReason : std::uint8_t {
    PickedUp = 1,
    Expired = 2,
    ServerCleanup = 3,
};

// ItemPickupResponse / EquipItemResponse / UnequipItemResponse 结果码。
enum class ItemResultCode : std::uint8_t {
    None = 0,
    Success = 1,
    MalformedRequest = 2,
    NotInWorld = 3,        // 玩家未进世界
    Dead = 4,              // 死亡玩家不能拾取（指令二十二/五十八）
    DropNotFound = 5,      // Drop 不存在或已 claimed（指令二十三）
    WrongMap = 6,          // 跨地图
    NotVisible = 7,        // 不在 visibleItemDrops（服务器 AOI 权威）
    TooFar = 8,            // 距离 > 100
    OwnerLocked = 9,       // 归属他人（10s 独占期内）
    InventoryFull = 10,    // 背包满（Drop 留在地上，指令二十五）
    DuplicateRequest = 11, // requestId 重放（指令三十六/三十七）
    InvalidItem = 12,      // definitionId 不存在 / 不是装备（指令四十一类校验）
    WrongSlot = 13,        // 物品类型与槽位不匹配
    NotEquipped = 14,      // 卸下时槽位为空
    InternalError = 15,    // DB 失败等（Drop 回滚恢复，不能吞物品）
};

const char* ItemResultCodeName(std::uint8_t code);

} // namespace legend::world
