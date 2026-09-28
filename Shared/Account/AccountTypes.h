#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace legend::account {

// 阶段10 指令十六：账号状态（阶段10 只做状态判断，不做 GM 管理界面）。
enum class AccountStatus : std::uint16_t {
    Active = 0,
    Disabled = 1,
    Banned = 2,
};

// 阶段10 指令十二：username 3~20 字符，只允许 A-Z a-z 0-9 _。
inline constexpr std::size_t kUsernameMinLength = 3;
inline constexpr std::size_t kUsernameMaxLength = 20;

// 阶段10 指令十三：password 8~64 字符（哈希前校验，绝不落明文）。
inline constexpr std::size_t kPasswordMinLength = 8;
inline constexpr std::size_t kPasswordMaxLength = 64;

// 阶段10 指令十二：统一保存原始 username，唯一性大小写不敏感
//（SQLite 列 COLLATE NOCASE，ASCII 语义与规则一致）。
inline bool IsValidUsername(const std::string& username) {
    if (username.size() < kUsernameMinLength || username.size() > kUsernameMaxLength) {
        return false;
    }
    for (const char c : username) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// 阶段10 指令十三：8~64 字符（按字节计；不限制字符集，长度即规则）。
inline bool IsValidPassword(const std::string& password) {
    return password.size() >= kPasswordMinLength && password.size() <= kPasswordMaxLength;
}

} // namespace legend::account
