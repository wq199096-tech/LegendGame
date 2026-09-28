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
    SetState(WorldFlowState::Disconnected);
}

void WorldClientController::OnDisconnected() {
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
        case WorldNetworkEvent::Type::ProtocolError:
            LOG_WARN("[World] Protocol error: " + event.message);
            m_lastError = event.message;
            break;
    }
}

} // namespace legend::client
