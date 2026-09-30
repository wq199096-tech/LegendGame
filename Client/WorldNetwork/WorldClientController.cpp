#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Progression/ProgressionTypes.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/World/WorldError.h"
#include "Shared/WorldMap/MapTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace legend::client {

WorldClientController::WorldClientController() = default;

float WorldClientController::CastProgress() const {
    // 阶段15 指令五十八：本地进度仅展示；绝不因本地 100% 自行结算伤害。
    if (!m_localCasting || m_castDurationMs == 0) {
        return 0.0f;
    }
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    const float elapsed = static_cast<float>(nowMs - m_castStartSteadyMs);
    const float progress = elapsed / static_cast<float>(m_castDurationMs);
    return std::clamp(progress, 0.0f, 1.0f);
}

void WorldClientController::SetState(WorldFlowState state) {
    if (m_state != state) {
        LOG_DEBUG(std::string("[World] state ") + WorldFlowStateName(m_state) + " -> " +
                  WorldFlowStateName(state));
        m_state = state;
    }
}

void WorldClientController::UpdateRemotePlayers(float deltaTime) {
    // 阶段12 指令三十七：主线程插值（网络线程绝不直接改实体，指令四十七）。
    m_remotePlayers.Update(deltaTime);
}

void WorldClientController::UpdateRemoteMonsters(float deltaTime) {
    // 阶段13 指令五十三/六十：主线程怪物插值。
    m_remoteMonsters.Update(deltaTime);
}

void WorldClientController::EnterWorldWithTicket(const std::string& selectionTicket) {
    if (selectionTicket.empty()) {
        return;
    }
    if (m_state != WorldFlowState::Disconnected && m_state != WorldFlowState::Failed) {
        return;
    }
    m_lastErrorCode = 0;
    m_lastError.clear();
    // 阶段11 指令四十六：CharacterSelect 成功后连接 WorldServer（端点经配置）
    m_client->Connect();
    m_client->SendEnterWorld(selectionTicket); // 握手前暂存，握手成功自动发送
    SetState(WorldFlowState::Connecting);
}

void WorldClientController::SendMoveInput(float directionX, float directionY, float deltaTime) {
    if (!IsWorldReady()) {
        return;
    }
    m_client->SendMoveInput(++m_lastSentSequence, directionX, directionY, deltaTime);
}

void WorldClientController::SendAttack(std::uint64_t targetEntityId) {
    // 阶段14 指令五十九/六十一：Space 攻击——只发"最近可见怪 entityId"（由调用方
    // 选择，服务器重新验证）；requestId 单调递增，服务器防重放。
    if (!IsWorldReady()) {
        return;
    }
    m_client->SendAttack(++m_lastAttackRequestId,
                         static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster),
                         targetEntityId);
}

void WorldClientController::SendSkillCast(std::uint32_t skillId, std::uint8_t targetType,
                                          std::uint64_t targetEntityId) {
    // 阶段15 指令五十九：Debug 施法——只发 skillId + 目标（服务器重新验证一切）。
    if (!IsWorldReady()) {
        return;
    }
    m_client->SendSkillCast(++m_lastSkillRequestId, skillId, targetType, targetEntityId);
}

void WorldClientController::Disconnect() {
    m_client->Disconnect(true);
    m_remotePlayers.Clear();   // 阶段12 指令五十九（客户端侧）：断开清空远程实体
    m_remoteMonsters.Clear();  // 阶段13：断开清空远程怪物
    m_worldItems.Clear();      // 阶段18：断开清空掉落/背包/装备镜像
    m_inventory.Clear();
    m_equipment.Clear();
    m_quests.Clear();          // 阶段19：断开清空任务镜像（重进等 Snapshot）
    m_npcs.Clear();            // 阶段20：断开清空 NPC/对话/商店镜像
    m_dialogue.Clear();
    m_shop.Clear();
    SetState(WorldFlowState::Disconnected);
}

void WorldClientController::OnDisconnected() {
    m_remotePlayers.Clear();
    m_remoteMonsters.Clear();
    m_worldItems.Clear();
    m_inventory.Clear();
    m_equipment.Clear();
    m_quests.Clear();          // 阶段19：断开清空任务镜像
    m_npcs.Clear();            // 阶段20：断开清空 NPC 镜像
    m_dialogue.Clear();
    m_shop.Clear();
    SetState(WorldFlowState::Disconnected);
}

