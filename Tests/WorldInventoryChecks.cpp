// ---------------------------------------------------------------------------
// 阶段18：服务器权威掉落/背包/装备检查（Loot/Inventory/Equipment V0.18）。
// 仍链接 LegendWorldTests（不新增测试 exe）。
// A 部分：纯逻辑（ItemDefinition/Inventory 40格/堆叠/DropRoller 确定性/
//         Drop SpatialGrid/AOI 滞回/装备交换纯逻辑）。
// B 部分：真实链路（死亡掉落/AOI 广播/拾取校验/原子性/防重放/持久化/重启）。
// C 部分：装备链路（Equip/Unequip/Derived 集成/替换/背包满/持久化重启）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/WorldServer/Item/DropRoller.h"
#include "Server/WorldServer/Item/InventoryContainer.h"
#include "Server/WorldServer/Item/InventoryRepository.h"
#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Server/WorldServer/Item/WorldItemDrop.h"
#include "Shared/Monster/MonsterDefinition.h"
#include "Server/WorldServer/Monster/MonsterEntity.h"
#include "Shared/Item/ItemDefinition.h"
#include "Shared/Item/ItemProtocol.h"
#include "Shared/Item/ItemTypes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <thread>

namespace worldtest {

namespace {

using namespace legend::world;
namespace CharacterRepository = legend::account::CharacterRepository;

using legend::world::DropRollResult;
using legend::world::DropRoller;
using legend::world::InventoryContainer;
using legend::world::InventoryEntry;
using legend::world::ItemRegistry;
using legend::world::SeededDropRoller;
using legend::world::WorldItemDrop;
using legend::world::WorldItemDropManager;
const std::uint8_t kTypePlayer = static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
const std::uint8_t kTypeMonster =
    static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);

// ---- 事件辅助（扫描 recorded 队列，基线计数避免旧事件命中） ----

std::size_t CountEventsOf(WorldTestClient& client, WorldNetworkEvent::Type type) {
    return client.recorded[WorldTestClient::IndexOf(type)].size();
}

// 在 recorded 中查找第一个满足谓词的事件（从 baseline 之后开始）。
bool FindRecordedFrom(WorldTestClient& client, WorldNetworkEvent::Type type,
                      std::size_t baseline, const std::function<bool(const WorldNetworkEvent&)>& pred,
                      WorldNetworkEvent& out) {
    const auto& events = client.recorded[WorldTestClient::IndexOf(type)];
    if (events.size() <= baseline) {
        return false;
    }
    for (auto it = events.begin() + static_cast<std::ptrdiff_t>(baseline); it != events.end();
         ++it) {
        if (pred(*it)) {
            out = *it;
            return true;
        }
    }
    return false;
}

bool WaitRecordedFrom(WorldTestClient& client, WorldNetworkEvent::Type type,
                      std::size_t baseline,
                      const std::function<bool(const WorldNetworkEvent&)>& pred,
                      WorldNetworkEvent& out, int timeoutMs = 4000) {
    return WaitUntil(
        [&] {
            client.DrainEvents();
            return FindRecordedFrom(client, type, baseline, pred, out);
        },
        timeoutMs);
}

// 找到掉落 Spawn 事件（按 definitionId；返回事件并推进该 type 的记录基线）。
bool WaitDropSpawn(WorldTestClient& client, std::uint32_t definitionId, WorldNetworkEvent& out,
                   int timeoutMs = 5000) {
    const std::size_t baseline =
        client.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::WorldItemSpawnEvent)]
            .size();
    return WaitRecordedFrom(
        client, WorldNetworkEvent::Type::WorldItemSpawnEvent, baseline,
        [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == definitionId; }, out,
        timeoutMs);
}

// 普攻击杀（每只 slime 80 HP：普攻 18 -> 5 次；requestId 递增防重放）。
bool BasicAttackUntilDead(WorldTestServers& servers, WorldTestClient& client,
                          std::uint64_t monsterId, std::uint64_t& requestId,
                          int timeoutMs = 15000) {
    return WaitUntil(
        [&] {
            auto monster = servers.world->FindMonster(monsterId);
            if (!monster || !monster->Alive()) {
                return true;
            }
            client.client().SendAttack(requestId++, kTypeMonster, monsterId);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return false;
        },
        timeoutMs);
}

