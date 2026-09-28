#pragma once

#include "Client/WorldNetwork/WorldNetworkClient.h"

#include <cstdint>
#include <memory>
#include <string>

namespace legend::client {

// 阶段11 指令四十五/五十：WorldClientController——世界连接编排。
// CharacterSelect 成功后（拿到 selectionTicket）自动连接 WorldServer；
// 只更新自身状态/缓存，禁止直接修改 PlayerCharacter/Combat。
class WorldClientController {
public:
    WorldClientController();

    // 每帧由上层喂入世界网络事件（主线程消费）。
    void HandleEvent(const WorldNetworkEvent& event);
    void OnDisconnected();

    // 指令四十六：CharacterSelect 成功后调用（自动连接 + EnterWorld）。
    void EnterWorldWithTicket(const std::string& selectionTicket);
    // 阶段11 指令三：世界端点配置（默认 127.0.0.1:7200）。
    void SetWorldEndpoint(const std::string& host, std::uint16_t port) {
        m_client->SetWorldEndpoint(host, port);
    }
    // 指令三十四：WorldReady 后发送移动输入（方向，禁止绝对坐标）。
    void SendMoveInput(float directionX, float directionY, float deltaTime);
    void Disconnect();

    WorldFlowState State() const { return m_state; }
    bool IsWorldReady() const { return m_state == WorldFlowState::WorldReady; }
    std::uint64_t WorldConnectionId() const { return m_client->ServerConnectionId(); }
    std::uint64_t CharacterId() const { return m_characterId; }
    std::uint16_t MapId() const { return m_mapId; }
    float ServerPositionX() const { return m_serverPositionX; }
    float ServerPositionY() const { return m_serverPositionY; }
    std::uint32_t LastServerInputSequence() const { return m_lastServerSequence; }
    std::uint32_t LastSentInputSequence() const { return m_lastSentSequence; }
    float RttMs() const { return m_client->LastRttMs(); }
    std::uint16_t LastErrorCode() const { return m_lastErrorCode; }
    const std::string& LastError() const { return m_lastError; }
    const WorldNetworkClient& Client() const { return *m_client; }
    WorldNetworkClient& Client() { return *m_client; }

private:
    void SetState(WorldFlowState state);

    std::shared_ptr<WorldNetworkClient> m_client = std::make_shared<WorldNetworkClient>();
    WorldFlowState m_state = WorldFlowState::Disconnected;

    std::uint64_t m_characterId = 0;
    std::uint16_t m_mapId = 1;
    float m_serverPositionX = 0.0f;
    float m_serverPositionY = 0.0f;
    std::uint32_t m_lastServerSequence = 0;
    std::uint32_t m_lastSentSequence = 0;
    std::uint16_t m_lastErrorCode = 0;
    std::string m_lastError;
};

} // namespace legend::client
