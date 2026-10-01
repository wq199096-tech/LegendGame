#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令二十九：PlayerFacingErrorCatalog —— 服务器错误码 -> 玩家可读
// 简体中文文案（登录/注册/角色大厅等页面统一使用；禁止把英文服务器消息直出）。
// 纯数据层（LegendClientUi，不依赖 SDL/OpenGL；测试可链接）。
// ---------------------------------------------------------------------------

#include <cstdint>

namespace legend::ui {

// AccountErrorCode（Shared/Account/AccountError.h）-> 简体中文。
// 未知码返回通用兜底文案（绝不返回空串）。
const char* PlayerFacingAccountErrorText(std::uint16_t code);

} // namespace legend::ui