void WorldClientController::HandleEvent(const WorldNetworkEvent& event) {
    // 阶段24：视觉表现钩子（只读转发；钩子内禁止修改任何镜像状态）。
    if (m_visualEventHook) {
        m_visualEventHook(event);
    }
    switch (event.type) {
        case WorldNetworkEvent::Type::Connected:
            LOG_INFO("[World] Connected to world server.");
            break;
        case WorldNetworkEvent::Type::ConnectFailed:
            LOG_WARN("[World] Connect failed: " + event.message);
            m_lastError = event.message;
            SetState(WorldFlowState::Failed);
            break;
        case WorldNetworkEvent::Type::Disconnected:
            LOG_INFO("[World] Disconnected: " + event.message);
            m_remotePlayers.Clear();  // 阶段12 指令五十九
            m_remoteMonsters.Clear(); // 阶段13
            SetState(WorldFlowState::Disconnected);
            break;
        case WorldNetworkEvent::Type::HandshakeSuccess:
            LOG_INFO("[World] Handshake accepted connection=" +
                     std::to_string(m_client->ServerConnectionId()) + ".");
            break;
        case WorldNetworkEvent::Type::HandshakeFailed:
            LOG_WARN("[World] Handshake rejected: " + event.message);
            m_lastError = event.message;
            break;
        case WorldNetworkEvent::Type::EnterWorldSuccess:
            // 阶段11 指令五十：WorldReady + 本地参考位置 = 服务器位置
            m_characterId = event.characterId;
            m_mapId = event.mapId;
            m_serverPositionX = event.positionX;
            m_serverPositionY = event.positionY;
            m_lastServerSequence = 0;
            m_lastErrorCode = 0;
            m_lastError.clear();
            m_remotePlayers.Clear(); // 阶段12：新世界会话不残留上次远程实体
            m_lastRemoteBatchSize = 0;
            m_remoteMonsters.Clear(); // 阶段13：新世界会话不残留上次远程怪物
            m_lastMonsterBatchSize = 0;
            m_worldItems.Clear(); // 阶段18：新会话清空掉落/背包/装备镜像（等服务器 Snapshot）
            m_inventory.Clear();
            m_equipment.Clear();
            m_quests.Clear(); // 阶段19：新会话清空任务镜像（等服务器 Snapshot）
            m_npcs.Clear();   // 阶段20：新会话清空 NPC/对话/商店镜像（等服务器 Spawn）
            m_portals.Clear(); // 阶段21：新会话清空 Portal/地图镜像（等服务器 Spawn）
            m_map.Clear();
            m_localDeathTime = {};
            m_localRespawnTime = {};
            // 阶段14 指令十七/六十五：本地玩家 HP 初始化（服务器权威值）。
            m_localCurrentHp = event.currentHp;
            m_localMaxHp = event.maxHp;
            m_localAlive = event.alive;
            // 阶段15 指令五十六/六十九：本地玩家 Mana 初始化（服务器权威值）。
            m_localCurrentMana = event.currentMana;
            m_localMaxMana = event.maxManaVal;
            m_localCasting = false;
            m_activeCastId = 0;
            m_activeSkillId = 0;
            SetState(WorldFlowState::WorldReady);
            LOG_INFO("[World] EnterWorld success character=" + event.characterName + " (#" +
                     std::to_string(event.characterId) + ") map=" + std::to_string(event.mapId) +
                     " pos=(" + std::to_string(event.positionX) + "," +
                     std::to_string(event.positionY) + ")");
            break;
        case WorldNetworkEvent::Type::EnterWorldFailed:
            m_lastErrorCode = event.errorCode;
            m_lastError = event.message;
            SetState(WorldFlowState::Failed);
            LOG_WARN("[World] EnterWorld failed: " +
                     std::string(legend::world::WorldErrorCodeName(event.errorCode)) + " " +
                     event.message);
            break;
        case WorldNetworkEvent::Type::PositionSnapshot:
            // 阶段11 指令四十二：记录权威位置（阶段11 不回写本地角色）
            m_serverPositionX = event.positionX;
            m_serverPositionY = event.positionY;
            m_lastServerSequence = event.lastProcessedInputSequence;
            break;
        // ------------------------------------------------------------------
        // 阶段12 指令四十六：AOI 事件 -> RemotePlayerManager（主线程，指令四十七）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::PlayerSpawn: {
            // 指令三十四：不存在创建 / 已存在更新（Manager 保证不重复实体）。
            world::PlayerSpawnPayload spawn;
            spawn.characterId = event.characterId;
            spawn.name = event.characterName;
            spawn.classId = event.classId;
            spawn.gender = event.gender;
            spawn.level = event.level;
            spawn.mapId = event.mapId;
            spawn.positionX = event.positionX;
            spawn.positionY = event.positionY;
            spawn.serverTime = event.serverTime;
            // 阶段14 指令十六：PlayerSpawn HP
            spawn.currentHp = event.currentHp;
            spawn.maxHp = event.maxHp;
            spawn.alive = event.alive;
            m_remotePlayers.HandleSpawn(spawn);
            LOG_DEBUG("[World] PlayerSpawn #" + std::to_string(event.characterId) + " " +
                      event.characterName);
            break;
        }
        case WorldNetworkEvent::Type::PlayerDespawn:
            // 指令三十五：立即删除（四十九：Despawn 后旧 snapshot 自然忽略）。
            m_remotePlayers.HandleDespawn(event.characterId);
            LOG_DEBUG("[World] PlayerDespawn #" + std::to_string(event.characterId) + " reason=" +
                      std::to_string(static_cast<int>(event.despawnReason)));
            break;
        case WorldNetworkEvent::Type::RemotePlayerBatchSnapshot: {
            // 指令二十五/四十八：未知 characterId 丢弃在 Manager 内处理。
            world::RemotePlayerBatchSnapshotPayload batch;
            batch.serverTime = event.serverTime;
            batch.players = event.batchPlayers;
            m_remotePlayers.HandleBatch(batch);
            m_lastRemoteBatchSize = static_cast<std::uint32_t>(batch.players.size());
            break;
        }
        // ------------------------------------------------------------------
        // 阶段13 指令五十九/六十/六十一/六十二：Monster 事件 -> RemoteMonsterManager
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::MonsterSpawn: {
            world::MonsterSpawnPayload spawn;
            spawn.entityId = event.monsterEntityId;
            spawn.monsterTypeId = event.monsterTypeId;
            spawn.name = event.characterName;
            spawn.level = event.level;
            spawn.mapId = event.mapId;
            spawn.positionX = event.positionX;
            spawn.positionY = event.positionY;
            spawn.state = event.monsterState;
            spawn.serverTime = event.serverTime;
            // 阶段14 指令十五：MonsterSpawn HP
            spawn.currentHp = event.currentHp;
            spawn.maxHp = event.maxHp;
            spawn.alive = event.alive;
            m_remoteMonsters.HandleSpawn(spawn);
            LOG_DEBUG("[World] MonsterSpawn #" + std::to_string(event.monsterEntityId) + " " +
                      event.characterName);
            break;
        }
        case WorldNetworkEvent::Type::MonsterDespawn:
            m_remoteMonsters.HandleDespawn(event.monsterEntityId);
            LOG_DEBUG("[World] MonsterDespawn #" + std::to_string(event.monsterEntityId) +
                      " reason=" + std::to_string(static_cast<int>(event.despawnReason)));
            break;
        case WorldNetworkEvent::Type::MonsterBatchSnapshot: {
            // 指令六十一/九十七：未知 entityId 丢弃在 Manager 内处理。
            world::MonsterBatchSnapshotPayload batch;
            batch.serverTime = event.serverTime;
            batch.monsters = event.monsterBatch;
            m_remoteMonsters.HandleBatch(batch);
            m_lastMonsterBatchSize = static_cast<std::uint32_t>(batch.monsters.size());
            break;
        }
        // ------------------------------------------------------------------
        // 阶段14 指令六十二~七十三：服务器权威战斗事件
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::AttackResponse:
            // 指令十一：请求回执（真正伤害走 CombatEvent）。
            LOG_DEBUG("[Combat] AttackResponse requestId=" + std::to_string(event.requestId) +
                      " success=" + (event.success ? "true" : "false") + " code=" +
                      std::string(legend::world::CombatResultCodeName(event.resultCode)));
            break;
        case WorldNetworkEvent::Type::CombatEvent: {
            // 指令六十五：按目标类型分发 HP 更新（不做客户端预测，指令六十六）。
            if (event.targetType ==
                static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster)) {
                m_remoteMonsters.HandleCombatEvent(event.eventId, event.targetType, event.targetId,
                                                   event.targetHpAfter, event.killed);
            } else if (event.targetId == m_characterId) {
                // 本地玩家 HP（指令六十五：由 WorldClientController 维护）。
                m_localCurrentHp = event.targetHpAfter;
                m_localMaxHp = event.targetMaxHp;
                if (event.killed) {
                    m_localAlive = false;
                }
            } else {
                m_remotePlayers.HandleCombatEvent(event.targetType, event.targetId,
                                                  event.targetHpAfter, event.targetMaxHp,
                                                  event.killed);
            }
            // 指令七十五：攻击表现——Debug 日志（不做正式特效）。
            // 阶段15 指令六十六：技能命中日志 [Combat] Skill X hit Monster #Y for Z。
            const bool playerAttacker =
                event.attackerType == static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
            std::string hitLog = std::string("[Combat] ") +
                                 (playerAttacker ? "Player #" : "Monster #") +
                                 std::to_string(event.attackerId);
            if (event.sourceType ==
                static_cast<std::uint8_t>(legend::world::CombatSource::Skill)) {
                hitLog += " (skill " + std::to_string(event.sourceId) + ")";
            }
            hitLog += std::string(" hit ") +
                      (event.targetType ==
                               static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster)
                           ? "Monster #"
                           : "Player #") +
                      std::to_string(event.targetId) + " for " + std::to_string(event.damage) +
                      (event.killed ? " (killed)" : "");
            LOG_INFO(hitLog);
            break;
        }
        case WorldNetworkEvent::Type::HealthSnapshot:
            // 指令六十八：1s 纠偏——本地/远程实体 HP 校正。
            if (event.entityType ==
                    static_cast<std::uint8_t>(legend::world::CombatEntityType::Player) &&
                event.entityId == m_characterId) {
                m_localCurrentHp = event.currentHp;
                m_localMaxHp = event.maxHp;
                m_localAlive = event.alive;
            } else if (event.entityType ==
                       static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster)) {
                const auto* monster = m_remoteMonsters.Find(event.entityId);
                if (monster != nullptr) {
                    // HealthSnapshot 不带 eventId，直接覆盖（权威纠偏）。
                    m_remoteMonsters.ApplyHealthSnapshot(event.entityId, event.currentHp,
                                                         event.maxHp, event.alive);
                }
            } else {
                const auto* remote = m_remotePlayers.Find(event.entityId);
                if (remote != nullptr) {
                    m_remotePlayers.ApplyHealthSnapshot(event.entityId, event.currentHp,
                                                        event.maxHp, event.alive);
                }
            }
            break;
        case WorldNetworkEvent::Type::MonsterDeath:
            // 指令七十二：RemoteMonsterEntity alive=false，保留直到 Despawn。
            m_remoteMonsters.HandleDeath(event.monsterEntityId);
            LOG_INFO("[Combat] Monster #" + std::to_string(event.monsterEntityId) +
                     " killed by player #" + std::to_string(event.characterId));
            break;
        case WorldNetworkEvent::Type::PlayerDeath:
            // 指令七十三：本地玩家 alive=false；远程玩家 alive=false。
            if (event.characterId == m_characterId) {
                m_localAlive = false;
                m_localCurrentHp = 0;
                m_localDeathTime = std::chrono::steady_clock::now(); // 阶段21：复活倒计时展示
                m_localRespawnTime = {};
                // 阶段15 指令十七：本地死亡清施法表现（服务器已先发 Cancelled(Dead)）。
                m_localCasting = false;
                m_activeCastId = 0;
                m_activeSkillId = 0;
                LOG_INFO("[Combat] You died (killer type=" +
                         std::to_string(static_cast<int>(event.attackerType)) + " id=" +
                         std::to_string(event.attackerId) + ")");
            } else {
                m_remotePlayers.HandleDeath(event.characterId);
                m_remotePlayers.ApplyCastState(event.characterId, false, 0, 0, 0);
                LOG_INFO("[Combat] Player #" + std::to_string(event.characterId) + " died.");
            }
            break;
        case WorldNetworkEvent::Type::ProtocolError:
            LOG_WARN("[World] Protocol error: " + event.message);
            m_lastError = event.message;
            break;
        // ------------------------------------------------------------------
        // 阶段15 指令五十五/五十六：服务器权威技能事件（Client 只响应，不做
        // 本地伤害预测——指令五十八：本地进度到 100% 不自行结算）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::SkillCastResponseEvent:
            // 指令二十四：请求回执；accepted 时等待 Started/Completed/Impact。
            m_localCurrentMana = event.currentMana;
            LOG_INFO(std::string("[Skill] Player #") + std::to_string(m_characterId) +
                     " cast " + std::to_string(event.skillId) + " -> " +
                     (event.accepted ? std::string("accepted")
                                     : std::string("rejected: ") +
                                           legend::world::SkillResultCodeName(
                                               event.skillResultCode)));
            break;
        case WorldNetworkEvent::Type::SkillCastStartedEvent:
            // 指令五十七/五十九：施法表现开始（本地 Caster 与远程 Caster）。
            if (event.characterId == m_characterId) {
                m_localCasting = true;
                m_activeCastId = event.castId;
                m_activeSkillId = event.skillId;
                m_castStartSteadyMs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
                m_castDurationMs = event.castTimeMs;
            } else {
                m_remotePlayers.ApplyCastState(event.characterId, true, event.skillId,
                                               event.serverTime, event.castTimeMs);
            }
            LOG_INFO("[Skill] Player #" + std::to_string(event.characterId) + " started cast " +
                     std::to_string(event.skillId) + " (" + std::to_string(event.castTimeMs) +
                     "ms)");
            break;
        case WorldNetworkEvent::Type::SkillCastCompletedEvent:
            // 指令五十七：完成只清表现状态——伤害必须等 Impact/CombatEvent。
            if (event.characterId == m_characterId) {
                m_localCasting = false;
                m_activeCastId = 0;
                m_activeSkillId = 0;
            } else {
                m_remotePlayers.ApplyCastState(event.characterId, false, 0, 0, 0);
            }
            break;
        case WorldNetworkEvent::Type::SkillCastCancelledEvent:
            // 指令二十八/五十七：取消（Moved/Dead/TargetInvalid）。
            if (event.characterId == m_characterId) {
                m_localCasting = false;
                m_activeCastId = 0;
                m_activeSkillId = 0;
            } else {
                m_remotePlayers.ApplyCastState(event.characterId, false, 0, 0, 0);
            }
            LOG_INFO("[Skill] Player #" + std::to_string(event.characterId) + " cast " +
                     std::to_string(event.skillId) + " cancelled (" +
                     legend::world::SkillCancelReasonName(event.cancelReason) + ")");
            break;
        case WorldNetworkEvent::Type::SkillImpact:
            // 指令二十九/六十六：Impact 到达才确认命中（Debug 表现：闪一下由
            // RemoteMonster HP 变化体现；正式飘字后续阶段）。
            LOG_INFO("[Skill] Skill " + std::to_string(event.skillId) + " by player #" +
                     std::to_string(event.characterId) + " impacted " +
                     std::to_string(event.impactTargets.size()) + " target(s)");
            break;
        case WorldNetworkEvent::Type::ManaSnapshot:
            // 指令六十七：1s Mana 快照纠偏（本人）。
            m_localCurrentMana = event.currentMana;
            m_localMaxMana = event.maxManaVal;
            break;
        // ------------------------------------------------------------------
        // 阶段16 指令六十四/六十五：状态事件路由（本地/远程玩家/远程怪物）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::StatusAppliedEvent:
        case WorldNetworkEvent::Type::StatusUpdatedEvent:
        case WorldNetworkEvent::Type::StatusRemovedEvent:
        case WorldNetworkEvent::Type::StatusSnapshotEvent:
            HandleStatusEvent(event);
            break;
        // ------------------------------------------------------------------
        // 阶段17 指令三十：成长/奖励事件（Client 只展示，数值全部服务器权威）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::RewardGrantedEvent:
            if (event.characterId == m_characterId) {
                m_localLevel = event.progression.level;
                m_localExperience = event.progression.newExperience;
                m_localExpToNext = static_cast<std::int64_t>(
                    legend::world::ExpToNextLevel(event.progression.level)) -
                    m_localExperience;
                if (m_localLevel >= legend::world::kProgressionMaxLevel) {
                    m_localExpToNext = 0;
                }
                m_localGold = event.progression.newGold;
            }
            LOG_INFO("[Progression] Reward " +
                     std::to_string(event.progression.expGranted) + " exp / " +
                     std::to_string(event.progression.goldGranted) + " gold to #" +
                     std::to_string(event.characterId));
            break;
        case WorldNetworkEvent::Type::LevelUpEvent:
            if (event.characterId == m_characterId) {
                m_localLevel = event.progression.newLevel;
                m_localExperience = event.progression.currentExp;
                m_localExpToNext = event.progression.expToNext;
                m_localMaxHp = event.progression.newMaxHp; // 升级回满（指令十二）
                m_localCurrentHp = event.progression.newMaxHp;
            }
            LOG_INFO("[Progression] Player #" + std::to_string(event.characterId) + " leveled " +
                     std::to_string(event.progression.oldLevel) + " -> " +
                     std::to_string(event.progression.newLevel));
            break;
        case WorldNetworkEvent::Type::ProgressionSnapshotEvent:
            if (event.characterId == m_characterId) {
                m_localLevel = event.progression.level;
                m_localExperience = event.progression.currentExp;
                m_localExpToNext = event.progression.expToNext;
                m_localGold = event.progression.newGold;
            }
            break;
        // ------------------------------------------------------------------
        // 阶段18：掉落/背包/装备镜像（Client 只是服务器状态投影，指令二十九）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::WorldItemSpawnEvent: {
            RemoteWorldItem item;
            item.dropEntityId = event.dropEntityId;
            item.itemDefinitionId = event.itemDefinitionId;
            item.quantity = event.itemQuantity;
            item.mapId = event.mapId;
            item.x = event.positionX;
            item.y = event.positionY;
            item.isOwnedByYou = event.itemOwnedByYou;
            item.ownerLockRemainingMs = event.ownerLockRemainingMs;
            m_worldItems.OnSpawn(item);
            LOG_DEBUG("[Item] WorldItemSpawn drop=" + std::to_string(event.dropEntityId) +
                      " item=" + std::to_string(event.itemDefinitionId));
            break;
        }
        case WorldNetworkEvent::Type::WorldItemDespawnEvent:
            // 指令四十八：Despawn 即移除（不保留幽灵掉落）。
            m_worldItems.OnDespawn(event.dropEntityId);
            LOG_DEBUG("[Item] WorldItemDespawn drop=" + std::to_string(event.dropEntityId) +
                      " reason=" + std::to_string(static_cast<int>(event.itemDespawnReason)));
            break;
        case WorldNetworkEvent::Type::ItemPickupResponseEvent:
            LOG_DEBUG("[Item] PickupResponse drop=" + std::to_string(event.dropEntityId) +
                      " success=" + (event.success ? "1" : "0") + " code=" +
                      std::to_string(static_cast<int>(event.itemResultCode)));
            break;
        case WorldNetworkEvent::Type::InventorySnapshotEvent:
            if (event.characterId == m_characterId) {
                m_inventory.ApplySnapshot(event.inventoryEntries);
            }
            break;
        case WorldNetworkEvent::Type::InventoryDeltaEvent:
            if (event.characterId == m_characterId) {
                world::InventoryEntryData entry;
                entry.instanceId = event.inventoryInstanceId;
                entry.definitionId = event.itemDefinitionId;
                entry.quantity = event.itemQuantity;
                entry.slotIndex = event.inventorySlotIndex;
                m_inventory.ApplyDelta(event.inventoryOpcode, entry);
            }
            break;
        case WorldNetworkEvent::Type::EquipItemResponseEvent:
        case WorldNetworkEvent::Type::UnequipItemResponseEvent:
            LOG_DEBUG("[Item] Equip/Unequip response success=" + std::string(event.success ? "1" : "0") +
                      " code=" + std::to_string(static_cast<int>(event.itemResultCode)));
            break;
        case WorldNetworkEvent::Type::EquipmentSnapshotEvent:
            if (event.characterId == m_characterId) {
                m_equipment.ApplySnapshot(event.equipmentSnapshot);
            }
            break;
        // ------------------------------------------------------------------
        // 阶段19：任务事件（Client 只是镜像，指令四十九；全部只来自本人服务器事件）
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::QuestAcceptResponseEvent:
            LOG_INFO("[Quest] Accept " + std::to_string(event.questId) + " -> " +
                     (event.success ? std::string("success")
                                    : std::string("failed: ") +
                                          legend::world::QuestResultCodeName(
                                              event.questResultCode)));
            break;
        case WorldNetworkEvent::Type::QuestTurnInResponseEvent:
            LOG_INFO("[Quest] TurnIn " + std::to_string(event.questId) + " -> " +
                     (event.success ? std::string("success")
                                    : std::string("failed: ") +
                                          legend::world::QuestResultCodeName(
                                              event.questResultCode)));
            break;
        case WorldNetworkEvent::Type::QuestAbandonResponseEvent:
            LOG_INFO("[Quest] Abandon " + std::to_string(event.questId) + " -> " +
                     (event.success ? std::string("success")
                                    : std::string("failed: ") +
                                          legend::world::QuestResultCodeName(
                                              event.questResultCode)));
            break;
        case WorldNetworkEvent::Type::QuestProgressUpdatedEvent:
            // 指令四十九：只应用服务器进度（不本地杀怪 +1）。
            m_quests.ApplyProgress(event.questId, event.questObjectiveId,
                                   event.questObjectiveCurrent, event.questObjectiveRequired);
            LOG_INFO("[Quest] progress quest=" + std::to_string(event.questId) + " objective=" +
                     std::to_string(event.questObjectiveId) + " " +
                     std::to_string(event.questObjectiveCurrent) + "/" +
                     std::to_string(event.questObjectiveRequired) + " state=" +
                     legend::world::QuestStateName(event.questState));
            break;
        case WorldNetworkEvent::Type::QuestStateChangedEvent:
            m_quests.ApplyStateChange(event.questId,
                                      static_cast<legend::world::QuestState>(event.questState));
            LOG_INFO("[Quest] state quest=" + std::to_string(event.questId) + " " +
                     legend::world::QuestStateName(event.questOldState) + " -> " +
                     legend::world::QuestStateName(event.questState));
            break;
        case WorldNetworkEvent::Type::QuestSnapshotEvent:
            if (event.characterId == m_characterId) {
                m_quests.ApplySnapshot(event.questSnapshot);
                LOG_INFO("[Quest] snapshot received quests=" +
                         std::to_string(event.questSnapshot.size()));
            }
            break;
        case WorldNetworkEvent::Type::QuestRewardGrantedEvent:
            // 指令四十一：奖励事件只发本人；newLevel/newExperience/newGold 由
            // 服务器随事件下发（本地成长数据展示同步）。
            m_localLevel = event.progression.newLevel;
            m_localExperience = event.progression.newExperience;
            m_localGold = event.progression.newGold;
            m_localExpToNext = static_cast<std::int64_t>(
                                   legend::world::ExpToNextLevel(event.progression.newLevel)) -
                               m_localExperience;
            if (m_localLevel >= legend::world::kProgressionMaxLevel) {
                m_localExpToNext = 0;
            }
            LOG_INFO("[Quest] reward quest=" + std::to_string(event.questId) + " exp=" +
                     std::to_string(event.questRewardExp) + " gold=" +
                     std::to_string(event.questRewardGold) + " item=" +
                     std::to_string(event.questRewardItemDefinitionId) + " x" +
                     std::to_string(event.questRewardItemQuantity));
            break;
        // ------------------------------------------------------------------
        // 阶段20：NPC / Dialogue / Shop / Teleport 事件（Client 只是镜像）。
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::NpcSpawnEvent:
            // 指令十五：只显示服务器 Spawn 过的 NPC。
            m_npcs.HandleSpawn(event.npcEntityId, event.npcDefinitionId, event.npcName,
                               event.mapId, event.positionX, event.positionY,
                               static_cast<legend::world::NpcType>(event.npcType), event.visualId);
            break;
        case WorldNetworkEvent::Type::NpcDespawnEvent:
            m_npcs.HandleDespawn(event.npcEntityId);
            break;
        case WorldNetworkEvent::Type::NpcInteractResponseEvent:
            LOG_INFO("[Npc] Interact npc=" + std::to_string(event.npcEntityId) + " -> " +
                     (event.success ? std::string("success session=") +
                                          std::to_string(event.dialogueSessionId)
                                    : std::string("failed: ") +
                                          legend::world::NpcResultCodeName(
                                              event.questResultCode)));
            break;
        case WorldNetworkEvent::Type::DialoguePayloadEvent:
            // options 为空 = 服务器指示关闭。
            m_dialogue.Apply(event.dialoguePayload);
            if (!m_dialogue.Active()) {
                m_shop.Clear();
            }
            break;
        case WorldNetworkEvent::Type::NpcQuestMarkerEvent:
            m_npcs.ApplyMarker(event.npcEntityId, event.questMarker);
            break;
        case WorldNetworkEvent::Type::ShopOpenResponseEvent:
            if (event.shopOpen.success) {
                m_shop.Apply(event.shopOpen);
                LOG_INFO("[Npc] shop opened session=" +
                         std::to_string(event.shopOpen.shopSessionId) + " entries=" +
                         std::to_string(event.shopOpen.entries.size()));
            } else {
                LOG_WARN("[Npc] shop open failed code=" +
                         std::to_string(static_cast<int>(event.shopOpen.resultCode)));
            }
            break;
        case WorldNetworkEvent::Type::ShopBuyResponseEvent:
            LOG_INFO(std::string("[Npc] buy ") + (event.shopBuy.success ? "ok" : "failed: ") +
                     (event.shopBuy.success ? " item=" +
                                                  std::to_string(event.shopBuy.itemDefinitionId) +
                                                  " x" + std::to_string(event.shopBuy.quantity) +
                                                  " cost=" +
                                                  std::to_string(event.shopBuy.goldSpent)
                                            : std::string(legend::world::ShopResultCodeName(
                                                  event.shopBuy.resultCode))));
            break;
        case WorldNetworkEvent::Type::ShopSellResponseEvent:
            LOG_INFO(std::string("[Npc] sell ") + (event.shopSell.success ? "ok" : "failed: ") +
                     (event.shopSell.success ? " item=" +
                                                   std::to_string(event.shopSell.itemDefinitionId) +
                                                   " x" + std::to_string(event.shopSell.quantity) +
                                                   " got=" +
                                                   std::to_string(event.shopSell.goldReceived)
                                             : std::string(legend::world::ShopResultCodeName(
                                                   event.shopSell.resultCode))));
            break;
        case WorldNetworkEvent::Type::TeleportResponseEvent:
            if (event.teleport.success) {
                m_serverPositionX = event.teleport.x;
                m_serverPositionY = event.teleport.y;
                m_mapId = event.teleport.mapId;
                LOG_INFO("[Npc] teleported to (" + std::to_string(event.teleport.x) + "," +
                         std::to_string(event.teleport.y) + ") cost=" +
                         std::to_string(event.teleport.goldCost));
            } else {
                LOG_WARN("[Npc] teleport failed: " +
                         std::string(legend::world::TeleportResultCodeName(
                             event.teleport.resultCode)));
            }
            break;
        // ------------------------------------------------------------------
        // 阶段21：地图 / Portal / 复活事件（Client 只是镜像，指令五十四/五十五）。
        // ------------------------------------------------------------------
        case WorldNetworkEvent::Type::PortalSpawnEvent:
            m_portals.HandleSpawn(event.portalEntityId, event.portalId, event.portalName,
                                  event.mapId, event.positionX, event.positionY,
                                  event.portalInteractionRadius, event.portalDestinationMapId,
                                  event.portalDestinationName);
            break;
        case WorldNetworkEvent::Type::PortalDespawnEvent:
            m_portals.HandleDespawn(event.portalEntityId);
            break;
        case WorldNetworkEvent::Type::PortalUseResponseEvent:
            if (event.portalUse.success) {
                LOG_INFO("[Portal] use portal #" + std::to_string(event.portalUse.requestId) +
                         " -> map " + std::to_string(event.portalUse.destinationMapId) + " (" +
                         std::to_string(event.portalUse.destinationX) + "," +
                         std::to_string(event.portalUse.destinationY) + ") cost=" +
                         std::to_string(event.portalUse.goldCost));
            } else {
                LOG_WARN("[Portal] use failed: " +
                         std::string(legend::world::PortalResultCodeName(
                             event.portalUse.resultCode)));
            }
            break;
        case WorldNetworkEvent::Type::MapChangedEvent:
            // 指令五十五：收到 MapChanged 立即清理旧地图全部镜像，等服务器新 AOI Spawn。
            m_remotePlayers.Clear();
            m_remoteMonsters.Clear();
            m_npcs.Clear();
            m_worldItems.Clear();
            m_portals.Clear();
            m_dialogue = ClientDialogueModel{};
            m_shop = ClientShopModel{};
            // 指令五十四：本地权威参考位置/地图更新。
            m_map.ApplyChanged(event.mapChanged);
            m_mapId = event.mapChanged.mapId;
            m_serverPositionX = event.mapChanged.x;
            m_serverPositionY = event.mapChanged.y;
            LOG_INFO("[Map] changed to map=" + std::to_string(event.mapChanged.mapId) + " (" +
                     event.mapChanged.mapName + ") pos=(" +
                     std::to_string(event.mapChanged.x) + "," + std::to_string(event.mapChanged.y) +
                     ")");
            break;
        case WorldNetworkEvent::Type::MapSnapshotEvent:
            m_map.ApplySnapshot(event.mapSnapshot);
            m_mapId = event.mapSnapshot.mapId;
            LOG_INFO("[Map] snapshot map=" + std::to_string(event.mapSnapshot.mapId) + " (" +
                     event.mapSnapshot.mapName + ")");
            break;
        case WorldNetworkEvent::Type::RespawnResponseEvent:
            if (event.respawn.success) {
                LOG_INFO("[Respawn] success map=" + std::to_string(event.respawn.mapId) +
                         " cost=" + std::to_string(event.respawn.goldCost));
            } else {
                LOG_WARN("[Respawn] rejected: " +
                         std::string(legend::world::RespawnResultCodeName(
                             event.respawn.resultCode)));
            }
            break;
        case WorldNetworkEvent::Type::PlayerRespawnedEvent:
            // 指令四十二：本地玩家复活（Alive 恢复 + 权威位置/HP/Mana/Gold 更新）。
            m_localAlive = true;
            m_localCurrentHp = event.playerRespawned.hp;
            m_localMaxHp = event.playerRespawned.maxHp;
            m_localCurrentMana = event.playerRespawned.mana;
            m_localMaxMana = event.playerRespawned.maxMana;
            m_localGold = event.playerRespawned.gold;
            m_mapId = event.playerRespawned.mapId;
            m_serverPositionX = event.playerRespawned.x;
            m_serverPositionY = event.playerRespawned.y;
            m_localDeathTime = {};
            m_localRespawnTime = std::chrono::steady_clock::now(); // 指令一百一十三：保护展示
            LOG_INFO("[Respawn] you respawned at map=" + std::to_string(event.playerRespawned.mapId) +
                     " (" + std::to_string(event.playerRespawned.x) + "," +
                     std::to_string(event.playerRespawned.y) + ")");
            break;
    }
}

