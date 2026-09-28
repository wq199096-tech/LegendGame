#include "Client/WorldNetwork/WorldClientController.h"

#include "Engine/Debug/Logger.h"
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
        case WorldNetworkEvent::Type::ProtocolError:
            LOG_WARN("[World] Protocol error: " + event.message);
            m_lastError = event.message;
            break;
    }
}

} // namespace legend::client
