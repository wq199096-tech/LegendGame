#include "Shared/Network/NetworkErrors.h"

namespace legend::network {

const char* NetworkErrorName(NetworkError error) {
    switch (error) {
        case NetworkError::None: return "none";
        case NetworkError::ConnectFailed: return "connect failed";
        case NetworkError::ConnectTimeout: return "connect timeout";
        case NetworkError::ConnectionClosed: return "connection closed";
        case NetworkError::ProtocolError: return "protocol error";
        case NetworkError::BadMagic: return "bad magic";
        case NetworkError::BadVersion: return "bad version";
        case NetworkError::OversizedPayload: return "oversized payload";
        case NetworkError::MalformedPayload: return "malformed payload";
        case NetworkError::UnknownMessageId: return "unknown message id";
        case NetworkError::HeartbeatTimeout: return "heartbeat timeout";
        case NetworkError::LoginTimeout: return "login timeout";
        case NetworkError::LoginServiceUnavailable: return "login service unavailable";
        case NetworkError::LoginRejected: return "login rejected";
    }
    return "unknown";
}

} // namespace legend::network
