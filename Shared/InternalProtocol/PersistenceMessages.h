#pragma once

#include "Shared/Account/CharacterTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::internal {

struct AccountCredentials { std::string username; std::string password; };
struct AccountRegisterResult { std::uint64_t accountId = 0; };
struct AccountLoginResult {
    std::uint64_t accountId = 0;
    std::string sessionToken;
    std::int64_t expiresAt = 0;
};
struct ValidateSessionRequest { std::string sessionToken; };
struct ValidateSessionResult { std::uint64_t accountId = 0; std::int64_t expiresAt = 0; };
struct CharacterListQuery { std::uint64_t accountId = 0; };
struct CharacterCreateCommand {
    std::uint64_t accountId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
};
struct CharacterCommand { std::uint64_t accountId = 0; std::uint64_t characterId = 0; };

bool EncodeAccountCredentials(const AccountCredentials& value, std::vector<std::uint8_t>& out);
bool DecodeAccountCredentials(const std::uint8_t* data, std::size_t size,
                              AccountCredentials& out, std::string& error);
bool EncodeAccountRegisterResult(const AccountRegisterResult& value,
                                 std::vector<std::uint8_t>& out);
bool DecodeAccountRegisterResult(const std::uint8_t* data, std::size_t size,
                                 AccountRegisterResult& out, std::string& error);
bool EncodeAccountLoginResult(const AccountLoginResult& value, std::vector<std::uint8_t>& out);
bool DecodeAccountLoginResult(const std::uint8_t* data, std::size_t size,
                              AccountLoginResult& out, std::string& error);

bool EncodeValidateSessionRequest(const ValidateSessionRequest& value,
                                  std::vector<std::uint8_t>& out);
bool DecodeValidateSessionRequest(const std::uint8_t* data, std::size_t size,
                                  ValidateSessionRequest& out, std::string& error);
bool EncodeValidateSessionResult(const ValidateSessionResult& value,
                                 std::vector<std::uint8_t>& out);
bool DecodeValidateSessionResult(const std::uint8_t* data, std::size_t size,
                                 ValidateSessionResult& out, std::string& error);
bool EncodeCharacterListQuery(const CharacterListQuery& value, std::vector<std::uint8_t>& out);
bool DecodeCharacterListQuery(const std::uint8_t* data, std::size_t size,
                              CharacterListQuery& out, std::string& error);
bool EncodeCharacterCreateCommand(const CharacterCreateCommand& value,
                                  std::vector<std::uint8_t>& out);
bool DecodeCharacterCreateCommand(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateCommand& out, std::string& error);
bool EncodeCharacterCommand(const CharacterCommand& value, std::vector<std::uint8_t>& out);
bool DecodeCharacterCommand(const std::uint8_t* data, std::size_t size,
                            CharacterCommand& out, std::string& error);
bool EncodeCharacterSummary(const legend::account::CharacterSummary& value,
                            std::vector<std::uint8_t>& out);
bool DecodeCharacterSummary(const std::uint8_t* data, std::size_t size,
                            legend::account::CharacterSummary& out, std::string& error);
bool EncodeCharacterList(const std::vector<legend::account::CharacterSummary>& value,
                         std::vector<std::uint8_t>& out);
bool DecodeCharacterList(const std::uint8_t* data, std::size_t size,
                         std::vector<legend::account::CharacterSummary>& out,
                         std::string& error);

} // namespace legend::internal
