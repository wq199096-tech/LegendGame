#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/World/WorldError.h"

#include <algorithm>
#include <chrono>

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
    SetState(WorldFlowState::Disconnected);
}

void WorldClientController::OnDisconnected() {
    m_remotePlayers.Clear();
    m_remoteMonsters.Clear();
    SetState(WorldFlowState::Disconnected);
}

void WorldClientController::HandleEvent(const WorldNetworkEvent& event) {
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
    }
}

} // namespace legend::client
