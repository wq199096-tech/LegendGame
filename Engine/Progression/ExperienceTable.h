#pragma once

#include <cstdint>

namespace legend::progression {

// 经验数值统一 64 位（阶段6.1）：currentExp/totalExp/RequiredExp/AddExperience 全链路使用，
// 杜绝高等级（1.5^49 ≈ 2.8e10）下 int32 截断与 currentExp += amount 的 signed overflow。
using ExperienceValue = std::int64_t;

// 最大等级（阶段6）：达到后不再升级，currentExp 归 0（行为明确）
inline constexpr int kMaxLevel = 50;

// 升级所需经验统一入口（禁止 UI/Player 各自写 level*100 散落公式）：
// RequiredExp(level) = 100 * 1.5^(level-1)，double 计算后安全转 int64（64 位范围内无截断）。
// Level 1~49 全部返回真实经验需求；level < 1 或 level >= kMaxLevel 返回 0（无升级需求）。
ExperienceValue RequiredExp(int level);

} // namespace legend::progression
