#include "Server/LoginServer/Account/CharacterService.h"

namespace legend::account {

CharacterService::CharacterService(std::size_t maxCharactersPerAccount)
    : m_maxCharactersPerAccount(maxCharactersPerAccount) {}

CharacterSummary CharacterService::ToSummary(const CharacterRow& row) {
    CharacterSummary summary;
    summary.characterId = row.id;
    summary.name = row.name;
    summary.classId = row.classId;
    summary.gender = row.gender;
    summary.level = row.level;
    summary.mapId = row.mapId;
    summary.lastPlayedAt = row.lastPlayedAt;
    summary.visualId = row.visualId;
    return summary;
}

RepositoryResult<std::vector<CharacterSummary>> CharacterService::List(
    Database& db, std::uint64_t accountId) const {
    RepositoryResult<std::vector<CharacterSummary>> result;
    auto rows = CharacterRepository::ListCharactersByAccount(db, accountId);
    if (!rows.success) {
        result.errorCode = rows.errorCode;
        result.errorMessage = rows.errorMessage;
        return result;
    }
    result.success = true;
    result.value.reserve(rows.value.size());
    for (const CharacterRow& row : rows.value) {
        result.value.push_back(ToSummary(row));
    }
    return result;
}

RepositoryResult<CharacterSummary> CharacterService::Create(Database& db, std::uint64_t accountId,
                                                            const std::string& name,
                                                            std::uint16_t classId,
                                                            std::uint16_t gender,
                                                            std::uint16_t visualId) const {
    RepositoryResult<CharacterSummary> result;
    // 阶段10 指令三十六/三十七/三十八 + 阶段26 指令十一：入参校验（服务器权威）。
    if (!IsValidCharacterName(name)) {
        result.errorCode = AccountErrorCode::InvalidCharacterName;
        result.errorMessage = "invalid character name";
        return result;
    }
    if (!IsValidClassId(classId)) {
        result.errorCode = AccountErrorCode::InvalidCharacterName;
        result.errorMessage = "invalid class id";
        return result;
    }
    if (!IsValidGenderId(gender)) {
        result.errorCode = AccountErrorCode::InvalidCharacterName;
        result.errorMessage = "invalid gender id";
        return result;
    }
    if (!IsValidVisualId(visualId)) {
        result.errorCode = AccountErrorCode::InvalidCharacterName;
        result.errorMessage = "invalid visual id";
        return result;
    }
    auto created =
        CharacterRepository::CreateCharacter(db, accountId, name, classId, gender, visualId,
                                             m_maxCharactersPerAccount);
    if (!created.success) {
        result.errorCode = created.errorCode;
        result.errorMessage = created.errorMessage;
        return result;
    }
    result.success = true;
    result.value = ToSummary(created.value);
    return result;
}

RepositoryResult<std::uint64_t> CharacterService::Delete(Database& db, std::uint64_t accountId,
                                                         std::uint64_t characterId) const {
    RepositoryResult<std::uint64_t> result;
    result.errorCode = AccountErrorCode::CharacterNotFound;
    result.errorMessage = "character not found";
    auto found = CharacterRepository::FindCharacterById(db, characterId);
    if (!found.success) {
        result.errorCode = found.errorCode;
        result.errorMessage = found.errorMessage;
        return result;
    }
    if (!found.value.has_value() || found.value->deleted) {
        return result; // CharacterNotFound（soft deleted 视为不存在）
    }
    if (found.value->accountId != accountId) {
        // 阶段10 指令四十六：禁止删除别人的角色。
        result.errorCode = AccountErrorCode::CharacterNotOwned;
        result.errorMessage = "character not owned";
        return result;
    }
    auto deleted = CharacterRepository::SoftDeleteCharacter(db, characterId);
    if (!deleted.success) {
        result.errorCode = deleted.errorCode;
        result.errorMessage = deleted.errorMessage;
        return result;
    }
    result.success = true;
    result.value = characterId;
    return result;
}

RepositoryResult<CharacterSummary> CharacterService::Select(Database& db, std::uint64_t accountId,
                                                            std::uint64_t characterId) const {
    RepositoryResult<CharacterSummary> result;
    result.errorCode = AccountErrorCode::CharacterNotFound;
    result.errorMessage = "character not found";
    auto found = CharacterRepository::FindCharacterById(db, characterId);
    if (!found.success) {
        result.errorCode = found.errorCode;
        result.errorMessage = found.errorMessage;
        return result;
    }
    if (!found.value.has_value() || found.value->deleted) {
        return result;
    }
    if (found.value->accountId != accountId) {
        // 阶段10 指令四十九：角色必须属于当前 account。
        result.errorCode = AccountErrorCode::CharacterNotOwned;
        result.errorMessage = "character not owned";
        return result;
    }
    auto touched = CharacterRepository::UpdateLastPlayed(db, characterId);
    if (!touched.success) {
        result.errorCode = touched.errorCode;
        result.errorMessage = touched.errorMessage;
        return result;
    }
    result.success = true;
    result.value = ToSummary(*found.value);
    return result;
}

} // namespace legend::account
