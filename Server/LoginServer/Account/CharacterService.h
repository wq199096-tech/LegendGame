#pragma once

#include "Server/LoginServer/Account/AccountRepository.h"

#include "Shared/Account/CharacterTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::account {

// 阶段10：角色列表 / 创建 / 删除（soft）/ 选择。
// 所有权校验（指令四十六/四十九）：character 必须属于当前 account。
class CharacterService {
public:
    explicit CharacterService(std::size_t maxCharactersPerAccount = kMaxCharactersPerAccount);

    RepositoryResult<std::vector<CharacterSummary>> List(Database& db,
                                                         std::uint64_t accountId) const;
    // 阶段26 指令十一：Create 增加 visualId（1~3 造型槽位，服务器权威校验）。
    RepositoryResult<CharacterSummary> Create(Database& db, std::uint64_t accountId,
                                              const std::string& name, std::uint16_t classId,
                                              std::uint16_t gender,
                                              std::uint16_t visualId) const;
    RepositoryResult<std::uint64_t> Delete(Database& db, std::uint64_t accountId,
                                           std::uint64_t characterId) const;
    RepositoryResult<CharacterSummary> Select(Database& db, std::uint64_t accountId,
                                              std::uint64_t characterId) const;

private:
    static CharacterSummary ToSummary(const CharacterRow& row);

    std::size_t m_maxCharactersPerAccount;
};

} // namespace legend::account
