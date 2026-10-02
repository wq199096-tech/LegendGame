#pragma once

// ---------------------------------------------------------------------------
// Stage27 指令八/十二/十四/十五/十六/十七/二十七：聊天常量与校验（Shared 唯一
// 定义，Client/Server 共用一套规则，禁止两套实现）。
//
// 纪律（Stage27 指令书）：
//   - 四频道：Nearby / World / Whisper / System；System 只能服务器产生，
//     客户端发送 channel=System 必须拒绝（指令十二）。
//   - 文本上限 = 120 UTF-8 码点（不是 120 字节，指令十四）。
//   - 禁止：控制字符（\r\n\t\0/ANSI escape/不可见控制码，指令二十七）、
//     非法 UTF-8、空消息、纯空格消息、超长消息（指令十四）。
//   - 附近频道半径 kNearbyChatRadius（默认 1200 world units，指令九）；
//     距离由服务器计算，客户端不能决定接收者。
//   - 防重放：requestId 纪律（指令十六）；防刷屏：服务器权威限流（指令十五）。
// ---------------------------------------------------------------------------

#include "Shared/Account/CharacterTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace legend::chat {

// 指令八：频道枚举（wire 上 uint8）。
enum class ChatChannel : std::uint8_t {
    Nearby = 1, // 附近：同地图 + 聊天距离内（服务器计算）
    World = 2,  // 世界：全部 InWorld 玩家（单 WorldServer 广播）
    Whisper = 3,// 私聊：/w 玩家名 内容；目标必须在线
    System = 4, // 系统：仅服务器产生；客户端发送必须拒绝
};

inline bool IsValidChatChannelValue(std::uint8_t channel) {
    switch (static_cast<ChatChannel>(channel)) {
        case ChatChannel::Nearby:
        case ChatChannel::World:
        case ChatChannel::Whisper:
        case ChatChannel::System:
            return true;
        default:
            return false;
    }
}

// 指令十二：客户端只能发送这三个频道；System 服务器专属。
inline bool IsClientSendableChannel(std::uint8_t channel) {
    switch (static_cast<ChatChannel>(channel)) {
        case ChatChannel::Nearby:
        case ChatChannel::World:
        case ChatChannel::Whisper:
            return true;
        default:
            return false;
    }
}

// 指令九：附近聊天半径（不复用 AOI 600 距离）。
inline constexpr float kNearbyChatRadius = 1200.0f;

// 指令十四：文本上限（Unicode 码点 + 字节双保险；BMP 中文每字 3 字节）。
inline constexpr std::size_t kChatMaxCodePoints = 120;
inline constexpr std::size_t kChatMaxBytes = 480;
// 私聊目标名 = 角色名规则（Stage26：2~12 码点）。
inline constexpr std::size_t kChatTargetNameMaxBytes = account::kCharacterNameMaxBytes;

// 指令十五：默认限流（服务器权威，客户端限流不是保护）。语义 = 窗口内允许条数：
//   附近 1 秒最多 2 条；世界 3 秒最多 1 条；私聊 1 秒最多 3 条；突发 10 秒最多 8 条。
// 配置非法时必须回退到这些默认值（禁止静默产生 0 窗口/0 条数，指令四十六）。
inline constexpr int kChatNearbyWindowMs = 1000;
inline constexpr int kChatNearbyMaxPerWindow = 2;
inline constexpr int kChatWorldWindowMs = 3000;
inline constexpr int kChatWorldMaxPerWindow = 1;
inline constexpr int kChatWhisperWindowMs = 1000;
inline constexpr int kChatWhisperMaxPerWindow = 3;
inline constexpr int kChatBurstWindowMs = 10000;
inline constexpr int kChatBurstMaxMessages = 8;

// 指令十六：防重放环形缓冲容量（与攻击/技能等请求历史一致：最近 64 个成功 requestId）。
inline constexpr std::size_t kChatRequestHistorySize = 64;