// ---------------------------------------------------------------------------
// 阶段19：Debug 任务操作（Ctrl+1~5 接取 / Shift+1~5 提交 / Alt+1~5 放弃；
// 只发 questId——指令二，状态/进度全部服务器权威）
// ---------------------------------------------------------------------------

void WorldClientController::SendQuestAccept(std::uint32_t questId) {
    if (!IsWorldReady()) {
        return;
    }
    m_lastQuestRequestId = m_nextQuestRequestId++;
    m_client->SendQuestAccept(m_lastQuestRequestId, questId);
}

void WorldClientController::SendQuestTurnIn(std::uint32_t questId) {
    if (!IsWorldReady()) {
        return;
    }
    m_lastQuestRequestId = m_nextQuestRequestId++;
    m_client->SendQuestTurnIn(m_lastQuestRequestId, questId);
}

void WorldClientController::SendQuestAbandon(std::uint32_t questId) {
    if (!IsWorldReady()) {
        return;
    }
    m_lastQuestRequestId = m_nextQuestRequestId++;
    m_client->SendQuestAbandon(m_lastQuestRequestId, questId);
}

// ---------------------------------------------------------------------------
// 阶段20：NPC Debug 交互（E / 对话数字键 / 商店 B·S；全部只发 id——指令二）
// ---------------------------------------------------------------------------

