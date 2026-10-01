#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令十二/十三：ClientFlowController —— 玩家流程状态机集中管理。
//
// 职责：把 GameNetworkClient / AccountClientController / WorldClientController /
// CharacterSelectionController 的状态收敛为唯一 ClientFlowState，并执行 UI 层
// 提交的意图（登录/注册/建角/删角/进世界/离开世界）。纯逻辑（不依赖 SDL），
// GameScene 每帧调用 Update()；页面交互经 FlowUiModel 动作进入。
//
// AutoEnter 兼容（指令八）：LEGEND_CLIENT_AUTO_ENTER=1 时本控制器退化为纯观测
// （不自动连接/登录/拉列表，避免与 AutoEnter 链路重复提交）。
// ---------------------------------------------------------------------------

#include "Client/Account/AccountClientController.h"
#include "Client/Account/CharacterSelectionController.h"
#include "Client/Flow/ClientFlowState.h"
#include "Client/Network/GameNetworkClient.h"
#include "Client/Ui/FlowUiModel.h"
#include "Client/WorldNetwork/WorldClientController.h"

#include <chrono>
#include <string>
#include <vector>

namespace legend::flow {

class ClientFlowController {
public:
    ClientFlowController(legend::client::GameNetworkClient& net,
                         legend::client::AccountClientController& account,
                         legend::client::WorldClientController& world,
                         legend::client::CharacterSelectionController& selection);

    // 每帧调用：状态推导 + 待办意图执行（AutoEnter 模式下只推导）。
    void Update(float deltaTime);

    ClientFlowState State() const { return m_state; }
    FlowUiModel& Model() { return m_model; }
    const FlowUiModel& Model() const { return m_model; }

    // LEGEND_CLIENT_AUTO_ENTER=1（由 GameScene 构造时注入；观测模式）。
    void SetObserverOnly(bool observerOnly) { m_observerOnly = observerOnly; }

    // ---- 意图入口（GameScene 从 FlowUiModel 动作分发）----
    void RequestRetryConnect();
    void RequestLogin();            // 用 Model().accountName/password
    void RequestOpenRegister();     // 登录页 -> 注册页（纯页面切换，无服务器往返）
    void RequestRegister();         // 用 Model().regAccount/regPassword
    void RequestCancelRegister();
    void RequestOpenCreate();
    void RequestCancelCreate();
    void RequestSubmitCreate();     // 用 Model().newName/newVisualId
    void RequestStartDelete(int index);
    void RequestConfirmDelete();    // 用 Model().deleteConfirmText
    void RequestCancelDelete();
    void RequestEnterWorld(int index);
    void RequestSelectCharacter(int index);
    void RequestLeaveWorld();
    void RequestReturnToLogin();
    void RequestFocusField(FlowField field);
    // Esc 收编（指令三十五）：注册页→登录页；创建页→大厅；确认框→关闭；其余无操作。
    void HandleEsc();

    // 文本输入（GameScene 每帧喂入；作用于当前焦点字段）。
    void FeedTextInput(const std::vector<std::string>& utf8Chunks, bool backspace);

private:
    void DeriveState();
    void GoState(ClientFlowState state);
    void MarkError();
    [[nodiscard]] bool CanSend() const; // 网络就绪且不 busy

    legend::client::GameNetworkClient& m_net;
    legend::client::AccountClientController& m_account;
    legend::client::WorldClientController& m_world;
    legend::client::CharacterSelectionController& m_selection;

    ClientFlowState m_state = ClientFlowState::Boot;
    FlowUiModel m_model;
    bool m_observerOnly = false;

    // 一次性意图守卫（防重复提交；状态离开后复位）。
    bool m_connectRequested = false;
    bool m_listRequested = false;
    bool m_leaveRequested = false;
    float m_bootElapsed = 0.0f;
    float m_connectElapsed = 0.0f;
    float m_lastDt = 0.0f;

    // 页面内嵌错误记忆（进入新页面时清零）。
    legend::client::AccountFlowState m_prevAccountState =
        legend::client::AccountFlowState::Disconnected;
    legend::client::WorldFlowState m_prevWorldState = legend::client::WorldFlowState::Disconnected;

    // 注册/创建页面的"已提交"观测位（区分页面停留与服务器往返完成）。
    bool m_sawRegistering = false;
    bool m_sawCreating = false;
};

} // namespace legend::flow
