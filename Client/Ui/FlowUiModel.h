#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令二十八：FlowUiModel —— 登录/注册/角色大厅/创建/删除确认页面的
// 纯数据模型（LegendClientUi，不依赖 SDL/OpenGL）。
// 渲染层（VisualRuntime::RenderFlowPages）只读绘制 + 命中测试产生 Action；
// 文本编辑（UTF-8 码点安全）走 flowui 纯函数，测试可脱离窗口系统验证。
// 密码/Token 绝不落日志（指令三十）；client_login.json 只存账号名。
// ---------------------------------------------------------------------------

#include "Client/Flow/ClientFlowState.h"
#include "Shared/Account/CharacterTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::flow {

// 页面按钮/交互动作（渲染层收集 -> GameScene 每帧 Drain -> ClientFlowController）。
struct FlowUiAction {
    enum class Kind : std::uint8_t {
        None,
        RetryConnect,     // 断线页：重试连接
        SubmitLogin,      // 登录页：提交
        GoRegister,       // 登录页：去注册
        SubmitRegister,   // 注册页：提交
        CancelRegister,   // 注册页：返回登录
        EnterWorld,       // 大厅：进入游戏（index = 角色列表下标）
        OpenCreate,       // 大厅：创建角色
        StartDelete,      // 大厅：发起删除（index；进入二次确认）
        ConfirmDelete,    // 确认框：确认删除
        CancelDelete,     // 确认框：取消
        SubmitCreate,     // 创建页：提交
        CancelCreate,     // 创建页：返回大厅
        LeaveWorld,       // 游戏内（设置菜单）：返回角色大厅
        ReturnToLogin,    // 大厅：退出登录
        FocusField,       // 点击文本框（index = 字段 id）
        SelectVisual,     // 创建页：选造型（index = visualId-1）
        SelectCharacter,  // 大厅：点选角色卡片（index）
    };
    Kind kind = Kind::None;
    int index = 0;
};

// 页面文本字段 id（焦点管理；页面切换时重置）。
enum class FlowField : std::uint8_t {
    None = 0,
    LoginAccount = 1,
    LoginPassword = 2,
    RegAccount = 3,
    RegPassword = 4,
    RegPassword2 = 5,
    CreateName = 6,
    DeleteConfirm = 7,
};

struct FlowUiModel {
    ClientFlowState state = ClientFlowState::Boot;

    // ---- 登录/注册 ----
    std::string accountName;
    std::string password;
    std::string regAccount;
    std::string regPassword;
    std::string regPassword2;
    FlowField focusedField = FlowField::LoginAccount;
    bool busy = false; // 等待服务器响应（提交按钮禁用）

    // ---- 大厅 ----
    std::vector<legend::account::CharacterSummary> characters;
    int selectedIndex = -1;
    bool deleteConfirmOpen = false;
    std::string deleteConfirmText; // 需重输角色名
    std::string loginUserName;     // 大厅顶部显示（仅账号名；非敏感）

    // ---- 创建 ----
    std::string newName;
    std::uint16_t newVisualId = 1;

    // ---- 错误提示（页面内嵌；code=0 无错）----
    std::uint16_t lastErrorCode = 0;
    std::string lastErrorMessage; // 服务器附言（仅调试展示，主文案走错误码映射）

    // ---- 提示 ----
    float busyElapsed = 0.0f; // busy 动画计时

    // 本帧收集的动作（GameScene::Update 消费后清空）。
    std::vector<FlowUiAction> pendingActions;
    void PushAction(FlowUiAction::Kind kind, int index = 0) {
        pendingActions.push_back(FlowUiAction{kind, index});
    }
    void DrainActions(std::vector<FlowUiAction>& out) {
        out = std::move(pendingActions);
        pendingActions.clear();
    }
};

// ---- UTF-8 码点安全文本编辑（纯函数；CharacterNameValidationChecks 用）----
// 追加 chunk（完整码点才追加；截断的 UTF-8 尾部被丢弃），超出 maxCodepoints 拒绝。
void FlowTextAppend(std::string& text, const std::string& chunk, std::size_t maxCodepoints);
// 删除末尾一个码点（空串无操作）。
void FlowTextBackspace(std::string& text);
// 码点计数。
std::size_t FlowTextCodepointCount(const std::string& text);

} // namespace legend::flow
