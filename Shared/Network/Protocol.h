#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::network {

// 阶段9.1指令十五：登录错误码（uint16 wire，不只靠 message 字符串）。
enum class LoginErrorCode : std::uint16_t {
    None = 0,
    InvalidCredentials = 1,
    ServiceUnavailable = 2,
    Timeout = 3,
    AlreadyPending = 4,
    AlreadyAuthenticated = 5,
};

// 阶段9 指令四十三~五十六：业务 payload 编解码（全部经 ByteWriter/Reader，
// 大端 + 长度校验 + 完整消费（阶段9.1指令十七：剩余字节非0即 malformed）；失败返回 false 不产生半包）。

struct ClientHelloPayload {
    std::uint16_t protocolVersion = 0;
    std::string clientBuild;
    std::string clientName;
};

struct ServerHelloPayload {
    bool accepted = false;
    std::uint16_t protocolVersion = 0;
    std::uint64_t connectionId = 0;
    std::string serverName;
    std::string message;
};

struct LoginRequestPayload {
    std::string username;
    std::string token;
};

struct LoginResponsePayload {
    bool success = false;
    std::uint64_t accountId = 0;
    std::string displayName;
    std::uint16_t errorCode = 0;
    std::string message;
};

struct HeartbeatPingPayload {
    std::uint32_t pingSequence = 0;
    std::uint64_t clientTimeMs = 0;
};

struct HeartbeatPongPayload {
    std::uint32_t pingSequence = 0;
    std::uint64_t serverTimeMs = 0;
};

struct DisconnectNoticePayload {
    std::string reason;
};

struct GatewayLoginForwardPayload {
    std::uint64_t requestId = 0;
    std::uint64_t clientConnectionId = 0;
    std::string username;
    std::string token;
};

struct LoginGatewayResponsePayload {
    std::uint64_t requestId = 0;
    std::uint64_t clientConnectionId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::string displayName;
    std::uint16_t errorCode = 0;
    std::string message;
};

struct ErrorResponsePayload {
    std::uint16_t errorCode = 0;
    std::string message;
};

bool EncodeClientHello(const ClientHelloPayload& p, std::vector<std::uint8_t>& out);
bool DecodeClientHello(const std::uint8_t* data, std::size_t size,
                       ClientHelloPayload& out, std::string& error);
bool EncodeServerHello(const ServerHelloPayload& p, std::vector<std::uint8_t>& out);
bool DecodeServerHello(const std::uint8_t* data, std::size_t size,
                       ServerHelloPayload& out, std::string& error);
bool EncodeLoginRequest(const LoginRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeLoginRequest(const std::uint8_t* data, std::size_t size,
                        LoginRequestPayload& out, std::string& error);
bool EncodeLoginResponse(const LoginResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeLoginResponse(const std::uint8_t* data, std::size_t size,
                         LoginResponsePayload& out, std::string& error);
bool EncodeHeartbeatPing(const HeartbeatPingPayload& p, std::vector<std::uint8_t>& out);
bool DecodeHeartbeatPing(const std::uint8_t* data, std::size_t size,
                         HeartbeatPingPayload& out, std::string& error);
bool EncodeHeartbeatPong(const HeartbeatPongPayload& p, std::vector<std::uint8_t>& out);
bool DecodeHeartbeatPong(const std::uint8_t* data, std::size_t size,
                         HeartbeatPongPayload& out, std::string& error);
bool EncodeDisconnectNotice(const DisconnectNoticePayload& p, std::vector<std::uint8_t>& out);
bool DecodeDisconnectNotice(const std::uint8_t* data, std::size_t size,
                            DisconnectNoticePayload& out, std::string& error);
bool EncodeGatewayLoginForward(const GatewayLoginForwardPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeGatewayLoginForward(const std::uint8_t* data, std::size_t size,
                               GatewayLoginForwardPayload& out, std::string& error);
bool EncodeLoginGatewayResponse(const LoginGatewayResponsePayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeLoginGatewayResponse(const std::uint8_t* data, std::size_t size,
                                LoginGatewayResponsePayload& out, std::string& error);
bool EncodeErrorResponse(const ErrorResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeErrorResponse(const std::uint8_t* data, std::size_t size,
                         ErrorResponsePayload& out, std::string& error);

} // namespace legend::network