bool WorldClientController::SendInteractNearestNpc(float selfX, float selfY) {
    if (!IsWorldReady()) {
        return false;
    }
    // 指令十七：最近 visible NPC 且距离<=120（Client 选最近仅便利；服务器重验）。
    const std::uint64_t* bestId = nullptr;
    float bestDistSq = 0.0f;
    for (const auto& [npcEntityId, npc] : m_npcs.All()) {
        if (!npc.alive) {
            continue;
        }
        const float dx = npc.x - selfX;
        const float dy = npc.y - selfY;
        const float distSq = dx * dx + dy * dy;
        if (distSq > legend::world::kNpcDefaultInteractionRange *
                         legend::world::kNpcDefaultInteractionRange) {
            continue;
        }
        if (bestId == nullptr || distSq < bestDistSq) {
            bestId = &npcEntityId;
            bestDistSq = distSq;
        }
    }
    if (bestId == nullptr) {
        return false;
    }
    m_lastNpcRequestId = m_nextNpcRequestId++;
    m_client->SendNpcInteract(m_lastNpcRequestId, *bestId);
    return true;
}

bool WorldClientController::SendDialogueOptionByIndex(std::size_t oneBased) {
    if (!IsWorldReady() || !m_dialogue.Active()) {
        return false;
    }
    world::DialogueOptionData option;
    if (!m_dialogue.FindOptionByIndex(oneBased, option)) {
        return false;
    }
    m_lastNpcRequestId = m_nextNpcRequestId++;
    m_client->SendDialogueOption(m_lastNpcRequestId, m_dialogue.SessionId(), option.optionId);
    return true;
}

