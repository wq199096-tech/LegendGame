#pragma once

#include <cstdint>

namespace legend::world {

// 阶段13 指令三：Monster 错误码（阶段13 服务端硬编码配置，仅基础定义）。
enum class MonsterErrorCode : std::uint16_t {
    None = 0,
    UnknownMonsterType = 1,
    InvalidEntity = 2,
    InternalError = 3,
};

inline const char* MonsterErrorCodeName(std::uint16_t code) {
    switch (static_cast<MonsterErrorCode>(code)) {
        case MonsterErrorCode::None: return "None";
        case MonsterErrorCode::UnknownMonsterType: return "UnknownMonsterType";
        case MonsterErrorCode::InvalidEntity: return "InvalidEntity";
        case MonsterErrorCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
