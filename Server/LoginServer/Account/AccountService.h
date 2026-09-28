#pragma once

#include "Server/LoginServer/Account/AccountRepository.h"

#include <cstdint>
#include <string>

namespace legend::account {

struct LoginOutcome {
    std::uint64_t accountId = 0;
    std::string sessionToken;
    std::int64_t sessionExpiresAt = 0;
};

// 阶段10：注册 / 登录应用逻辑（校验 -> Argon2id -> Repository -> Session）。
// 防暴力（指令二十九）：连续失败 >= maxFailedLogins -> 固定 lockoutSeconds 锁定。
class AccountService {
public:
    AccountService(int maxFailedLogins = 5, std::int64_t lockoutSeconds = 60);

    RepositoryResult<std::uint64_t> Register(Database& db, const std::string& username,
                                             const std::string& password) const;
    RepositoryResult<LoginOutcome> Login(Database& db, const std::string& username,
                                         const std::string& password,
                                         std::int64_t sessionTtlSeconds) const;

private:
    int m_maxFailedLogins;
    std::int64_t m_lockoutSeconds;
};

} // namespace legend::account