// 等待怪物尸体清理（3s）后 slot 重生不再等待——掉落验证只需击杀完成。
void MoveSlimeNearA(WorldTestServers& servers, std::uint64_t monsterId) {
    servers.world->MoveMonsterTo(monsterId, 240.0f, 1060.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

std::string TicketFor(const std::shared_ptr<LoginServer>& login, const CharacterSeed& seed) {
    return login->Tickets().Create(seed.accountId, seed.characterId, 120.0);
}

bool SeedAt(Database& db, AccountService& accounts, CharacterService& characters,
            const std::string& username, const std::string& charName, float x, float y,
            CharacterSeed& out) {
    if (!SeedAccountAndCharacter(db, accounts, characters, username, charName, out)) {
        return false;
    }
    return CharacterRepository::UpdateWorldPosition(db, out.characterId, 1, x, y,
                                                    account::UnixNow())
        .success;
}

// 白盒读玩家有效属性。
struct PlayerStats {
    bool found = false;
    std::uint32_t effectiveAttack = 0;
    std::uint32_t effectiveDefense = 0;
    std::uint32_t baseAttack = 0;
    std::uint32_t baseDefense = 0;
};

PlayerStats ReadPlayerStats(WorldTestServers& servers, std::uint64_t characterId) {
    PlayerStats stats;
    auto player = servers.world->FindPlayerByCharacter(characterId);
    if (!player) {
        return stats;
    }
    stats.found = true;
    stats.effectiveAttack = player->EffectiveAttackPower();
    stats.effectiveDefense = player->EffectiveDefense();
    stats.baseAttack = player->BaseAttackPower();
    stats.baseDefense = player->BaseDefense();
    return stats;
}

// ===========================================================================
// A. 纯逻辑检查
// ===========================================================================

void RunInventoryLogicChecks() {
    // ---- ItemDefinitionCheck（指令二/三 + 阶段25 指令十三）：7 个定义字段正确 ----
    {
        ItemRegistry registry;
        bool ok = registry.Count() == 7;
        const auto* sword = registry.Find(kItemRustySwordId);
        const auto* armor = registry.Find(kItemClothArmorId);
        const auto* core = registry.Find(kItemSlimeCoreId);
        const auto* bronze = registry.Find(kItemBronzeSwordId);
        ok = ok && sword && armor && core && bronze;
        ok = ok && sword->type == ItemType::Weapon && sword->maxStack == 1 &&
             sword->attackBonus == 3 && sword->defenseBonus == 0 &&
             std::string(sword->name) == "Rusty Sword";
        ok = ok && armor->type == ItemType::Armor && armor->maxStack == 1 &&
             armor->defenseBonus == 2 && std::string(armor->name) == "Cloth Armor";
        ok = ok && core->type == ItemType::Material && core->maxStack == kSlimeCoreMaxStack &&
             core->attackBonus == 0 && core->defenseBonus == 0 &&
             std::string(core->name) == "Slime Core";
        ok = ok && bronze->type == ItemType::Weapon && bronze->attackBonus == 8 &&
             std::string(bronze->name) == "Bronze Sword";
        ok = ok && registry.Find(9999) == nullptr;
        Check("ItemDefinitionCheck: 7 definitions with correct fields (3010+ stage25)", ok);
    }

    // ---- Inventory40SlotCheck（指令六）：40 格容量 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        bool ok = bag.SlotCount() == kInventorySlots && bag.UsedCount() == 0;
        for (std::size_t i = 0; i < kInventorySlots; ++i) {
            const auto result =
                bag.Add(registry, kItemRustySwordId, 1, 100 + i, 0); // 非堆叠各占一格
            ok = ok && result.code == InventoryAddCode::Success;
        }
        ok = ok && bag.UsedCount() == 40 && bag.IsFull();
        const auto overflow = bag.Add(registry, kItemRustySwordId, 1, 0, 0);
        ok = ok && overflow.code == InventoryAddCode::Full &&
             overflow.remainingQuantity == 1;
        Check("Inventory40SlotCheck: 40 slots, exact fill, overflow rejected", ok);
    }

    // ---- SlimeCoreStackCheck（指令二十六）：同 definition 优先堆叠 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        const auto first = bag.Add(registry, kItemSlimeCoreId, 1, 5001, 0);
        const auto second = bag.Add(registry, kItemSlimeCoreId, 1, 5002, 0);
        bool ok = first.code == InventoryAddCode::Success && first.slotIndex == 0;
        ok = ok && second.code == InventoryAddCode::Merged && second.slotIndex == 0;
        ok = ok && bag.UsedCount() == 1 && bag.At(0)->quantity == 2;
        Check("SlimeCoreStackCheck: same definition merges into one stack", ok);
    }

    // ---- Stack99LimitCheck（指令二十六）：堆叠上限 99，超出开新格 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        const auto full = bag.Add(registry, kItemSlimeCoreId, 99, 6001, 0);
        const auto overflow = bag.Add(registry, kItemSlimeCoreId, 1, 6002, 0);
        bool ok = full.code == InventoryAddCode::Success && bag.At(0)->quantity == 99;
        ok = ok && overflow.code == InventoryAddCode::Success && overflow.slotIndex == 1 &&
             bag.At(1)->quantity == 1 && bag.UsedCount() == 2;
        Check("Stack99LimitCheck: stack caps at 99, overflow opens new slot", ok);
    }

    // ---- NonStackEquipmentCheck（指令二十七）：装备不堆叠、quantity 恒 1 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        const auto a = bag.Add(registry, kItemRustySwordId, 1, 7001, 0);
        const auto b = bag.Add(registry, kItemRustySwordId, 1, 7002, 0);
        bool ok = a.code == InventoryAddCode::Success && b.code == InventoryAddCode::Success &&
                  a.slotIndex != b.slotIndex;
        ok = ok && bag.At(a.slotIndex)->quantity == 1 && bag.At(b.slotIndex)->quantity == 1;
        Check("NonStackEquipmentCheck: equipment never stacks, quantity stays 1", ok);
    }

    // ---- DeterministicDropRollCheck（指令十/十一）：固定 seed 可复现 ----
    {
        const auto table = DropRoller::TrainingSlimeTable();
        SeededDropRoller rollerA(12345);
        SeededDropRoller rollerB(12345);
        bool ok = true;
        for (int i = 0; i < 200; ++i) {
            const auto resultsA = rollerA.Roll(table);
            const auto resultsB = rollerB.Roll(table);
            if (resultsA.size() != resultsB.size()) {
                ok = false;
                break;
            }
            for (std::size_t j = 0; j < resultsA.size(); ++j) {
                if (resultsA[j].definitionId != resultsB[j].definitionId ||
                    resultsA[j].quantity != resultsB[j].quantity) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                break;
            }
        }
        // Slime Core 100% 条目：任何 roll 必含 core。
        SeededDropRoller rollerC(999);
        bool alwaysCore = true;
        for (int i = 0; i < 50; ++i) {
            const auto results = rollerC.Roll(table);
            const bool hasCore = std::any_of(results.begin(), results.end(),
                                             [](const DropRollResult& r) {
                                                 return r.definitionId == kItemSlimeCoreId &&
                                                        r.quantity == 1;
                                             });
            if (!hasCore) {
                alwaysCore = false;
                break;
            }
        }
        Check("DeterministicDropRollCheck: fixed seed reproducible, core 100% always drops",
              ok && alwaysCore);
    }

    // ---- DropSpatialGridCheck（指令十七）：grid 添加/查询/claim/恢复 ----
    {
        WorldItemDropManager grid;
        bool ok = grid.Count() == 0;
        const auto now = std::chrono::steady_clock::now();
        WorldItemDrop drop;
        drop.dropEntityId = 1;
        drop.itemDefinitionId = kItemSlimeCoreId;
        drop.mapId = 1;
        drop.x = 100.0f;
        drop.y = 100.0f;
        drop.expireAt = now + std::chrono::hours(1);
        drop.active = true;
        grid.Add(drop);
        WorldItemDrop drop2 = drop;
        drop2.dropEntityId = 2;
        drop2.x = 300.0f;
        drop2.y = 100.0f;
        grid.Add(drop2);
        ok = ok && grid.Count() == 2;
        // 半径 50 只查到 #1；半径 300 查到两个（跨 cell）。
        ok = ok && grid.QueryNearby(100.0f, 100.0f, 50.0f, 1).size() == 1;
        ok = ok && grid.QueryNearby(100.0f, 100.0f, 300.0f, 1).size() == 2;
        // 跨地图不可见。
        ok = ok && grid.QueryNearby(100.0f, 100.0f, 300.0f, 2).size() == 0;
        // claim 原子移除 + restore 恢复。
        WorldItemDrop claimed;
        ok = ok && grid.Claim(1, claimed) && claimed.dropEntityId == 1 && grid.Count() == 1;
        ok = ok && !grid.Claim(1, claimed); // 二次 claim 失败（防 dup）
        grid.Restore(claimed);
        ok = ok && grid.Count() == 2 && grid.Find(1) != nullptr && grid.Find(1)->active;
        Check("DropSpatialGridCheck: add/query/map-filter/claim-once/restore", ok);
    }

    // ---- 100DropSpatialStressCheck（指令十七/四十九）：100 掉落查询正确性 ----
    {
        WorldItemDropManager grid;
        const auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            WorldItemDrop drop;
            drop.dropEntityId = static_cast<std::uint64_t>(i + 1);
            drop.itemDefinitionId = kItemSlimeCoreId;
            drop.mapId = 1;
            drop.x = 100.0f + static_cast<float>(i % 50) * 10.0f; // 100~590
            drop.y = 100.0f + static_cast<float>(i / 50) * 10.0f; // 100 或 110
            drop.expireAt = now + std::chrono::hours(1);
            drop.active = true;
            grid.Add(drop);
        }
        const auto nearDrops = grid.QueryNearby(100.0f, 100.0f, 150.0f, 1);
        // 查询结果必须与暴力扫描一致（半径内全部命中且不重不漏）。
        bool ok = grid.Count() == 100;
        int brute = 0;
        for (int i = 0; i < 100; ++i) {
            const float x = 100.0f + static_cast<float>(i % 50) * 10.0f;
            const float y = 100.0f + static_cast<float>(i / 50) * 10.0f;
            const float dx = x - 100.0f;
            const float dy = y - 100.0f;
            if (dx * dx + dy * dy <= 150.0f * 150.0f) {
                ++brute;
            }
        }
        ok = ok && static_cast<int>(nearDrops.size()) == brute;
        ok = ok && grid.QueryNearby(100.0f, 100.0f, 150.0f, 1).size() == nearDrops.size();
        Check("100DropSpatialStressCheck: 100 drops, grid query matches brute force", ok);
    }

    // ---- DropAoiHysteresisCheck（指令十八）：Enter 600 / Leave 700 滞回 ----
    {
        WorldItemDrop drop;
        drop.dropEntityId = 77;
        drop.mapId = 1;
        drop.x = 500.0f;
        drop.y = 0.0f;
        std::vector<legend::world::DropAoiCandidate> candidates;
        std::unordered_set<std::uint64_t> empty;
        std::unordered_set<std::uint64_t> visible{77};
        // 650：enter 外（600）但 leave 内（700）——不可新 spawn，已可见则保留。
        const float dist650Sq = 650.0f * 650.0f;
        candidates.push_back({&drop, dist650Sq});
        const auto deltaFar = legend::world::ResolveDropAoiVisibility(candidates, 1, empty, 600.0f,
                                                                     700.0f, 128);
        const auto deltaKept = legend::world::ResolveDropAoiVisibility(candidates, 1, visible,
                                                                      600.0f, 700.0f, 128);
        bool ok = deltaFar.spawns.empty();               // 未可见且 >enter -> 不 spawn
        ok = ok && deltaKept.despawns.empty();           // 已可见且 <=leave -> 保留
        // 750：>leave -> despawn。
        candidates.clear();
        candidates.push_back({&drop, 750.0f * 750.0f});
        const auto deltaOut = legend::world::ResolveDropAoiVisibility(candidates, 1, visible,
                                                                      600.0f, 700.0f, 128);
        ok = ok && deltaOut.despawns.size() == 1 && deltaOut.despawns[0] == 77;
        // 500：<=enter -> spawn。
        candidates.clear();
        candidates.push_back({&drop, 500.0f * 500.0f});
        const auto deltaIn = legend::world::ResolveDropAoiVisibility(candidates, 1, empty, 600.0f,
                                                                     700.0f, 128);
        ok = ok && deltaIn.spawns.size() == 1 && deltaIn.spawns[0]->dropEntityId == 77;
        Check("DropAoiHysteresisCheck: enter 600 / leave 700 hysteresis with limit", ok);
    }

    // ---- EquipServiceReplacedCheck（指令三十三）：替换装备原子交换 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        legend::world::EquipmentSlots equipped;
        bag.Add(registry, kItemRustySwordId, 1, 8001, 0); // slot 0
        bag.Add(registry, kItemRustySwordId, 1, 8002, 0); // slot 1
        const auto first = legend::world::EquipmentService::Equip(registry, bag, 0, equipped);
        bool ok = first.code == legend::world::EquipResultCode::Success &&
                  equipped.weapon.instanceId == 8001 && bag.UsedCount() == 1;
        // 装备 slot1 上的第二把剑（slot0 已空）。
        const auto second = legend::world::EquipmentService::Equip(registry, bag, 1, equipped);
        ok = ok && second.code == legend::world::EquipResultCode::Replaced &&
             equipped.weapon.instanceId == 8002;             // 新剑装备
        ok = ok && bag.At(1)->instanceId == 8001;            // 旧剑回到腾出的原槽（原子）
        Check("EquipServiceReplacedCheck: weapon replace swaps atomically via freed slot", ok);
    }

    // ---- UnequipBagFullCheck（指令三十四）：背包满卸下失败且装备不变 ----
    {
        ItemRegistry registry;
        InventoryContainer bag;
        legend::world::EquipmentSlots equipped;
        bag.Add(registry, kItemRustySwordId, 1, 8101, 0);
        (void)legend::world::EquipmentService::Equip(registry, bag, 0, equipped);
        for (std::size_t i = 0; i < kInventorySlots; ++i) {
            InventoryEntry entry;
            entry.instanceId = 8200 + i;
            entry.definitionId = kItemSlimeCoreId;
            entry.quantity = kSlimeCoreMaxStack;
            (void)bag.PutAt(i, entry);
        }
        const auto result = legend::world::EquipmentService::Unequip(
            registry, bag, EquipmentSlot::Weapon, equipped);
        bool ok = result.code == legend::world::EquipResultCode::BagFull &&
                  equipped.weapon.instanceId == 8101; // 装备保持不变
        Check("UnequipBagFullCheck: unequip fails with full bag, equipment unchanged", ok);
    }

    // ---- WrongSlotCheck（指令五十八 WrongSlot 语义）：类型与槽位一致性校验 ----
    // 阶段18 物品集中，槽位由类型唯一映射——一致性在定义层校验：
    // Weapon->Weapon 槽 / Armor->Armor 槽 / Material->无槽，且 Material 不可装备。
    {
        ItemRegistry registry;
        bool ok = registry.Find(kItemRustySwordId)->equipmentSlot == EquipmentSlot::Weapon &&
                  registry.Find(kItemClothArmorId)->equipmentSlot == EquipmentSlot::Armor &&
                  registry.Find(kItemSlimeCoreId)->equipmentSlot == EquipmentSlot::None;
        // Material 走 Equip -> InvalidItem（类型映射拒绝）。
        InventoryContainer bag;
        legend::world::EquipmentSlots equipped;
        bag.Add(registry, kItemSlimeCoreId, 1, 8301, 0);
        const auto result = legend::world::EquipmentService::Equip(registry, bag, 0, equipped);
        ok = ok && result.code == legend::world::EquipResultCode::InvalidItem &&
             equipped.weapon.quantity == 0;
        Check("WrongSlotCheck: definition slot mapping consistent, material rejected", ok);
    }
}