// 指令十七：错误码（wire 上 uint16）。
enum class ChatErrorCode : std::uint16_t {
    None = 0,
    Malformed = 1,             // 畸形包（截断/多余字节/非法长度）
    InvalidChannel = 2,        // 非法 channel 值
    SystemChannelForbidden = 3,// 伪造 System 频道（客户端发送）
    EmptyText = 4,             // 空消息 / 纯空格
    TextTooLong = 5,           // 超过 120 码点
    InvalidUtf8 = 6,           // 非法 UTF-8 / 控制字符
    TargetOffline = 7,         // 私聊目标不在线
    TargetInvalid = 8,         // 私聊目标名非法（空/超长/非法字符）
    RateLimited = 9,           // 发言过于频繁
    NotInWorld = 10,           // 未进入世界（状态机拒绝）
    DuplicateRequest = 11,     // 重复 requestId（不重复广播）
};

inline const char* ChatErrorCodeName(std::uint16_t code) {
    switch (static_cast<ChatErrorCode>(code)) {
        case ChatErrorCode::None: return "None";
        case ChatErrorCode::Malformed: return "Malformed";
        case ChatErrorCode::InvalidChannel: return "InvalidChannel";
        case ChatErrorCode::SystemChannelForbidden: return "SystemChannelForbidden";
        case ChatErrorCode::EmptyText: return "EmptyText";
        case ChatErrorCode::TextTooLong: return "TextTooLong";
        case ChatErrorCode::InvalidUtf8: return "InvalidUtf8";
        case ChatErrorCode::TargetOffline: return "TargetOffline";
        case ChatErrorCode::TargetInvalid: return "TargetInvalid";
        case ChatErrorCode::RateLimited: return "RateLimited";
        case ChatErrorCode::NotInWorld: return "NotInWorld";
        case ChatErrorCode::DuplicateRequest: return "DuplicateRequest";
    }
    return "Unknown";
}

// 指令十五：玩家可见中文文案（客户端 ChatSendResponse 失败提示统一走这里）。
inline const char* ChatErrorUserText(std::uint16_t code) {
    switch (static_cast<ChatErrorCode>(code)) {
        case ChatErrorCode::SystemChannelForbidden:
            return "系统频道只能由服务器发送";
        case ChatErrorCode::EmptyText:
            return "不能发送空消息";
        case ChatErrorCode::TextTooLong:
            return "消息过长（最多 120 字）";
        case ChatErrorCode::InvalidUtf8:
            return "消息包含非法字符";
        case ChatErrorCode::TargetOffline:
            return "该玩家当前不在线";
        case ChatErrorCode::TargetInvalid:
            return "私聊目标名非法";
        case ChatErrorCode::RateLimited:
            return "发言过于频繁，请稍后再试";
        case ChatErrorCode::InvalidChannel:
        case ChatErrorCode::NotInWorld:
        case ChatErrorCode::Malformed:
            return "消息发送失败";
        case ChatErrorCode::DuplicateRequest:
            return "";
        case ChatErrorCode::None:
            return "";
    }
    return "消息发送失败";
}

// ---------------------------------------------------------------------------
// 文本校验（指令十四/二十七；纯函数，测试可脱离网络验证）。
// 规则：UTF-8 严格解码（复用 Stage26 角色名解码器，拒绝截断/overlong/代理区）；
// 码点数 <= maxCodePoints；禁止控制字符（C0/C1，含 \r\n\t\0 与 ANSI ESC）；
// 拒绝空消息与纯空格消息。
// ---------------------------------------------------------------------------
inline bool IsValidChatText(const std::string& text, std::size_t maxCodePoints) {
    if (text.empty() || text.size() > kChatMaxBytes) {
        return false;
    }
    std::size_t codePoints = 0;
    std::size_t i = 0;
    bool hasNonSpace = false;
    while (i < text.size()) {
        std::uint32_t codePoint = 0;
        if (!account::detail::DecodeUtf8CodePoint(text, i, codePoint)) {
            return false; // 非法 UTF-8（截断/overlong/代理区）
        }
        // 指令二十七：控制字符全拒绝（C0 0x00~0x1F 与 0x7F、C1 0x80~0x9F）。
        // 注意 Unicode 码点语义：DEL/C1 用码点值判断；ANSI ESC(0x1B) 属于 C0。
        if (codePoint < 0x20 || (codePoint >= 0x7F && codePoint <= 0x9F)) {
            return false;
        }
        if (codePoint != ' ') {
            hasNonSpace = true;
        }
        ++codePoints;
        if (codePoints > maxCodePoints) {
            return false;
        }
    }
    return hasNonSpace; // 纯空格拒绝（指令十四）
}

