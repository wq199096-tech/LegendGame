#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/World/WorldError.h"

namespace legend::client {

WorldClientController::WorldClientController() = default;

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
            const bool playerAttacker =
                event.attackerType == static_cast<std::uint8_t>(legend::world::CombatEntityType::Player);
            LOG_INFO(std::string("[Combat] ") +
                     (playerAttacker ? "Player #" : "Monster #") +
                     std::to_string(event.attackerId) + " hit " +
                     (event.targetType ==
                              static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster)
                          ? "Monster #"
                          : "Player #") +
                     std::to_string(event.targetId) + " for " + std::to_string(event.damage) +
                     (event.killed ? " (killed)" : ""));
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
                LOG_INFO("[Combat] You died (killer type=" +
                         std::to_string(static_cast<int>(event.attackerType)) + " id=" +
                         std::to_string(event.attackerId) + ")");
            } else {
                m_remotePlayers.HandleDeath(event.characterId);
                LOG_INFO("[Combat] Player #" + std::to_string(event.characterId) + " died.");
            }
            break;
        case WorldNetworkEvent::Type::ProtocolError:
            LOG_WARN("[World] Protocol error: " + event.message);
            m_lastError = event.message;
            break;
    }
}

} // namespace legend::client
