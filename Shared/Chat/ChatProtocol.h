#pragma once

// ---------------------------------------------------------------------------
// Stage27 指令十三：聊天协议 payload（Client <-> WorldServer；经 Gateway 透传）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令六十三惯例）。
// 服务器权威：sender 身份由 WorldSession 确定（指令十七）——客户端只能发送
// requestId/channel/targetName/text，绝不接受客户端上报的 senderName/senderId。
// ---------------------------------------------------------------------------

#include "Shared/Chat/ChatConstants.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::chat {

// ChatSendRequest(350)：客户端 -> 服务器。
struct ChatSendRequestPayload {
    std::uint64_t requestId = 0;
    std::uint8_t channel = static_cast<std::uint8_t>(ChatChannel::Nearby);
    std::string targetName; // 仅 Whisper 使用；其它频道必须为空
    std::string text;       // <= 120 码点（服务器权威校验）
};

// ChatSendResponse(351)：服务器 -> 客户端（提交结果；失败带中文可读文案）。
struct ChatSendResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint16_t errorCode = static_cast<std::uint16_t>(ChatErrorCode::None);
    std::string message; // 玩家可读中文（空 = 无）
};

// ChatMessageEvent(352)：服务器 -> 客户端（投递聊天消息；客户端只展示）。
struct ChatMessageEventPayload {
    std::uint64_t messageId = 0;     // 服务器单调计数（排序/去重）
    std::uint8_t channel = static_cast<std::uint8_t>(ChatChannel::Nearby);
    std::uint64_t senderCharacterId = 0; // System 消息为 0
    std::string senderName;              // System 消息为空；禁止发账号名（指令四）
    std::string targetName;              // 仅 Whisper 使用
    std::string text;
    std::uint64_t timestamp = 0;         // Unix 毫秒
};

// 指令十八：聊天审计记录（WorldServer -> LogServer LogEvent；不含密码/Token/Ticket）。
struct ChatAuditRecord {
    std::uint64_t timestampMs = 0;
    std::uint64_t senderCharacterId = 0;
    std::string senderName;
    std::uint8_t channel = 0;
    std::string targetName;
    std::string text;
};

bool EncodeChatSendRequest(const ChatSendRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeChatSendRequest(const std::uint8_t* data, std::size_t size,
                           ChatSendRequestPayload& out, std::string& error);
bool EncodeChatSendResponse(const ChatSendResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeChatSendResponse(const std::uint8_t* data, std::size_t size,
                            ChatSendResponsePayload& out, std::string& error);
bool EncodeChatMessageEvent(const ChatMessageEventPayload& p, std::vector<std::uint8_t>& out);
bool DecodeChatMessageEvent(const std::uint8_t* data, std::size_t size,
                            ChatMessageEventPayload& out, std::string& error);

} // namespace legend::chat