// 私聊目标名校验 = 角色名规则（Stage26 唯一定义；禁止暴露内部 sessionId，指令十一）。
inline bool IsValidChatTargetName(const std::string& targetName) {
    return account::IsValidCharacterName(targetName);
}

// ---------------------------------------------------------------------------
// 限流判定（指令十五；纯函数——时间点由调用方注入，测试无需 sleep）。
// 语义：windowMs 时间窗内最多 maxMessages 条；now 之前窗口内的记录数 >= 上限
// 即拒绝。windowMs/maxMessages <= 0 仅测试显式放宽；生产配置校验拒绝（指令四十六）。
// ---------------------------------------------------------------------------
using SteadyClock = std::chrono::steady_clock;
using ChatTimePoint = SteadyClock::time_point;

inline bool IsChatRateAllowed(ChatTimePoint now, const std::deque<ChatTimePoint>& times,
                              int windowMs, int maxMessages) {
    if (windowMs <= 0 || maxMessages <= 0) {
        return true;
    }
    const auto window = std::chrono::milliseconds(windowMs);
    std::size_t count = 0;
    for (const auto& t : times) {
        if (now - t < window) {
            ++count;
        }
    }
    return count < static_cast<std::size_t>(maxMessages);
}

// 指令二十六：客户端本地命令解析（服务器仍验证 channel）。
// 返回 true 表示已解析出频道/目标；textInOut 去掉命令前缀。
inline bool ParseChatCommand(const std::string& input, ChatChannel& outChannel,
                             std::string& outTargetName, std::string& textInOut) {
    if (input.empty() || input[0] != '/') {
        return false;
    }
    // /n 内容 -> 附近；/world 内容 -> 世界；/w 玩家名 内容 -> 私聊。
    // （/s 系统频道禁止玩家发送，不解析。）
    const auto splitAt = [&input](std::size_t pos) {
        while (pos < input.size() && input[pos] == ' ') {
            ++pos;
        }
        return pos;
    };
    if (input.rfind("/n ", 0) == 0) {
        const std::size_t start = splitAt(3);
        if (start >= input.size()) {
            return false; // 空内容走通用拒绝
        }
        outChannel = ChatChannel::Nearby;
        outTargetName.clear();
        textInOut = input.substr(start);
        return true;
    }
    if (input.rfind("/world ", 0) == 0) {
        const std::size_t start = splitAt(7);
        if (start >= input.size()) {
            return false;
        }
        outChannel = ChatChannel::World;
        outTargetName.clear();
        textInOut = input.substr(start);
        return true;
    }
    if (input.rfind("/w ", 0) == 0) {
        const std::size_t nameStart = splitAt(3);
        const std::size_t nameEnd = input.find(' ', nameStart);
        if (nameEnd == std::string::npos) {
            return false; // 只有目标名没有内容 -> 交由上层提示
        }
        const std::size_t textStart = splitAt(nameEnd);
        if (textStart >= input.size()) {
            return false;
        }
        outChannel = ChatChannel::Whisper;
        outTargetName = input.substr(nameStart, nameEnd - nameStart);
        textInOut = input.substr(textStart);
        return true;
    }
    return false; // /s 等其它命令不支持（系统频道禁止玩家发送）
}

} // namespace legend::chat