void WorldClientController::SendShopOpenRequest() {
    if (!IsWorldReady() || !m_dialogue.Active()) {
        return;
    }
    m_lastNpcRequestId = m_nextNpcRequestId++;
    m_client->SendShopOpen(m_lastNpcRequestId, m_dialogue.SessionId());
}

bool WorldClientController::SendBuySelected(std::uint32_t quantity) {
    if (!IsWorldReady() || !m_shop.Active()) {
        return false;
    }
    // 指令八十一 Debug：买第一件 canBuy 条目（简化 UI；链路与正式一致）。
    for (const auto& entry : m_shop.Entries()) {
        if (entry.canBuy) {
            m_lastNpcRequestId = m_nextNpcRequestId++;
            m_client->SendShopBuy(m_lastNpcRequestId, m_shop.SessionId(),
                                  entry.itemDefinitionId, quantity);
            return true;
        }
    }
    return false;
}

bool WorldClientController::SendSellSelected(std::uint64_t inventoryInstanceId,
                                             std::uint32_t quantity) {
    if (!IsWorldReady() || !m_shop.Active() || inventoryInstanceId == 0) {
        return false;
    }
    m_lastNpcRequestId = m_nextNpcRequestId++;
    m_client->SendShopSell(m_lastNpcRequestId, m_shop.SessionId(), inventoryInstanceId, quantity);
    return true;
}

