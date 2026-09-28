#include "Server/LoginServer/Account/AccountService.h"

#include "Server/LoginServer/Account/PasswordHasher.h"
#include "Server/LoginServer/Account/SessionService.h"
#include "Shared/Account/AccountTypes.h"

namespace legend::account {

AccountService::AccountService(int maxFailedLogins, std::int64_t lockoutSeconds)
    : m_maxFailedLogins(maxFailedLogins), m_lockoutSeconds(lockoutSeconds) {}

RepositoryResult<std::uint64_t> AccountService::Register(Database& db,
                                                         const std::string& username,
                                                         const std::string& password) const {
    RepositoryResult<std::uint64_t> result;
    // 阶段10 指令十二/十三：入参校验（Username/Password 规则）。
    if (!IsValidUsername(username)) {
        result.errorCode = AccountErrorCode::InvalidUsername;
        result.errorMessage = "invalid username";
        return result;
    }
    if (!IsValidPassword(password)) {
        result.errorCode = AccountErrorCode::InvalidPassword;
        result.errorMessage = "invalid password";
        return result;
    }
    // 阶段10 指令十三/十四：Argon2id（含 salt + 参数），绝不存明文。
    std::string hash;
    std::string hashError;
    if (!PasswordHasher::Hash(password, hash, hashError)) {
        result.errorCode = AccountErrorCode::InternalError;
        result.errorMessage = hashError;
        return result;
    }
    auto created = AccountRepository::CreateAccount(db, username, hash);
    if (!created.success) {
        result.errorCode = created.errorCode;
        result.errorMessage = created.errorMessage;
        return result;
    }
    result.success = true;
    result.value = created.value;
    return result;
}

RepositoryResult<LoginOutcome> AccountService::Login(Database& db, const std::string& username,
                                                     const std::string& password,
                                                     std::int64_t sessionTtlSeconds) const {
    RepositoryResult<LoginOutcome> result;
    result.errorCode = AccountErrorCode::InvalidCredentials;
    result.errorMessage = "invalid username or password";
    if (username.empty() || password.empty()) {
        return result;
    }
    auto found = AccountRepository::FindAccountByUsername(db, username);
    if (!found.success) {
        result.errorCode = found.errorCode;
        result.errorMessage = found.errorMessage;
        return result;
    }
    if (!found.value.has_value()) {
        return result; // 用户不存在与密码错误同码（不泄露账号存在性）
    }
    const AccountRow& account = *found.value;

    // 阶段10 指令十六：状态判断（阶段10 不做 GM 管理界面）。
    if (account.status == static_cast<std::uint16_t>(AccountStatus::Disabled)) {
        result.errorCode = AccountErrorCode::AccountDisabled;
        result.errorMessage = "account disabled";
        return result;
    }
    if (account.status == static_cast<std::uint16_t>(AccountStatus::Banned)) {
        result.errorCode = AccountErrorCode::AccountBanned;
        result.errorMessage = "account banned";
        return result;
    }
    // 阶段10 指令二十九：锁定中直接拒绝（TooManyAttempts）。
    if (account.lockedUntil > UnixNow()) {
        result.errorCode = AccountErrorCode::TooManyAttempts;
        result.errorMessage = "account temporarily locked";
        return result;
    }

    std::string verifyError;
    if (!PasswordHasher::Verify(password, account.passwordHash, verifyError)) {
        if (!verifyError.empty()) {
            result.errorCode = AccountErrorCode::InternalError;
            result.errorMessage = verifyError;
            return result;
        }
        // 指令二十八/二十九：失败计数 +1，达到阈值进入短锁定。
        auto updated =
            AccountRepository::UpdateLoginFailure(db, account.id, m_maxFailedLogins, m_lockoutSeconds);
        if (!updated.success) {
            result.errorCode = updated.errorCode;
            result.errorMessage = updated.errorMessage;
            return result;
        }
        return result; // InvalidCredentials
    }

    auto reset = AccountRepository::UpdateLoginSuccess(db, account.id);
    if (!reset.success) {
        result.errorCode = reset.errorCode;
        result.errorMessage = reset.errorMessage;
        return result;
    }
    // 阶段10 指令三十/三十二：CSPRNG token + 默认 24h 有效期（测试可调短）。
    auto session = SessionService{}.Create(db, account.id, sessionTtlSeconds);
    if (!session.success) {
        result.errorCode = session.errorCode;
        result.errorMessage = session.errorMessage;
        return result;
    }
    result.success = true;
    result.value.accountId = account.id;
    result.value.sessionToken = std::move(session.value.token);
    result.value.sessionExpiresAt = session.value.expiresAt;
    return result;
}

} // namespace legend::account
