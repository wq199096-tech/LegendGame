#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令十二：ClientFlowState —— 客户端玩家流程状态机（集中管理）。
// 禁止在场景/渲染层用零散 bool 拼接流程；页面归属由本状态唯一决定。
//   Boot           启动画面（Splash，短暂展示后自动连接）
//   Connecting     连接网关/握手中
//   Login          登录页
//   Register       注册页
//   CharacterLobby 角色大厅
//   CharacterCreate 创建角色页
//   EnteringWorld  进入世界加载中（选角后 / 重连后）
//   InWorld        游戏内（正式世界渲染/HUD）
//   Disconnected   断线页（可重试连接）
//   FatalError     致命错误页（资源缺失等不可恢复问题）
// ---------------------------------------------------------------------------

#include <cstdint>

namespace legend::flow {

enum class ClientFlowState : std::uint8_t {
    Boot,
    Connecting,
    Login,
    Register,
    CharacterLobby,
    CharacterCreate,
    EnteringWorld,
    InWorld,
    Disconnected,
    FatalError,
};

const char* ClientFlowStateName(ClientFlowState state);

} // namespace legend::flow