bool WorldClientController::SendTeleportByOptionIndex(std::size_t oneBased) {
    // Teleport 走对话 Option（指令六十二：只能选择 NPC 提供的 Teleport Option）。
    return SendDialogueOptionByIndex(oneBased);
}

std::string WorldClientController::NpcStatusText() const {
    // 指令八十二：NPC 名字 + Marker + 对话/商店 Debug 文本。
    std::string text = "\n[NPC Debug]\n";
    for (const auto& [npcEntityId, npc] : m_npcs.All()) {
        text += "#" + std::to_string(npcEntityId) + " " + npc.name + " [" +
                legend::world::NpcTypeName(static_cast<std::uint8_t>(npc.type)) + "] marker=" +
                legend::world::NpcQuestMarkerName(
                    static_cast<std::uint8_t>(npc.questMarker)) + "\n";
    }
    if (m_dialogue.Active()) {
        text += "Dialogue #" + std::to_string(m_dialogue.SessionId()) + " " + m_dialogue.Title() +
                "\n";
        int index = 1;
        for (const auto& option : m_dialogue.Options()) {
            text += "  " + std::to_string(index++) + ". " + option.label + "\n";
        }
    }
    if (m_shop.Active()) {
        text += "Shop #" + std::to_string(m_shop.SessionId()) + " (B buy / S sell)\n";
        int index = 1;
        for (const auto& entry : m_shop.Entries()) {
            text += "  " + std::to_string(index++) + ". item " +
                    std::to_string(entry.itemDefinitionId) + " buy=" +
                    std::to_string(entry.buyPrice) + " sell=" +
                    std::to_string(entry.sellPrice) + "\n";
        }
    }
    return text;
}

