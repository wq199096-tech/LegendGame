#pragma once

#include <cstdint>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段16 指令六十一：RemoteStatusEffect —— 客户端侧单个状态效果展示数据。
// 仅展示（remainingMs 本地递减只做 UI 倒计时，不影响战斗数值，指令六十五/
// 六十六）；真正的 Remove 必须等 StatusRemoved 或 Snapshot 纠偏。
// ---------------------------------------------------------------------------
struct RemoteStatusEffect {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t stacks = 1;
    std::uint32_t remainingMs = 0;   // 服务器权威剩余（最近一次更新时）
    std::uint32_t durationMs = 0;    // 总时长（Applied 携带）
    std::uint64_t sourceEntityId = 0;
};

} // namespace legend::client
