#pragma once

#include <cstdint>

namespace legend::network {

// 阶段9 指令六十八/六十九：统一网络错误分类（正常断线绝不当 Fatal）。
enum class NetworkError : std::uint8_t {
    None = 0,
    ConnectFailed,
    ConnectTimeout,
    ConnectionClosed,   // EOF / ECONNRESET / broken pipe 统一
    ProtocolError,      // bad magic / bad version / malformed / unknown id
    BadMagic,
    BadVersion,
    OversizedPayload,
    MalformedPayload,
    UnknownMessageId,
    HeartbeatTimeout,
    LoginTimeout,
    LoginServiceUnavailable,
    LoginRejected,
};

const char* NetworkErrorName(NetworkError error);

} // namespace legend::network
