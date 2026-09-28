#pragma once

#include "Server/LoginServer/Account/AccountRepository.h"

#include <cstdint>
#include <string>

namespace legend::account {

struct SessionInfo {
    std::string token;      // 原始 token（只回给客户端，不落库不落日志）
    std::int64_t expiresAt = 0;
};

struct SessionResumeInfo {
    std::uint64_t accountId = 0;
    std::int64_t expiresAt = 0;
};

// 阶段10 指令三十~三十四：Session 生成/恢复/吊销。
// - token：256-bit CSPRNG（randombytes_buf）
// - 数据库只存 SHA-256(token)
// - 单设备策略：允许多个有效 Session，不做踢下线
class SessionService {
public:
    RepositoryResult<SessionInfo> Create(Database& db, std::uint64_t accountId,
                                         std::int64_t ttlSeconds) const;
    RepositoryResult<SessionResumeInfo> Resume(Database& db, const std::string& token) const;
    RepositoryResult<int> Revoke(Database& db, std::uint64_t sessionId) const;
};

} // namespace legend::account