// ---------------------------------------------------------------------------
// 阶段18：Debug 操作（E 拾取 / 6/7 装备 / 8/9 卸下；requestId 单调防重放）
// ---------------------------------------------------------------------------

void WorldClientController::SendPickup(std::uint64_t dropEntityId) {
    if (!IsWorldReady() || dropEntityId == 0) {
        return;
    }
    m_lastPickupRequestId = m_nextItemRequestId++;
    m_client->SendItemPickup(m_lastPickupRequestId, dropEntityId);
}

bool WorldClientController::FindFirstBagSlotOf(std::uint32_t definitionId,
                                               std::uint32_t& outSlotIndex) const {
    for (std::size_t i = 0; i < ClientInventoryModel::kSlots; ++i) {
        const auto& slot = m_inventory.Slot(i);
        if (slot.quantity > 0 && slot.definitionId == definitionId) {
            outSlotIndex = static_cast<std::uint32_t>(i);
            return true;
        }
    }
    return false;
}

bool WorldClientController::SendEquipFirstOf(std::uint32_t definitionId) {
    if (!IsWorldReady()) {
        return false;
    }
    std::uint32_t slotIndex = 0;
    if (!FindFirstBagSlotOf(definitionId, slotIndex)) {
        return false;
    }
    m_lastEquipRequestId = m_nextItemRequestId++;
    m_client->SendEquipItem(m_lastEquipRequestId, slotIndex);
    return true;
}

void WorldClientController::SendUnequip(std::uint8_t equipmentSlot) {
    if (!IsWorldReady()) {
        return;
    }
    m_lastEquipRequestId = m_nextItemRequestId++;
    m_client->SendUnequipItem(m_lastEquipRequestId, equipmentSlot);
}

