#pragma once

#include "Shared/Account/AccountError.h"
#include "Shared/Account/CharacterTypes.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::account {

// 阶段10 指令十九：Account MessageId（与阶段9 无冲突；具体编号见
// Shared/Network/MessageId.h）。所有请求携带 requestId（指令一百零一），
// Response 原样回传，防止并发请求串响应。

// 阶段10 指令二十一/二十二：注册。
struct RegisterRequestPayload {
    std::uint64_t requestId = 0;
    std::string username;
    std::string password;
};

struct RegisterResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::uint16_t errorCode = 0; // AccountErrorCode
    std::string message;
};

// 阶段10 指令二十五/二十六：账号登录。
struct AccountLoginRequestPayload {
    std::uint64_t requestId = 0;
    std::string username;
    std::string password;
};

struct AccountLoginResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::string sessionToken;      // 原始 token 只回给客户端（数据库只存 hash，指令三十）
    std::int64_t expiresAt = 0;    // unix 秒
    std::uint16_t errorCode = 0;
    std::string message;
};

// 阶段10 指令三十三：Session Resume。
struct SessionResumeRequestPayload {
    std::uint64_t requestId = 0;
    std::string sessionToken;
};

struct SessionResumeResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::int64_t expiresAt = 0;
    std::uint16_t errorCode = 0;
    std::string message;
};

// 阶段10 指令四十二/四十三：角色列表。
struct CharacterListRequestPayload {
    std::uint64_t requestId = 0;
    std::string sessionToken;
};

struct CharacterListResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::vector<CharacterSummary> characters;
    std::uint16_t errorCode = 0;
    std::string message;
};

// 阶段10 指令三十九/四十：创建角色。
struct CharacterCreateRequestPayload {
    std::uint64_t requestId = 0;
    std::string sessionToken;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
};

struct CharacterCreateResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    CharacterSummary character;
    std::uint16_t errorCode = 0;
    std::string message;
};

// 阶段10 指令四十六/四十七：删除角色（soft delete）。
struct CharacterDeleteRequestPayload {
    std::uint64_t requestId = 0;
    std::string sessionToken;
    std::uint64_t characterId = 0;
};

struct CharacterDeleteResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t characterId = 0;
    std::uint16_t errorCode = 0;
    std::string message;
};

// 阶段10 指令四十九/五十：选择角色（返回 selectionTicket，阶段10 不连 WorldServer）。
struct CharacterSelectRequestPayload {
    std::uint64_t requestId = 0;
    std::string sessionToken;
    std::uint64_t characterId = 0;
};

struct CharacterSelectResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    CharacterSummary character;
    std::string selectionTicket;
    std::uint16_t errorCode = 0;
    std::string message;
};

// ---- Gateway <-> LoginServer 内部信封（Client 的 Account 包原样透传，
// Gateway 不解析业务 payload —— 指令五十四：不碰密码/角色表/SQLite） ----
struct AccountEnvelope {
    std::uint64_t requestId = 0;
    std::uint64_t clientConnectionId = 0;
    std::uint16_t innerMessageId = 0;
    std::vector<std::uint8_t> innerPayload; // 原始业务 payload（不解析）
};

// ---- 编解码 ----
bool EncodeRegisterRequest(const RegisterRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeRegisterRequest(const std::uint8_t* data, std::size_t size,
                           RegisterRequestPayload& out, std::string& error);
bool EncodeRegisterResponse(const RegisterResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeRegisterResponse(const std::uint8_t* data, std::size_t size,
                            RegisterResponsePayload& out, std::string& error);
bool EncodeAccountLoginRequest(const AccountLoginRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeAccountLoginRequest(const std::uint8_t* data, std::size_t size,
                               AccountLoginRequestPayload& out, std::string& error);
bool EncodeAccountLoginResponse(const AccountLoginResponsePayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeAccountLoginResponse(const std::uint8_t* data, std::size_t size,
                                AccountLoginResponsePayload& out, std::string& error);
bool EncodeSessionResumeRequest(const SessionResumeRequestPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeSessionResumeRequest(const std::uint8_t* data, std::size_t size,
                                SessionResumeRequestPayload& out, std::string& error);
bool EncodeSessionResumeResponse(const SessionResumeResponsePayload& p,
                                 std::vector<std::uint8_t>& out);
bool DecodeSessionResumeResponse(const std::uint8_t* data, std::size_t size,
                                 SessionResumeResponsePayload& out, std::string& error);
bool EncodeCharacterListRequest(const CharacterListRequestPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeCharacterListRequest(const std::uint8_t* data, std::size_t size,
                                CharacterListRequestPayload& out, std::string& error);
bool EncodeCharacterListResponse(const CharacterListResponsePayload& p,
                                 std::vector<std::uint8_t>& out);
bool DecodeCharacterListResponse(const std::uint8_t* data, std::size_t size,
                                 CharacterListResponsePayload& out, std::string& error);
bool EncodeCharacterCreateRequest(const CharacterCreateRequestPayload& p,
                                  std::vector<std::uint8_t>& out);
bool DecodeCharacterCreateRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterCreateRequestPayload& out, std::string& error);
bool EncodeCharacterCreateResponse(const CharacterCreateResponsePayload& p,
                                   std::vector<std::uint8_t>& out);
bool DecodeCharacterCreateResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterCreateResponsePayload& out, std::string& error);
bool EncodeCharacterDeleteRequest(const CharacterDeleteRequestPayload& p,
                                  std::vector<std::uint8_t>& out);
bool DecodeCharacterDeleteRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterDeleteRequestPayload& out, std::string& error);
bool EncodeCharacterDeleteResponse(const CharacterDeleteResponsePayload& p,
                                   std::vector<std::uint8_t>& out);
bool DecodeCharacterDeleteResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterDeleteResponsePayload& out, std::string& error);
bool EncodeCharacterSelectRequest(const CharacterSelectRequestPayload& p,
                                  std::vector<std::uint8_t>& out);
bool DecodeCharacterSelectRequest(const std::uint8_t* data, std::size_t size,
                                  CharacterSelectRequestPayload& out, std::string& error);
bool EncodeCharacterSelectResponse(const CharacterSelectResponsePayload& p,
                                   std::vector<std::uint8_t>& out);
bool DecodeCharacterSelectResponse(const std::uint8_t* data, std::size_t size,
                                   CharacterSelectResponsePayload& out, std::string& error);

bool EncodeAccountEnvelope(const AccountEnvelope& envelope, std::vector<std::uint8_t>& out);
bool DecodeAccountEnvelope(const std::uint8_t* data, std::size_t size,
                           AccountEnvelope& out, std::string& error);

// CharacterSummary wire（列表/创建/选择共用）：u64 id + name + u16 class +
// u16 gender + u32 level + u16 mapId + i64 lastPlayedAt
void WriteCharacterSummary(legend::network::ByteWriter& writer,
                           const CharacterSummary& summary);
bool ReadCharacterSummary(legend::network::ByteReader& reader, CharacterSummary& out);

} // namespace legend::account
