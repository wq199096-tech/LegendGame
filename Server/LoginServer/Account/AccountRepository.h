#pragma once

#include "Server/LoginServer/Account/Database/Database.h"

#include "Shared/Account/AccountError.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace legend::account {

// 阶段10 指令六十七：Repository 统一返回值（禁止 bool everywhere）。
// errorMessage 只进服务端日志，不直接下发客户端（指令六十八）。
template <typename T>
struct RepositoryResult {
    bool success = false;
    T value{};
    AccountErrorCode errorCode = AccountErrorCode::None;
    std::string errorMessage;
};

// 阶段10 指令七：accounts 表行（不含密码原文，仅 hash）。
struct AccountRow {
    std::uint64_t id = 0;
    std::string username;
    std::string passwordHash;
    std::int64_t createdAt = 0;
    std::int64_t lastLoginAt = 0; // 0 = 从未登录
    std::uint16_t status = 0;     // AccountStatus
    std::int64_t failedLoginCount = 0;
    std::int64_t lockedUntil = 0; // 0 = 未锁定（unix 秒）
};

// 阶段10 指令九：sessions 表行。
struct SessionRow {
    std::uint64_t id = 0;
    std::uint64_t accountId = 0;
    std::string tokenHash;
    std::int64_t createdAt = 0;
    std::int64_t expiresAt = 0;
    bool revoked = false;
};

// 阶段10 指令八：characters 表行。
struct CharacterRow {
    std::uint64_t id = 0;
    std::uint64_t accountId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::int64_t exp = 0;
    std::uint16_t mapId = 1;
    double positionX = 0.0;
    double positionY = 0.0;
    std::int64_t createdAt = 0;
    std::int64_t lastPlayedAt = 0;
    bool deleted = false;
};

namespace AccountRepository {

RepositoryResult<std::uint64_t> CreateAccount(Database& db, const std::string& username,
                                              const std::string& passwordHash);
RepositoryResult<std::optional<AccountRow>> FindAccountByUsername(Database& db,
                                                                  const std::string& username);
// 登录成功：failed_login_count = 0，locked_until 清空，last_login_at = now（指令二十八）。
RepositoryResult<int> UpdateLoginSuccess(Database& db, std::uint64_t accountId);
// 登录失败：failed_login_count += 1；达到阈值后设置 locked_until（指令二十九）。
RepositoryResult<std::int64_t> UpdateLoginFailure(Database& db, std::uint64_t accountId,
                                                  int maxFailedLogins,
                                                  std::int64_t lockoutSeconds);

} // namespace AccountRepository

namespace SessionRepository {

// 阶段10 指令三十：数据库只保存 token hash（SHA-256 hex），绝不保存原始 token。
RepositoryResult<std::uint64_t> CreateSession(Database& db, std::uint64_t accountId,
                                              const std::string& tokenHash,
                                              std::int64_t expiresAt);
RepositoryResult<std::optional<SessionRow>> FindSessionByTokenHash(Database& db,
                                                                   const std::string& tokenHash);
RepositoryResult<int> RevokeSession(Database& db, std::uint64_t sessionId);

} // namespace SessionRepository

namespace CharacterRepository {

// 阶段10 指令九十五/九十六：数量检查 + INSERT 必须在同一事务内（并发不超上限）。
RepositoryResult<CharacterRow> CreateCharacter(Database& db, std::uint64_t accountId,
                                               const std::string& name, std::uint16_t classId,
                                               std::uint16_t gender,
                                               std::size_t maxCharactersPerAccount);
RepositoryResult<std::vector<CharacterRow>> ListCharactersByAccount(Database& db,
                                                                    std::uint64_t accountId);
RepositoryResult<std::optional<CharacterRow>> FindCharacterById(Database& db,
                                                                std::uint64_t characterId);
// 阶段10 指令四十五：soft delete（deleted = 1，禁止物理删除）。
RepositoryResult<int> SoftDeleteCharacter(Database& db, std::uint64_t characterId);
// 阶段10 指令四十四：选择角色即视为游玩（列表排序依据）。
RepositoryResult<int> UpdateLastPlayed(Database& db, std::uint64_t characterId);

} // namespace CharacterRepository

} // namespace legend::account
