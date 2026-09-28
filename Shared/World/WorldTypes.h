#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// 阶段11：World 基础常量（阶段11 只做 mapId=1 单实例，无副本/分线）。

// World 协议版本：与阶段9 PacketHeader version 一致（WorldClientHello 校验）。
inline constexpr std::uint16_t kWorldProtocolVersion = 1;

// 默认端口（指令三）：WorldServer 只绑定 127.0.0.1，DEV ONLY。
inline constexpr std::uint16_t kWorldServerDefaultPort = 7200;
inline constexpr std::uint16_t kLoginServerDefaultPort = 7100;

// 阶段11 只支持 mapId=1（指令三十/三十二）；无效 mapId 回退 1 + (0,0)。
inline constexpr std::uint16_t kDefaultMapId = 1;

// map1 位置边界（指令四十）：服务器权威 Clamp。
inline constexpr float kMapMinX = 0.0f;
inline constexpr float kMapMaxX = 2000.0f;
inline constexpr float kMapMinY = 0.0f;
inline constexpr float kMapMaxY = 2000.0f;

// 移动速度（指令三十七）：阶段11 固定 120 units/sec。
inline constexpr float kWorldMoveSpeed = 120.0f;

// deltaTime 上限（指令三十八）：防客户端伪造大 dt 瞬移。
inline constexpr float kMaxMoveDeltaTime = 0.1f;

// SelectionTicket 长度上限（指令六十五）：超长 = Malformed。
inline constexpr std::size_t kSelectionTicketMaxLength = 128;

// 服务白名单（指令九）：ClientHello.clientName 允许的内部服务。
inline constexpr const char* kServiceNameGateway = "LegendGateway";
inline constexpr const char* kServiceNameWorldServer = "LegendWorldServer";

inline bool IsMapIdSupported(std::uint16_t mapId) {
    return mapId == kDefaultMapId;
}

// 位置安全回退（指令五十八）：NaN/Inf/越界 → 0。
inline float SanitizeCoord(float v) {
    if (!(v >= kMapMinX) || !(v <= kMapMaxX)) { // 同时排除 NaN/Inf
        return 0.0f;
    }
    return v;
}

} // namespace legend::world
