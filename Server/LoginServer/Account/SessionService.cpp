#include "Server/LoginServer/Account/SessionService.h"

#include "Server/LoginServer/Account/PasswordHasher.h"

namespace legend::account {

RepositoryResult<SessionInfo> SessionService::Create(Database& db, std::uint64_t accountId,
                                                     std::int64_t ttlSeconds) const {
    RepositoryResult<SessionInfo> result;
    const std::string token = GenerateTokenHex(32); // 256-bit（指令三十）
    if (token.empty()) {
        result.errorCode = AccountErrorCode::InternalError;
        result.errorMessage = "session token generation failed";
        return result;
    }
    const std::int64_t expiresAt = UnixNow() + ttlSeconds;
    auto created = SessionRepository::CreateSession(db, accountId, Sha256Hex(token), expiresAt);
    if (!created.success) {
        result.errorCode = created.errorCode;
        result.errorMessage = created.errorMessage;
        return result;
    }
    result.success = true;
    result.value.token = std::move(token);
    result.value.expiresAt = expiresAt;
    return result;
}

RepositoryResult<SessionResumeInfo> SessionService::Resume(Database& db,
                                                           const std::string& token) const {
    RepositoryResult<SessionResumeInfo> result;
    result.errorCode = AccountErrorCode::SessionInvalid;
    result.errorMessage = "session resume failed";
    if (token.empty()) {
        return result;
    }
    auto found = SessionRepository::FindSessionByTokenHash(db, Sha256Hex(token));
    if (!found.success) {
        result.errorCode = found.errorCode;
        result.errorMessage = found.errorMessage;
        return result;
    }
    if (!found.value.has_value()) {
        return result; // SessionInvalid
    }
    const SessionRow& row = *found.value;
    // 指令三十三：存在 + 未 revoked + 未过期 才成功。
    if (row.revoked) {
        return result;
    }
    if (row.expiresAt <= UnixNow()) {
        return result;
    }
    result.success = true;
    result.value.accountId = row.accountId;
    result.value.expiresAt = row.expiresAt;
    return result;
}

RepositoryResult<int> SessionService::Revoke(Database& db, std::uint64_t sessionId) const {
    return SessionRepository::RevokeSession(db, sessionId);
}

} // namespace legend::account