void WorldClientController::HandleStatusEvent(const WorldNetworkEvent& event) {
    // 路由：目标 = 本地玩家 -> m_localStatusEffects；远程玩家/怪物 -> 对应容器。
    // Client 只展示（指令六十五）：不由持续时间移除，Remove/Snapshot 才变更删除。
    const bool isSelf = event.status.targetType ==
                            static_cast<std::uint8_t>(legend::world::CombatEntityType::Player) &&
                        event.status.targetEntityId == m_characterId;
    const bool isMonster =
        event.status.targetType ==
        static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster);

    switch (event.type) {
        case WorldNetworkEvent::Type::StatusAppliedEvent: {
            RemoteStatusEffect effect;
            effect.instanceId = event.status.instanceId;
            effect.effectId = event.status.effectId;
            effect.stacks = event.status.stacks;
            effect.remainingMs = event.status.remainingMs;
            effect.durationMs = event.status.durationMs;
            effect.sourceEntityId = event.status.sourceEntityId;
            if (isSelf) {
                m_localStatusEffects.Apply(effect);
            } else if (isMonster) {
                m_remoteMonsters.ApplyStatus(event.status.targetEntityId, effect);
            } else {
                m_remotePlayers.ApplyStatus(event.status.targetEntityId, effect);
            }
            LOG_INFO("[Status] effect " + std::to_string(event.status.effectId) + " x" +
                     std::to_string(event.status.stacks) + " applied to target #" +
                     std::to_string(event.status.targetEntityId));
            break;
        }
        case WorldNetworkEvent::Type::StatusUpdatedEvent:
            // 指令一百二十四：未知 instanceId 忽略，不创建幽灵状态。
            if (isSelf) {
                m_localStatusEffects.Update(event.status.instanceId, event.status.stacks,
                                            event.status.remainingMs);
            } else if (isMonster) {
                m_remoteMonsters.UpdateStatus(event.status.targetEntityId,
                                              event.status.instanceId, event.status.stacks,
                                              event.status.remainingMs);
            } else {
                m_remotePlayers.UpdateStatus(event.status.targetEntityId,
                                             event.status.instanceId, event.status.stacks,
                                             event.status.remainingMs);
            }
            LOG_INFO("[Status] effect " + std::to_string(event.status.effectId) + " updated on #" +
                     std::to_string(event.status.targetEntityId) + " x" +
                     std::to_string(event.status.stacks));
            break;
        case WorldNetworkEvent::Type::StatusRemovedEvent:
            // 指令一百二十五：未知 instanceId 忽略，不 Crash。
            if (isSelf) {
                m_localStatusEffects.Remove(event.status.instanceId);
            } else if (isMonster) {
                m_remoteMonsters.RemoveStatus(event.status.targetEntityId,
                                              event.status.instanceId);
            } else {
                m_remotePlayers.RemoveStatus(event.status.targetEntityId,
                                             event.status.instanceId);
            }
            LOG_INFO("[Status] effect " + std::to_string(event.status.effectId) + " removed (" +
                     std::to_string(static_cast<int>(event.status.reason)) + ") from #" +
                     std::to_string(event.status.targetEntityId));
            break;
        case WorldNetworkEvent::Type::StatusSnapshotEvent: {
            // 指令一百二十六：Snapshot 以服务器列表为准（多余删除、缺少创建）。
            std::vector<RemoteStatusEffect> effects;
            effects.reserve(event.status.snapshotEffects.size());
            for (const auto& entry : event.status.snapshotEffects) {
                RemoteStatusEffect effect;
                effect.instanceId = entry.instanceId;
                effect.effectId = entry.effectId;
                effect.stacks = entry.stacks;
                effect.remainingMs = entry.remainingMs;
                effects.push_back(effect);
            }
            if (isSelf) {
                m_localStatusEffects.SnapshotReplace(effects);
            } else if (isMonster) {
                m_remoteMonsters.SnapshotStatus(event.status.targetEntityId, effects);
            } else {
                m_remotePlayers.SnapshotStatus(event.status.targetEntityId, effects);
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 阶段21：Portal / 复活 / F9 Map Debug Panel（Client 只表达意图，指令二十/三十三）。
// ---------------------------------------------------------------------------

// 指令十九：F —— 选交互半径内最近 visible Portal 发 PortalUseRequest。
bool WorldClientController::SendPortalUseNearest(float selfX, float selfY) {
    if (!IsWorldReady()) {
        return false;
    }
    const std::uint64_t* bestId = nullptr;
    float bestDistSq = 0.0f;
    for (const auto& [portalEntityId, portal] : m_portals.All()) {
        if (!portal.active) {
            continue;
        }
        const float dx = portal.x - selfX;
        const float dy = portal.y - selfY;
        const float distSq = dx * dx + dy * dy;
        const float radius = portal.interactionRadius > 0.0f ? portal.interactionRadius : 100.0f;
        if (distSq > radius * radius) {
            continue;
        }
        if (bestId == nullptr || distSq < bestDistSq) {
            bestId = &portalEntityId;
            bestDistSq = distSq;
        }
    }
    if (bestId == nullptr) {
        return false;
    }
    m_lastMapRequestId = m_nextMapRequestId++;
    m_client->SendPortalUse(m_lastMapRequestId, *bestId);
    return true;
}

// 指令三十三：R/T —— 发 RespawnRequest（mode 由 GameScene 决定，服务器全重验）。
bool WorldClientController::SendRespawnRequest(std::uint8_t respawnMode) {
    if (!IsWorldReady()) {
        return false;
    }
    m_lastMapRequestId = m_nextMapRequestId++;
    m_client->SendRespawn(m_lastMapRequestId, respawnMode);
    return true;
}

// 指令一百一十二/一百一十三：F9 Map Debug Panel + 死亡/复活状态文本。
std::string WorldClientController::MapStatusText() const {
    std::string text = m_map.DebugText();
    text += "[F9 Map] visible: players=" + std::to_string(m_remotePlayers.Count()) +
            " monsters=" + std::to_string(m_remoteMonsters.Count()) +
            " npcs=" + std::to_string(m_npcs.Count()) +
            " drops=" + std::to_string(m_worldItems.Count()) +
            " portals=" + std::to_string(m_portals.Count()) + "\n";
    for (const auto& [portalEntityId, portal] : m_portals.All()) {
        text += "  " + portal.name + " -> " + portal.destinationName + " (map " +
                std::to_string(portal.destinationMapId) + ") @" +
                std::to_string(static_cast<int>(portal.x)) + "," +
                std::to_string(static_cast<int>(portal.y)) + "\n";
    }
    if (!m_localAlive) {
        // 指令一百一十三：Debug 死亡 Overlay（倒计时仅展示，服务器权威 3 秒）。
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                                             m_localDeathTime)
                                   .count();
        const double remain = elapsed < legend::world::kRespawnMinDelaySeconds
                                  ? legend::world::kRespawnMinDelaySeconds - elapsed
                                  : 0.0;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "[YOU DIED] Respawn in %.1fs | R = Current Map Respawn (%uG) | T = Town "
                      "Respawn (Free)\n",
                      remain, static_cast<unsigned>(legend::world::kRespawnCurrentMapGoldCost));
        text += buf;
    } else if (m_localRespawnTime.time_since_epoch().count() != 0) {
        const double sinceRespawn =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - m_localRespawnTime)
                .count();
        if (sinceRespawn < legend::world::kRespawnProtectionSeconds) {
            text += "[Respawn Protection] " +
                    std::to_string(legend::world::kRespawnProtectionSeconds - sinceRespawn) + "s\n";
        }
    }
    return text;
}

} // namespace legend::client
