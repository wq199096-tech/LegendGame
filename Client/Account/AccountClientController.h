#pragma once

#include "Client/Network/GameNetworkClient.h"
#include "Shared/Account/AccountError.h"
#include "Shared/Account/CharacterTypes.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace legend::client {

// 阶段10 指令五十六：AccountFlowState（客户端账号流程状态机）。
enum class AccountFlowState {
    Disconnected,
    Connecting,
    Handshake,
    Unauthenticated,
    Registering,
    LoggingIn,
    Authenticated,
    LoadingCharacters,
    CharacterListReady,
    CreatingCharacter,
    DeletingCharacter,
    SelectingCharacter,
    CharacterSelected,
    Failed,
};

const char* AccountFlowStateName(AccountFlowState state);

// 阶段10 指令五十七：AccountClientController——只负责发送 Request / 接收
// NetworkEvent / 更新 AccountFlowState。禁止直接修改 GameScene/Player/Combat。
class AccountClientController {
public:
    explicit AccountClientController(GameNetworkClient& client);

    // 每帧由上层（ClientNetworkController）喂入网络事件（主线程消费）。
    void HandleEvent(const NetworkEvent& event);
    // 网络断开时由上层调用（状态回 Disconnected，保留 token 供 SessionResume）。
    void OnNetworkDisconnected();
    // 握手成功时由上层调用（进入 Unauthenticated）。
    void SetState(AccountFlowState state);

    // ---- 发送侧（payload 在此编码，requestId 自增，指令一百零一） ----
    void SendRegister(const std::string& username, const std::string& password);
    void SendAccountLogin(const std::string& username, const std::string& password);
    void SendSessionResume(const std::string& token);
    void SendCharacterList(const std::string& token);
    void SendCreateCharacter(const std::string& token, const std::string& name,
                             std::uint16_t classId, std::uint16_t gender,
                             std::uint16_t visualId = 1);
    void SendDeleteCharacter(const std::string& token, std::uint64_t characterId);
    void SendSelectCharacter(const std::string& token, std::uint64_t characterId);

    // ---- 观测（只读） ----
    AccountFlowState State() const { return m_state; }
    std::uint64_t AccountId() const { return m_accountId; }
    bool HasSession() const { return !m_sessionToken.empty(); }
    const std::string& SessionToken() const { return m_sessionToken; }
    std::int64_t SessionExpiresAt() const { return m_sessionExpiresAt; }
    const std::vector<legend::account::CharacterSummary>& Characters() const {
        return m_characters;
    }
    bool HasSelectedCharacter() const { return m_selectedCharacterId != 0; }
    std::uint64_t SelectedCharacterId() const { return m_selectedCharacterId; }
    const legend::account::CharacterSummary& SelectedCharacter() const { return m_selectedCharacter; }
    const std::string& SelectionTicket() const { return m_selectionTicket; }
    std::uint16_t LastErrorCode() const { return m_lastErrorCode; }
    const std::string& LastError() const { return m_lastError; }
    std::uint64_t LastRequestId() const { return m_lastRequestId; }

private:
    void Fail(AccountFlowState backState, std::uint16_t errorCode, const std::string& message);
    std::uint64_t NextRequestId();
    bool CanSend() const;

    GameNetworkClient& m_client;
    AccountFlowState m_state = AccountFlowState::Disconnected;
    std::uint64_t m_nextRequestId = 1;

    std::uint64_t m_accountId = 0;
    std::string m_sessionToken;      // 仅内存（指令六十九：不落日志）
    std::int64_t m_sessionExpiresAt = 0;
    std::vector<legend::account::CharacterSummary> m_characters;
    std::uint64_t m_selectedCharacterId = 0;
    legend::account::CharacterSummary m_selectedCharacter;
    std::string m_selectionTicket;   // 仅内存
    std::uint16_t m_lastErrorCode = 0;
    std::string m_lastError;
    std::uint64_t m_lastRequestId = 0;
};

} // namespace legend::client
