#pragma once

#include <cstdint>

namespace legend::entity {

// 实体唯一标识：不使用裸 int 到处乱传
using EntityId = uint64_t;

constexpr EntityId kInvalidEntityId = 0;

} // namespace legend::entity
