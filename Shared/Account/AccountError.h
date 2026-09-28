#pragma once

#include <cstdint>

namespace legend::account {

// 阶段10 指令二十三/二十七：账号/角色统一错误码（uint16 wire）。
// 客户端只收到本错误码 + message；SQLite 内部错误细节仅进服务端日志（指令六十八）。
enum class AccountErrorCode : std::uint16_t {
    None = 0,
    // 注册（指令二十三）
    InvalidUsername = 1,
    InvalidPassword = 2,
    UsernameAlreadyExists = 3,
    DatabaseError = 4,
    InternalError = 5,
    // 登录（指令二十七）
    InvalidCredentials = 10,
    AccountDisabled = 11,
    AccountBanned = 12,
    TooManyAttempts = 13,
    // Session（指令三十三）
    SessionInvalid = 20,
    // 角色
    InvalidCharacterName = 30,
    CharacterNameTaken = 31,
    CharacterLimitReached = 32,
    CharacterNotFound = 33,
    CharacterNotOwned = 34,
    // Gateway 侧（重复请求 / 超时 / 服务不可用）
    RequestPending = 40,
    RequestTimeout = 41,
    ServiceUnavailable = 42,
};

inline const char* AccountErrorCodeName(std::uint16_t code) {
    switch (static_cast<AccountErrorCode>(code)) {
        case AccountErrorCode::None: return "None";
        case AccountErrorCode::InvalidUsername: return "InvalidUsername";
        case AccountErrorCode::InvalidPassword: return "InvalidPassword";
        case AccountErrorCode::UsernameAlreadyExists: return "UsernameAlreadyExists";
        case AccountErrorCode::DatabaseError: return "DatabaseError";
        case AccountErrorCode::InternalError: return "InternalError";
        case AccountErrorCode::InvalidCredentials: return "InvalidCredentials";
        case AccountErrorCode::AccountDisabled: return "AccountDisabled";
        case AccountErrorCode::AccountBanned: return "AccountBanned";
        case AccountErrorCode::TooManyAttempts: return "TooManyAttempts";
        case AccountErrorCode::SessionInvalid: return "SessionInvalid";
        case AccountErrorCode::InvalidCharacterName: return "InvalidCharacterName";
        case AccountErrorCode::CharacterNameTaken: return "CharacterNameTaken";
        case AccountErrorCode::CharacterLimitReached: return "CharacterLimitReached";
        case AccountErrorCode::CharacterNotFound: return "CharacterNotFound";
        case AccountErrorCode::CharacterNotOwned: return "CharacterNotOwned";
        case AccountErrorCode::RequestPending: return "RequestPending";
        case AccountErrorCode::RequestTimeout: return "RequestTimeout";
        case AccountErrorCode::ServiceUnavailable: return "ServiceUnavailable";
    }
    return "Unknown";
}

} // namespace legend::account
