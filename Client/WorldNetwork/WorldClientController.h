#pragma once

#include "Client/WorldNetwork/RemoteMonsterManager.h"
#include "Client/WorldNetwork/RemotePlayerManager.h"
#include "Client/WorldNetwork/RemoteStatusEffectContainer.h"
#include "Client/WorldNetwork/WorldNetworkClient.h"

#include <cstdint>
#include <memory>
#include <string>

namespace legend::client {

// 阶段11 指令四十五/五十：WorldClientController——世界连接编排。
// CharacterSelect 成功后（拿到 selectionTicket）自动连接 WorldServer；
// 只更新自身状态/缓存，禁止直接修改 PlayerCharacter/Combat。
// 阶段12：持有 RemotePlayerManager（指令三十三，主线程独占，指令四十七）。
// 阶段13：持有 RemoteMonsterManager（指令五十二/六十）。
class WorldClientController {
public:
    WorldClientController();

    // 每帧由上层喂入世界网络事件（主线程消费）。
    void HandleEvent(const WorldNetworkEvent& event);
    void OnDisconnected();
    // 阶段12 指令三十七：每帧远程玩家插值（主线程）。
    void UpdateRemotePlayers(float deltaTime);
    // 阶段13 指令五十三：每帧远程怪物插值（主线程）。
    void UpdateRemoteMonsters(float deltaTime);

    // 指令四十六：CharacterSelect 成功后调用（自动连接 + EnterWorld）。
    void EnterWorldWithTicket(const std::string& selectionTicket);
    // 阶段11 指令三：世界端点配置（默认 127.0.0.1:7200）。
    void SetWorldEndpoint(const std::string& host, std::uint16_t port) {
        m_client->SetWorldEndpoint(host, port);
    }
    // 指令三十四：WorldReady 后发送移动输入（方向，禁止绝对坐标）。
    void SendMoveInput(float directionX, float directionY, float deltaTime);
    // 阶段14 指令五十九/六十一：Debug 攻击——只发目标（服务器重新验证）。
    void SendAttack(std::uint64_t targetEntityId);
    // 阶段15 指令五十五/五十九：Debug 施法——只发 skillId + 目标类型/ID
    //（伤害/Mana/CD/完成时间全部服务器权威，指令一/二）。
    void SendSkillCast(std::uint32_t skillId, std::uint8_t targetType,
                       std::uint64_t targetEntityId);
    void Disconnect();

    WorldFlowState State() const { return m_state; }
    bool IsWorldReady() const { return m_state == WorldFlowState::WorldReady; }
    std::uint64_t WorldConnectionId() const { return m_client->ServerConnectionId(); }
    std::uint64_t CharacterId() const { return m_characterId; }
    std::uint16_t MapId() const { return m_mapId; }
    // 阶段14 指令六十五/七十一：本地玩家 HP（服务器权威事件驱动维护）。
    std::uint32_t LocalCurrentHp() const { return m_localCurrentHp; }
    std::uint32_t LocalMaxHp() const { return m_localMaxHp; }
    bool LocalAlive() const { return m_localAlive; }
    std::uint64_t LastAttackRequestId() const { return m_lastAttackRequestId; }
    std::uint64_t LastSkillRequestId() const { return m_lastSkillRequestId; } // 阶段15
    // 阶段15 指令五十六：本地 Mana / 施法状态（服务器权威事件驱动维护）。
    std::uint32_t LocalCurrentMana() const { return m_localCurrentMana; }
    std::uint32_t LocalMaxMana() const { return m_localMaxMana; }
    bool LocalCasting() const { return m_localCasting; }
    std::uint64_t ActiveCastId() const { return m_activeCastId; }
    std::uint32_t ActiveSkillId() const { return m_activeSkillId; }
    float CastProgress() const; // 0~1（仅展示；完成必须等服务器事件，指令五十八）
    // 阶段16 指令六十四：本地玩家状态容器（仅展示；Remove 等 StatusRemoved/Snapshot）。
    const RemoteStatusEffectContainer& LocalStatusEffects() const { return m_localStatusEffects; }
    // 阶段12 指令三十三/七十二：远程玩家容器与 Debug 统计。
    const RemotePlayerManager& RemotePlayers() const { return m_remotePlayers; }
    std::uint32_t LastRemoteBatchSize() const { return m_lastRemoteBatchSize; }
    // 阶段13 指令五十二/五十八：远程怪物容器与 Debug 统计。
    const RemoteMonsterManager& RemoteMonsters() const { return m_remoteMonsters; }
    std::uint32_t LastMonsterBatchSize() const { return m_lastMonsterBatchSize; }
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
    void HandleStatusEvent(const WorldNetworkEvent& event); // 阶段16：状态事件路由

    std::shared_ptr<WorldNetworkClient> m_client = std::make_shared<WorldNetworkClient>();
    WorldFlowState m_state = WorldFlowState::Disconnected;
    RemotePlayerManager m_remotePlayers; // 阶段12 指令三十三
    std::uint32_t m_lastRemoteBatchSize = 0;
    RemoteMonsterManager m_remoteMonsters; // 阶段13 指令五十二
    std::uint32_t m_lastMonsterBatchSize = 0;

    std::uint64_t m_characterId = 0;
    std::uint16_t m_mapId = 1;
    float m_serverPositionX = 0.0f;
    float m_serverPositionY = 0.0f;
    std::uint32_t m_lastServerSequence = 0;
    std::uint32_t m_lastSentSequence = 0;
    std::uint16_t m_lastErrorCode = 0;
    std::string m_lastError;
    // 阶段14：本地玩家 HP（服务器权威：EnterWorldSuccess 初始化，CombatEvent/
    // HealthSnapshot/PlayerDeath 更新）。
    std::uint32_t m_localCurrentHp = 100;
    std::uint32_t m_localMaxHp = 100;
    bool m_localAlive = true;
    std::uint64_t m_lastAttackRequestId = 0;
    // 阶段15 指令五十六：本地 Mana / 施法状态（服务器权威事件驱动）。
    std::uint32_t m_localCurrentMana = 100;
    std::uint32_t m_localMaxMana = 100;
    bool m_localCasting = false;
    std::uint64_t m_activeCastId = 0;
    std::uint32_t m_activeSkillId = 0;
    std::uint64_t m_castStartSteadyMs = 0; // 本地展示计时（steady，仅进度条）
    std::uint32_t m_castDurationMs = 0;
    std::uint64_t m_lastSkillRequestId = 0;
    // 阶段16 指令六十四：本地玩家状态容器。
    RemoteStatusEffectContainer m_localStatusEffects;
};

} // namespace legend::client