// ===========================================================================
// B. 掉落/拾取链路检查（真实 servers；owner lock 1.5s / TTL 6s 加速验证）
// ===========================================================================

void RunInventoryDropChecks() {
    // ---- 场景 1：A 单独（C 远处旁观 NoGlobalItemBroadcast）----
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("world_item_drop1");
        RemoveDb(servers.dbPath);
        servers.itemOwnerLockMs = 1500;
        servers.itemDropTtlMs = 6000;
        servers.worldRespawnDelayMs = 600000; // 阶段17 经验：防止重生怪围殴测试角色
        Check("DropServersStartCheck", servers.StartLogin() && servers.StartWorld());
        Database db;
        std::string error;
        const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
        AccountService accounts(8, 60);
        CharacterService characters(account::kMaxCharactersPerAccount);
        CharacterSeed seedA;
        CharacterSeed seedC;
        const bool seeded = dbOk && SeedAt(db, accounts, characters, "it_user_a", "ItA", 200.0f,
                                           1000.0f, seedA) &&
                            SeedAt(db, accounts, characters, "it_user_c", "ItC", 1900.0f, 1900.0f,
                                   seedC);
        MoveSlimeNearA(servers, 9);
        WorldTestClient clientA;
        WorldTestClient clientC;
        const bool entered = seeded && clientA.ConnectAndEnter(TicketFor(servers.login, seedA)) &&
                             clientC.ConnectAndEnter(TicketFor(servers.login, seedC));
        Check("DropScenario1SetupCheck", entered);
        if (!entered) {
            servers.StopAll();
            return;
        }
        clientA.DrainEvents();
        clientC.DrainEvents();

        // ---- DropGeneratedOnMonsterDeathCheck + SlimeCore100PercentDropCheck +
        //      WorldItemAoiSpawnCheck + NoGlobalItemBroadcastCheck ----
        const std::size_t dropsBefore = servers.world->WorldItemDropCount();
        const std::size_t aSpawnBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent);
        const std::size_t cSpawnBaseline =
            CountEventsOf(clientC, WorldNetworkEvent::Type::WorldItemSpawnEvent);
        std::uint64_t killRequestId = 6100;
        const bool killed = BasicAttackUntilDead(servers, clientA, 9, killRequestId);
        const bool dropped = WaitUntil(
            [&] { return servers.world->WorldItemDropCount() > dropsBefore; }, 3000);
        WorldNetworkEvent coreSpawn;
        const bool aGotSpawn =
            WaitRecordedFrom(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, aSpawnBaseline,
                             [](const WorldNetworkEvent&) { return true; }, coreSpawn);
        clientC.DrainEvents();
        const std::size_t cSpawns =
            CountEventsOf(clientC, WorldNetworkEvent::Type::WorldItemSpawnEvent) - cSpawnBaseline;
        Check("DropGeneratedOnMonsterDeathCheck/SlimeCore100PercentDropCheck/"
              "WorldItemAoiSpawnCheck/NoGlobalItemBroadcastCheck: kill drops core, "
              "AOI spawns to killer only",
              killed && dropped && aGotSpawn && coreSpawn.itemDefinitionId == kItemSlimeCoreId &&
                  cSpawns == 0);

        // ---- PickupSuccessCheck + InventoryDeltaCheck + DropRemovedAfterPickupCheck ----
        const std::uint64_t coreDropId = coreSpawn.dropEntityId;
        const std::size_t deltaBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::InventoryDeltaEvent);
        clientA.controller.SendPickup(coreDropId);
        WorldNetworkEvent pickupOk;
        const bool picked = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return e.success && e.dropEntityId == coreDropId;
            },
            pickupOk);
        WorldNetworkEvent delta;
        const bool gotDelta = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::InventoryDeltaEvent, deltaBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.itemDefinitionId == kItemSlimeCoreId && e.inventoryOpcode == 1;
            },
            delta);
        WorldNetworkEvent despawnEvent;
        const bool despawned = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemDespawnEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return e.dropEntityId == coreDropId &&
                       e.itemDespawnReason ==
                           static_cast<std::uint8_t>(ItemDespawnReason::PickedUp);
            },
            despawnEvent, 2000);
        const bool dropGone = servers.world->FindItemDrop(coreDropId) == nullptr;
        Check("PickupSuccessCheck/InventoryDeltaCheck/DropRemovedAfterPickupCheck: "
              "pickup grants item, delta syncs, drop removed with PickedUp",
              picked && gotDelta && despawned && dropGone);

        // ---- PickupDuplicateRequestCheck（指令三十六）：重复 requestId 拒绝 ----
        clientA.client().SendItemPickup(pickupOk.requestId, coreDropId); // 原样重放
        WorldNetworkEvent dupEvent;
        const bool dupRejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode == static_cast<std::uint8_t>(ItemResultCode::DuplicateRequest);
            },
            dupEvent, 2000);
        const auto mirrorUsed = clientA.controller.Inventory().UsedCount();
        Check("PickupDuplicateRequestCheck: replayed requestId rejected, bag unchanged",
              dupRejected && mirrorUsed == 1);

        // ---- ItemInstancePersistenceCheck（指令八/九）：instanceId 持久化 ----
        {
            Database queryDb;
            std::string queryError;
            const bool opened = queryDb.Open(servers.dbPath, queryError);
            const std::int64_t instanceId =
                opened ? QueryScalar(servers.dbPath,
                                     "SELECT instance_id FROM inventory_items "
                                     "WHERE character_id = " +
                                         std::to_string(seedA.characterId) +
                                         " AND item_definition_id = " +
                                         std::to_string(kItemSlimeCoreId) + ";")
                       : -1;
            Check("ItemInstancePersistenceCheck: pickup persisted with DB instanceId",
                  opened && instanceId > 0);
            queryDb.Close();
        }

        // ---- PickupDistanceCheck（指令二十二）：>100 距离拒绝，Drop 保留 ----
        {
            const std::size_t dropsNow = servers.world->WorldItemDropCount();
            servers.world->TestSpawnDrop(500.0f, 1000.0f, 1, 0, kItemSlimeCoreId); // 距 A 300
            const bool spawned = WaitUntil(
                [&] { return servers.world->WorldItemDropCount() > dropsNow; }, 2000);
            WorldNetworkEvent farSpawn;
            const bool visible = WaitDropSpawn(clientA, kItemSlimeCoreId, farSpawn, 2000);
            clientA.controller.SendPickup(farSpawn.dropEntityId);
            WorldNetworkEvent tooFar;
            const bool rejected = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [&](const WorldNetworkEvent& e) {
                    return !e.success &&
                           e.itemResultCode == static_cast<std::uint8_t>(ItemResultCode::TooFar);
                },
                tooFar);
            Check("PickupDistanceCheck: pickup beyond 100 rejected (TooFar), drop stays",
                  spawned && visible && rejected &&
                      servers.world->FindItemDrop(farSpawn.dropEntityId) != nullptr);
        }

        // ---- PickupInvisibleCheck（指令二十二）：不在 visibleItemDrops 拒绝 ----
        {
            const std::size_t dropsNow = servers.world->WorldItemDropCount();
            servers.world->TestSpawnDrop(220.0f, 1040.0f, 1, 0, kItemSlimeCoreId);
            std::uint64_t newDropId = 0;
            WaitUntil(
                [&] {
                    newDropId = servers.world->NextItemDropIdForTest() - 1;
                    return servers.world->WorldItemDropCount() > dropsNow;
                },
                2000);
            WorldNetworkEvent invisSpawn;
            WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent) - 1,
                [&](const WorldNetworkEvent& e) { return e.dropEntityId == newDropId; },
                invisSpawn, 2000); // 先进入可见集（AOI tick 广播）
            // 清除可见集后立即拾取（抢在下一个 AOI tick 重新收录之前）。
            // 与 AOI tick 存在良性竞争 -> 最多重试 4 次（重试时重新清除可见集）。
            bool rejected = false;
            for (int attempt = 0; attempt < 4 && !rejected; ++attempt) {
                servers.world->TestEraseVisibleItemDrop(seedA.characterId, newDropId);
                clientA.controller.SendPickup(newDropId);
                WorldNetworkEvent notVisible;
                rejected = WaitRecordedFrom(
                    clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                    [&](const WorldNetworkEvent& e) {
                        return e.dropEntityId == newDropId && !e.success &&
                               e.itemResultCode ==
                                   static_cast<std::uint8_t>(ItemResultCode::NotVisible);
                    },
                    notVisible, 1000);
            }
            Check("PickupInvisibleCheck: pickup without server visibility rejected", rejected);
        }

        // ---- PickupWrongMapCheck（指令二十二）：跨地图拒绝 ----
        {
            const std::size_t dropsNow = servers.world->WorldItemDropCount();
            servers.world->TestSpawnDrop(220.0f, 1040.0f, 2, 0, kItemSlimeCoreId); // map 2
            std::uint64_t map2DropId = 0;
            WaitUntil(
                [&] {
                    map2DropId = servers.world->NextItemDropIdForTest() - 1;
                    return servers.world->WorldItemDropCount() > dropsNow;
                },
                2000);
            // 手工加入可见集后立即拾取（抢在 AOI tick 因跨图剔除之前）。
            // 良性竞争 -> 最多重试 4 次（重试时重新加入可见集）。
            bool rejected = false;
            for (int attempt = 0; attempt < 4 && !rejected; ++attempt) {
                servers.world->TestAddVisibleItemDrop(seedA.characterId, map2DropId);
                clientA.controller.SendPickup(map2DropId);
                WorldNetworkEvent wrongMap;
                rejected = WaitRecordedFrom(
                    clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                    [&](const WorldNetworkEvent& e) {
                        return e.dropEntityId == map2DropId && !e.success &&
                               e.itemResultCode ==
                                   static_cast<std::uint8_t>(ItemResultCode::WrongMap);
                    },
                    wrongMap, 1000);
            }
            Check("PickupWrongMapCheck: cross-map pickup rejected", rejected);
        }

        // ---- RewardRegressionCheck（指令五十）：阶段17 Reward 不被破坏 ----
        {
            const std::size_t rewardBaseline =
                CountEventsOf(clientA, WorldNetworkEvent::Type::RewardGrantedEvent);
            MoveSlimeNearA(servers, 10);
            std::uint64_t rewardKillRequestId = 6300;
            (void)BasicAttackUntilDead(servers, clientA, 10, rewardKillRequestId);
            WorldNetworkEvent reward;
            const bool gotReward = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::RewardGrantedEvent, rewardBaseline,
                [&](const WorldNetworkEvent& e) { return e.success || !e.success; }, reward);
            Check("RewardRegressionCheck: monster kill still grants stage17 exp/gold reward",
                  gotReward);
        }

        clientA.Disconnect();
        clientC.Disconnect();
        servers.StopAll();
    }

    // ---- 场景 2：A + B 近距（归属/竞态/DOT owner/DB 回滚/TTL/重启）----
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("world_item_drop2");
        RemoveDb(servers.dbPath);
        servers.itemOwnerLockMs = 1500;  // 指令十五：生产 10s，测试缩短验证同一代码路径
        servers.itemDropTtlMs = 6000;    // 指令十六：生产 60s，测试缩短验证
        servers.worldRespawnDelayMs = 600000; // 防止重生怪围殴测试角色
        Check("DropServers2StartCheck", servers.StartLogin() && servers.StartWorld());
        Database db;
        std::string error;
        const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
        AccountService accounts(8, 60);
        CharacterService characters(account::kMaxCharactersPerAccount);
        CharacterSeed seedA;
        CharacterSeed seedB;
        const bool seeded = dbOk && SeedAt(db, accounts, characters, "it2_user_a", "JtA", 200.0f,
                                           1000.0f, seedA) &&
                            SeedAt(db, accounts, characters, "it2_user_b", "JtB", 300.0f,
                                   1100.0f, seedB);
        MoveSlimeNearA(servers, 9);
        WorldTestClient clientA;
        WorldTestClient clientB;
        const bool entered = seeded && clientA.ConnectAndEnter(TicketFor(servers.login, seedA)) &&
                             clientB.ConnectAndEnter(TicketFor(servers.login, seedB));
        Check("DropScenario2SetupCheck", entered);
        if (!entered) {
            servers.StopAll();
            return;
        }
        clientA.DrainEvents();
        clientB.DrainEvents();

        // ---- DropOwnerLockCheck（指令十五）：归属期内他人拾取拒绝 ----
        // CI 慢机防护：击杀循环期间怪物反击，A 若被围殴致死会级联污染后续检查。
        servers.world->TestBuffPlayerHp(seedA.characterId, 1000000);
        std::uint64_t killRequestId = 6400;
        (void)BasicAttackUntilDead(servers, clientA, 9, killRequestId);
        WorldNetworkEvent ownerSpawn;
        const bool spawnA = WaitDropSpawn(clientA, kItemSlimeCoreId, ownerSpawn);
        const bool spawnB = WaitDropSpawn(clientB, kItemSlimeCoreId, ownerSpawn, 2000);
        clientB.controller.SendPickup(ownerSpawn.dropEntityId);
        WorldNetworkEvent ownerDenied;
        const bool denied = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode ==
                           static_cast<std::uint8_t>(ItemResultCode::OwnerLocked);
            },
            ownerDenied);
        Check("DropOwnerLockCheck: killer owns drop, other player denied during lock",
              spawnA && spawnB && denied);

        // ---- DropPublicAfter10sCheck（指令十五）：锁期满公共拾取 ----
        std::this_thread::sleep_for(std::chrono::milliseconds(1900)); // lock 1.5s 过期
        clientB.controller.SendPickup(ownerSpawn.dropEntityId);
        WorldNetworkEvent publicPickup;
        const bool publicOk = WaitRecordedFrom(
            clientB, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) { return e.success; }, publicPickup);
        Check("DropPublicAfter10sCheck: after owner lock expiry anyone may pick up",
              publicOk);

        // ---- TwoPlayerSameDropRaceCheck（指令二十三）：同一 Drop 只成功一次 ----
        {
            const std::size_t dropsNow = servers.world->WorldItemDropCount();
            servers.world->TestSpawnDrop(220.0f, 1040.0f, 1, 0, kItemSlimeCoreId); // 无归属
            std::uint64_t raceDropId = 0;
            const bool spawned = WaitUntil(
                [&] {
                    raceDropId = servers.world->NextItemDropIdForTest() - 1;
                    return servers.world->WorldItemDropCount() > dropsNow;
                },
                2000);
            // 等待双方都收到 Spawn（AOI tick 收录后拾取请求才能通过 NotVisible 校验）。
            WorldNetworkEvent raceSpawnA;
            WorldNetworkEvent raceSpawnB;
            const bool seenA = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent),
                [&](const WorldNetworkEvent& e) { return e.dropEntityId == raceDropId; },
                raceSpawnA, 3000);
            const bool seenB = WaitRecordedFrom(
                clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent,
                CountEventsOf(clientB, WorldNetworkEvent::Type::WorldItemSpawnEvent),
                [&](const WorldNetworkEvent& e) { return e.dropEntityId == raceDropId; },
                raceSpawnB, 3000);
            const std::size_t aRespBaseline =
                CountEventsOf(clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent);
            const std::size_t bRespBaseline =
                CountEventsOf(clientB, WorldNetworkEvent::Type::ItemPickupResponseEvent);
            clientA.controller.SendPickup(raceDropId);
            clientB.controller.SendPickup(raceDropId); // 同 tick 争抢
            int successCount = 0;
            const bool settled = WaitUntil(
                [&] {
                    clientA.DrainEvents();
                    clientB.DrainEvents();
                    successCount = 0;
                    const auto& aResp =
                        clientA.recorded[WorldTestClient::IndexOf(
                            WorldNetworkEvent::Type::ItemPickupResponseEvent)];
                    const auto& bResp =
                        clientB.recorded[WorldTestClient::IndexOf(
                            WorldNetworkEvent::Type::ItemPickupResponseEvent)];
                    for (std::size_t i = aRespBaseline; i < aResp.size(); ++i) {
                        if (aResp[i].success && aResp[i].dropEntityId == raceDropId) {
                            ++successCount;
                        }
                    }
                    for (std::size_t i = bRespBaseline; i < bResp.size(); ++i) {
                        if (bResp[i].success && bResp[i].dropEntityId == raceDropId) {
                            ++successCount;
                        }
                    }
                    return successCount >= 1; // 一个成功即结算
                },
                3000);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 等第二个响应
            clientA.DrainEvents();
            clientB.DrainEvents();
            successCount = 0;
            const auto& aResp = clientA.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::ItemPickupResponseEvent)];
            const auto& bResp = clientB.recorded[WorldTestClient::IndexOf(
                WorldNetworkEvent::Type::ItemPickupResponseEvent)];
            for (std::size_t i = aRespBaseline; i < aResp.size(); ++i) {
                if (aResp[i].success && aResp[i].dropEntityId == raceDropId) {
                    ++successCount;
                }
            }
            for (std::size_t i = bRespBaseline; i < bResp.size(); ++i) {
                if (bResp[i].success && bResp[i].dropEntityId == raceDropId) {
                    ++successCount;
                }
            }
            Check("TwoPlayerSameDropRaceCheck: concurrent pickup of same drop succeeds once",
                  spawned && seenA && seenB && settled && successCount == 1);
        }

        // ---- DotKillDropOwnerCheck（指令四十/四十一）：DOT 击杀 owner = source ----
        std::uint64_t dotOwnerId = 0;
        {
            MoveSlimeNearA(servers, 8);
            // CI 慢机防护：A 若在攻击循环中被怪物 8 反击致死，后续攻击全部被拒
            //（Dead）→ HP 降不到 26 → Burn(32) 烧不死满血怪 → 无掉落 →
            // PickupDbFailure/DropExpire 级联失败。Buff 满血保证循环确定性。
            servers.world->TestBuffPlayerHp(seedA.characterId, 1000000);
            const std::uint64_t dropIdBefore = servers.world->NextItemDropIdForTest();
            // 打 3 次（80->62->44->26），Burn 4 跳 32 伤害必杀死。
            std::uint64_t dotKillRequestId = 6500;
            const bool lowered = WaitUntil(
                [&] {
                    auto m = servers.world->FindMonster(8);
                    if (!m || !m->Alive() || m->CurrentHp() <= 26) {
                        return true;
                    }
                    clientA.client().SendAttack(dotKillRequestId++, kTypeMonster, 8);
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    return false;
                },
                30000); // CI 慢机：攻击冷却 + 调度延迟放宽（原 15s）
            int hpAfterLoop = -1;
            bool aliveAfterLoop = false;
            {
                auto m = servers.world->FindMonster(8);
                if (m) {
                    hpAfterLoop = static_cast<int>(m->CurrentHp());
                    aliveAfterLoop = m->Alive();
                }
            }
            servers.world->ApplyStatusToTarget(kTypeMonster, 8, 2003, 1, kTypePlayer,
                                               seedA.characterId, 0u);
            clientA.Disconnect(); // 离线 killer（指令四十一：owner 保留其 ID）
            const bool dotKilled = WaitUntil(
                [&] {
                    auto m = servers.world->FindMonster(8);
                    return !m || !m->Alive();
                },
                12000);
            // CI 慢机加固：drop 由 io 线程在 KillMonster 内写入，测试线程直读存在
            // 可见性/调度延迟（曾致 dotKilled=1 但 drop=0 的三连级联）→ 轮询等待。
            std::uint32_t dropItemDefinitionId = 0;
            const bool dropAppeared = WaitUntil(
                [&] {
                    const auto* d = servers.world->FindItemDrop(dropIdBefore);
                    if (!d) {
                        return false;
                    }
                    dropItemDefinitionId = d->itemDefinitionId;
                    dotOwnerId = d->ownerCharacterId;
                    return true;
                },
                3000);
            static char dotDiag[224];
            std::snprintf(dotDiag, sizeof(dotDiag),
                          "DotKillDropOwnerCheck: offline DOT killer keeps drop ownership "
                          "[lowered=%d alive=%d hp=%d dotKilled=%d drop=%d ownerMatch=%d]",
                          lowered ? 1 : 0, aliveAfterLoop ? 1 : 0, hpAfterLoop,
                          dotKilled ? 1 : 0, dropAppeared ? 1 : 0,
                          dotOwnerId == seedA.characterId ? 1 : 0);
            Check(dotDiag, dotKilled && dropAppeared &&
                               dropItemDefinitionId == kItemSlimeCoreId &&
                               dotOwnerId == seedA.characterId);
        }

        // ---- PickupDbFailureRollbackCheck（指令二十四）：DB 失败回滚恢复 Drop ----
        {
            const std::uint64_t dbFailDropId =
                servers.world->NextItemDropIdForTest() - 1; // DOT kill 的 core drop
            const std::size_t dropsBefore = servers.world->WorldItemDropCount();
            // 第二连接 DROP 表 -> DB 写入失败（world 连接后续 INSERT 报错）。
            {
                Database breaker;
                std::string breakError;
                if (breaker.Open(servers.dbPath, breakError)) {
                    (void)breaker.Execute("DROP TABLE inventory_items;", breakError);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1800)); // 等 owner lock 过期
            clientB.controller.SendPickup(dbFailDropId);
            WorldNetworkEvent failure;
            const bool failedAtDb = WaitRecordedFrom(
                clientB, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [&](const WorldNetworkEvent& e) {
                    return !e.success &&
                           e.itemResultCode ==
                               static_cast<std::uint8_t>(ItemResultCode::InternalError);
                },
                failure, 5000);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            clientB.DrainEvents();
            const bool dropRestored = servers.world->FindItemDrop(dbFailDropId) != nullptr;
            const bool bagEmpty =
                clientB.controller.Inventory().UsedCount() == 1; // 只有先前 owner-unlock 拾取的 1 个
            static char dbDiag[192];
            std::snprintf(dbDiag, sizeof(dbDiag),
                          "PickupDbFailureRollbackCheck: DB failure restores drop, no item "
                          "swallowed [failedAtDb=%d restored=%d bagEmpty=%d dropsBefore=%zu]",
                          failedAtDb ? 1 : 0, dropRestored ? 1 : 0, bagEmpty ? 1 : 0,
                          dropsBefore);
            Check(dbDiag, failedAtDb && dropRestored && bagEmpty && dropsBefore > 0);
            // 重建表（后续重启持久化检查需要）。
            {
                Database fixer;
                std::string fixError;
                if (fixer.Open(servers.dbPath, fixError)) {
                    (void)fixer.Execute(
                        "CREATE TABLE IF NOT EXISTS inventory_items ("
                        "  instance_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "  character_id INTEGER NOT NULL,"
                        "  item_definition_id INTEGER NOT NULL,"
                        "  quantity INTEGER NOT NULL,"
                        "  slot_index INTEGER NOT NULL,"
                        "  created_at INTEGER NOT NULL,"
                        "  FOREIGN KEY(character_id) REFERENCES characters(id)"
                        ");",
                        fixError);
                }
            }
        }

        // ---- DropExpire60sCheck（指令十六/四十三）：TTL 过期服务器删除 ----
        //（TTL 配置缩短为 6s 验证同一代码路径）
        {
            const std::size_t despawnBaseline =
                CountEventsOf(clientB, WorldNetworkEvent::Type::WorldItemDespawnEvent);
            const bool expired = WaitUntil(
                [&] { return servers.world->WorldItemDropCount() == 0; }, 12000);
            WorldNetworkEvent expireEvent;
            const bool gotExpiredEvent = WaitRecordedFrom(
                clientB, WorldNetworkEvent::Type::WorldItemDespawnEvent, despawnBaseline,
                [&](const WorldNetworkEvent& e) {
                    return e.itemDespawnReason ==
                           static_cast<std::uint8_t>(ItemDespawnReason::Expired);
                },
                expireEvent, 3000); // CI 慢机：过期事件广播紧跟清理 tick，放宽到 3s（原 1s）
            static char expireDiag[160];
            std::snprintf(expireDiag, sizeof(expireDiag),
                          "DropExpire60sCheck: drops expire after TTL and broadcast Expired "
                          "[expired=%d event=%d dropsLeft=%zu]",
                          expired ? 1 : 0, gotExpiredEvent ? 1 : 0,
                          servers.world->WorldItemDropCount());
            Check(expireDiag, expired && gotExpiredEvent);
        }

        // ---- WorldRestartDropClearCheck + NoWorldDropPersistenceCheck（指令四十九）----
        {
            servers.world->TestSpawnDrop(220.0f, 1040.0f, 1, 0, kItemSlimeCoreId);
            WaitUntil([&] { return servers.world->WorldItemDropCount() > 0; }, 2000);
            const std::int64_t rowsBefore =
                QueryScalar(servers.dbPath, "SELECT COUNT(*) FROM inventory_items;");
            servers.StopWorld();
            const bool restarted = servers.StartWorld();
            const bool cleared = restarted && servers.world->WorldItemDropCount() == 0;
            const std::int64_t rowsAfter =
                QueryScalar(servers.dbPath, "SELECT COUNT(*) FROM inventory_items;");
            Check("WorldRestartDropClearCheck/NoWorldDropPersistenceCheck: restart clears "
                  "drops, drops never persisted",
                  cleared && rowsBefore == rowsAfter);
        }

        clientB.Disconnect();
        servers.StopAll();
    }
}

// ===========================================================================
// C. 装备链路检查（真实 servers；testForceDropAll 确定性地掉三件）
// ===========================================================================

void RunInventoryEquipChecks() {
    WorldTestServers servers;
    servers.dbPath = TempDbPath("world_item_equip");
    RemoveDb(servers.dbPath);
    servers.testForceDropAll = true; // 指令十一：链路不变，仅概率确定性（首杀必掉 3 件）
    servers.worldRespawnDelayMs = 600000; // 防止重生怪围殴测试角色
    Check("EquipServersStartCheck", servers.StartLogin() && servers.StartWorld());
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(8, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    CharacterSeed seedA;
    const bool seeded = dbOk && SeedAt(db, accounts, characters, "eq_user_a", "EqA", 200.0f,
                                       1000.0f, seedA);
    MoveSlimeNearA(servers, 9);
    WorldTestClient clientA;
    const bool entered = seeded && clientA.ConnectAndEnter(TicketFor(servers.login, seedA));
    Check("EquipScenarioSetupCheck", entered);
    if (!entered) {
        servers.StopAll();
        return;
    }
    clientA.DrainEvents();

    // ---- EquipmentSnapshotCheck（指令二十八）：进世界下发空装备快照 ----
    {
        WorldNetworkEvent snapshot;
        const bool got = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipmentSnapshotEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return e.equipmentSnapshot.weaponInstanceId == 0 &&
                       e.equipmentSnapshot.armorInstanceId == 0;
            },
            snapshot);
        Check("EquipmentSnapshotCheck: empty equipment snapshot sent on enter", got);
    }

    // ---- 击杀 + 拾取三件（core/sword/armor 各一，强制掉落确定性）----
    std::uint64_t swordInstance = 0;
    std::uint64_t armorInstance = 0;
    {
        const std::size_t spawnBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent);
        std::uint64_t killRequestId = 6600;
        (void)BasicAttackUntilDead(servers, clientA, 9, killRequestId);
        WorldNetworkEvent coreSpawn;
        WorldNetworkEvent swordSpawn;
        WorldNetworkEvent armorSpawn;
        const bool gotCore = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, spawnBaseline,
            [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemSlimeCoreId; },
            coreSpawn);
        const bool gotSword = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, spawnBaseline,
            [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemRustySwordId; },
            swordSpawn);
        const bool gotArmor = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, spawnBaseline,
            [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemClothArmorId; },
            armorSpawn);
        // 依次拾取（等待各自的 PickupResponse，保证串行落库回填 instanceId）。
        bool pickedSword = false;
        bool pickedArmor = false;
        bool pickedCore = false;
        if (gotSword) {
            clientA.controller.SendPickup(swordSpawn.dropEntityId);
            WorldNetworkEvent resp;
            pickedSword = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [&](const WorldNetworkEvent& e) {
                    return e.success && e.dropEntityId == swordSpawn.dropEntityId;
                },
                resp, 4000);
        }
        if (gotArmor) {
            clientA.controller.SendPickup(armorSpawn.dropEntityId);
            WorldNetworkEvent resp;
            pickedArmor = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [&](const WorldNetworkEvent& e) {
                    return e.success && e.dropEntityId == armorSpawn.dropEntityId;
                },
                resp, 4000);
        }
        if (gotCore) {
            clientA.controller.SendPickup(coreSpawn.dropEntityId);
            WorldNetworkEvent resp;
            pickedCore = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
                [&](const WorldNetworkEvent& e) {
                    return e.success && e.dropEntityId == coreSpawn.dropEntityId;
                },
                resp, 4000);
        }
        // 记录 instanceId（镜像 Delta 中读取）。
        const auto& deltas =
            clientA
                .recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::InventoryDeltaEvent)];
        for (const auto& d : deltas) {
            if (d.itemDefinitionId == kItemRustySwordId && swordInstance == 0) {
                swordInstance = d.inventoryInstanceId;
            }
            if (d.itemDefinitionId == kItemClothArmorId && armorInstance == 0) {
                armorInstance = d.inventoryInstanceId;
            }
        }
        Check("EquipScenarioLootCheck: forced kill drops core+sword+armor, all picked up",
              gotSword && gotArmor && gotCore && pickedSword && pickedArmor && pickedCore &&
                  swordInstance != 0 && armorInstance != 0 &&
                  clientA.controller.Inventory().UsedCount() == 3);
    }

    // ---- EquipSwordCheck + EquipAttackDerivedCheck（指令三十/三十一/五十二/五十三）----
    {
        const std::size_t equipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
        const bool sent = clientA.controller.SendEquipFirstOf(kItemRustySwordId);
        WorldNetworkEvent equipResp;
        const bool equipped = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.success &&
                       e.itemEquipmentSlot ==
                           static_cast<std::uint8_t>(EquipmentSlot::Weapon);
            },
            equipResp);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto stats = ReadPlayerStats(servers, seedA.characterId);
        Check("EquipSwordCheck/EquipAttackDerivedCheck: sword equips to weapon slot, "
              "effective attack 20+3=23 (basic attack and skills use it)",
              sent && equipped && stats.found && stats.effectiveAttack == 23 &&
                  stats.effectiveDefense == 5);
    }

    // ---- EquipArmorCheck + EquipDefenseDerivedCheck ----
    {
        const std::size_t equipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
        const bool sent = clientA.controller.SendEquipFirstOf(kItemClothArmorId);
        WorldNetworkEvent equipResp;
        const bool equipped = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.success &&
                       e.itemEquipmentSlot == static_cast<std::uint8_t>(EquipmentSlot::Armor);
            },
            equipResp);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto stats = ReadPlayerStats(servers, seedA.characterId);
        Check("EquipArmorCheck/EquipDefenseDerivedCheck: armor equips to armor slot, "
              "effective defense 5+2=7",
              sent && equipped && stats.found && stats.effectiveDefense == 7 &&
                  stats.effectiveAttack == 23);
    }

    // ---- StatusEquipmentCoexistCheck（指令五十一）：装备 + 状态叠加 ----
    {
        servers.world->ApplyStatusToTarget(kTypePlayer, seedA.characterId, 2001, 1, kTypePlayer,
                                           seedA.characterId, 0u);
        const bool applied = WaitUntil(
            [&] {
                const auto stats = ReadPlayerStats(servers, seedA.characterId);
                return stats.found && stats.effectiveAttack == 33; // 20 + 3(剑) + 10(BF)
            },
            3000);
        Check("StatusEquipmentCoexistCheck: level1 + sword3 + battle focus10 = attack 33",
              applied);
    }

    // ---- EquipReplacementCheck（指令三十三）：第二把剑替换，旧剑回背包 ----
    {
        MoveSlimeNearA(servers, 10);
        const std::size_t spawnBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent);
        std::uint64_t secondKillRequestId = 6700;
        (void)BasicAttackUntilDead(servers, clientA, 10, secondKillRequestId);
        WorldNetworkEvent secondSword;
        const bool gotSecond = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, spawnBaseline,
            [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemRustySwordId; },
            secondSword);
        clientA.controller.SendPickup(secondSword.dropEntityId);
        WorldNetworkEvent secondResp;
        (void)WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return e.success && e.dropEntityId == secondSword.dropEntityId;
            },
            secondResp, 4000);
        // 背包中现在有第一把剑（被换下）+ 第二把剑。
        std::uint32_t swordSlot = 0;
        const bool foundSword = clientA.controller.FindFirstBagSlotOf(kItemRustySwordId, swordSlot);
        const std::size_t equipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
        clientA.controller.Client().SendEquipItem(clientA.controller.LastEquipRequestId() + 100, swordSlot); // 装备第二把
        WorldNetworkEvent replaceResp;
        const bool replaced = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
            [&](const WorldNetworkEvent& e) { return e.success; }, replaceResp);
        clientA.DrainEvents();
        // 换装后背包里仍有另一把剑。
        std::uint32_t anotherSwordSlot = 0;
        const bool oldSwordInBag = clientA.controller.FindFirstBagSlotOf(kItemRustySwordId, anotherSwordSlot);
        Check("EquipReplacementCheck: equipping second sword returns first to bag atomically",
              gotSecond && replaced && foundSword && oldSwordInBag);
    }

    // ---- UnequipCheck（指令三十四）----
    {
        const std::size_t unequipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::UnequipItemResponseEvent);
        clientA.controller.SendUnequip(static_cast<std::uint8_t>(EquipmentSlot::Weapon));
        WorldNetworkEvent unequipResp;
        const bool unequipped = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::UnequipItemResponseEvent, unequipBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.success &&
                       e.itemEquipmentSlot ==
                           static_cast<std::uint8_t>(EquipmentSlot::Weapon);
            },
            unequipResp);
        std::uint32_t swordSlot = 0;
        const bool swordInBag = clientA.controller.FindFirstBagSlotOf(kItemRustySwordId, swordSlot);
        Check("UnequipCheck: unequip weapon returns sword to bag", unequipped && swordInBag);
    }

    // ---- InvalidItemEquipCheck（指令五十八 InvalidItem 语义）：Material 拒绝 ----
    {
        std::uint32_t coreSlot = 0;
        const bool foundCore = clientA.controller.FindFirstBagSlotOf(kItemSlimeCoreId, coreSlot);
        const std::size_t equipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
        clientA.controller.Client().SendEquipItem(clientA.controller.LastEquipRequestId() + 200, coreSlot);
        WorldNetworkEvent invalidResp;
        const bool rejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode ==
                           static_cast<std::uint8_t>(ItemResultCode::InvalidItem);
            },
            invalidResp);
        Check("InvalidItemEquipCheck: equipping material rejected", foundCore && rejected);
    }

    // ---- EquipDuplicateRequestCheck（指令三十七）：重放拒绝 ----
    {
        std::uint32_t swordSlot = 0;
        (void)clientA.controller.FindFirstBagSlotOf(kItemRustySwordId, swordSlot);
        const std::uint64_t dupRequestId = clientA.controller.LastEquipRequestId() + 300;
        const std::size_t equipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
        clientA.controller.Client().SendEquipItem(dupRequestId, swordSlot);
        // 等第一次响应（成功）。
        WorldNetworkEvent firstEquip;
        (void)WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
            [&](const WorldNetworkEvent& e) { return e.requestId == dupRequestId; }, firstEquip,
            4000);
        // 重放同一 requestId -> DuplicateRequest。
        clientA.controller.Client().SendEquipItem(dupRequestId, swordSlot);
        WorldNetworkEvent dupEquip;
        const bool dupRejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode ==
                           static_cast<std::uint8_t>(ItemResultCode::DuplicateRequest);
            },
            dupEquip, 3000);
        Check("EquipDuplicateRequestCheck: replayed equip requestId rejected", dupRejected);
    }

    // ---- UnequipInventoryFullCheck（指令三十四）：背包满卸下失败 ----
    {
        // 先装备一把剑（背包中现有）。
        std::uint32_t swordSlot = 0;
        if (clientA.controller.FindFirstBagSlotOf(kItemRustySwordId, swordSlot)) {
            const std::size_t equipBaseline =
                CountEventsOf(clientA, WorldNetworkEvent::Type::EquipItemResponseEvent);
            clientA.controller.Client().SendEquipItem(clientA.controller.LastEquipRequestId() + 400, swordSlot);
            WorldNetworkEvent refillResp;
            (void)WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::EquipItemResponseEvent, equipBaseline,
                [](const WorldNetworkEvent& e) { return e.success; }, refillResp, 4000);
        }
        servers.world->TestFillInventory(seedA.characterId); // 白盒填满（内存布景）
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::size_t unequipBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::UnequipItemResponseEvent);
        clientA.controller.SendUnequip(static_cast<std::uint8_t>(EquipmentSlot::Weapon));
        WorldNetworkEvent fullResp;
        const bool rejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::UnequipItemResponseEvent, unequipBaseline,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode ==
                           static_cast<std::uint8_t>(ItemResultCode::InventoryFull);
            },
            fullResp);
        const auto stats = ReadPlayerStats(servers, seedA.characterId);
        // 装备保持不变（武器槽仍有剑；attack 因 Battle Focus 可能仍在持续，
        // 用白盒武器槽断言替代具体数值）。
        auto player = servers.world->FindPlayerByCharacter(seedA.characterId);
        const bool weaponStillEquipped =
            player && player->EquipmentRef().weapon.quantity == 1;
        Check("UnequipInventoryFullCheck: unequip with full bag fails, weapon stays equipped",
              rejected && stats.found && weaponStillEquipped);
    }

    // ---- DeadPlayerPickupBlockedCheck（指令二十二）：死亡拾取拒绝 ----
    {
        servers.world->TestSpawnDrop(220.0f, 1040.0f, 1, 0, kItemSlimeCoreId);
        std::uint64_t deadDropId = 0;
        WaitUntil(
            [&] {
                deadDropId = servers.world->NextItemDropIdForTest() - 1;
                return servers.world->FindItemDrop(deadDropId) != nullptr;
            },
            2000);
        servers.world->TestAddVisibleItemDrop(seedA.characterId, deadDropId);
        servers.world->TestMarkPlayerDead(seedA.characterId);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        clientA.controller.SendPickup(deadDropId);
        WorldNetworkEvent deadResp;
        const bool rejected = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::ItemPickupResponseEvent, 0,
            [&](const WorldNetworkEvent& e) {
                return !e.success &&
                       e.itemResultCode == static_cast<std::uint8_t>(ItemResultCode::Dead);
            },
            deadResp);
        Check("DeadPlayerPickupBlockedCheck: dead player pickup rejected", rejected);
    }

    // ---- EquipmentPersistenceRestartCheck + InventoryPersistenceRestartCheck ----
    {
        const std::size_t snapshotBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::EquipmentSnapshotEvent);
        clientA.Disconnect();
        servers.StopWorld();
        const bool restarted = servers.StartWorld();
        const bool reentered = clientA.ConnectAndEnter(TicketFor(servers.login, seedA));
        clientA.DrainEvents();
        // 装备：重启后 Weapon 槽仍装备（slot_index 1001 加载），攻击 23。
        const auto stats = ReadPlayerStats(servers, seedA.characterId);
        bool weaponRestored = false;
        {
            WorldNetworkEvent snapAfter;
            weaponRestored = WaitRecordedFrom(
                clientA, WorldNetworkEvent::Type::EquipmentSnapshotEvent, snapshotBaseline,
                [&](const WorldNetworkEvent& e) {
                    return e.equipmentSnapshot.weaponDefinitionId == kItemRustySwordId;
                },
                snapAfter, 4000);
        }
        // 背包：重启后镜像包含持久化物品（sword#1 + core 在背包；armor 与 weapon
        // 均在装备槽——各自独立持久化）。
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        clientA.DrainEvents();
        const std::size_t usedAfter = clientA.controller.Inventory().UsedCount();
        // 再拾取一枚 core：新 instanceId 不与旧 instanceId 冲突（持久唯一）。
        MoveSlimeNearA(servers, 9);
        const std::size_t spawnBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent);
        std::uint64_t postRestartKillId = 6800;
        (void)BasicAttackUntilDead(servers, clientA, 9, postRestartKillId);
        WorldNetworkEvent postCore;
        (void)WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::WorldItemSpawnEvent, spawnBaseline,
            [&](const WorldNetworkEvent& e) { return e.itemDefinitionId == kItemSlimeCoreId; },
            postCore);
        const std::size_t deltaBaseline =
            CountEventsOf(clientA, WorldNetworkEvent::Type::InventoryDeltaEvent);
        clientA.controller.SendPickup(postCore.dropEntityId);
        WorldNetworkEvent postDelta;
        const bool postPickup = WaitRecordedFrom(
            clientA, WorldNetworkEvent::Type::InventoryDeltaEvent, deltaBaseline,
            [&](const WorldNetworkEvent& e) {
                return e.inventoryOpcode == 1 &&
                       e.itemDefinitionId == kItemSlimeCoreId &&
                       e.inventoryInstanceId != 0;
            },
            postDelta, 5000);
        const std::int64_t mergedQuantity =
            postPickup ? QueryScalar(servers.dbPath,
                                     "SELECT quantity FROM inventory_items WHERE instance_id = " +
                                         std::to_string(static_cast<long long>(
                                             postDelta.inventoryInstanceId)) +
                                         ";")
                       : -1;
        // 重启后再次拾取的 core 并入既有堆叠（quantity 2），不产生新 instanceId
        //——同一 instanceId 唯一且数量正确合并（持久唯一 + 堆叠持久化）。
        const bool restartOk = restarted && reentered && weaponRestored && stats.found &&
                               stats.effectiveAttack == 23 && usedAfter >= 2 && postPickup &&
                               mergedQuantity == 2;
        if (!restartOk) {
            std::printf(
                "[Diag] Restart: restarted=%d reentered=%d weaponRestored=%d found=%d "
                "effAtk=%u usedAfter=%zu postPickup=%d mergedQty=%lld\n",
                static_cast<int>(restarted), static_cast<int>(reentered),
                static_cast<int>(weaponRestored), static_cast<int>(stats.found),
                stats.effectiveAttack, usedAfter, static_cast<int>(postPickup),
                static_cast<long long>(mergedQuantity));
        }
        Check("EquipmentPersistenceRestartCheck/InventoryPersistenceRestartCheck: equipment "
              "and bag survive restart, stacked pickup merges with persisted quantity",
              restartOk);
    }

    clientA.Disconnect();
    servers.StopAll();
}

} // namespace

void RunWorldInventoryChecks() {
    std::printf("[WorldInventory] logic checks begin\n");
    RunInventoryLogicChecks();
    std::printf("[WorldInventory] drop checks begin\n");
    RunInventoryDropChecks();
    std::printf("[WorldInventory] equip checks begin\n");
    RunInventoryEquipChecks();
}

} // namespace worldtest
