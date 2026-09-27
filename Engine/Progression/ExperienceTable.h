#pragma once

namespace legend::progression {

// 最大等级（阶段6）：达到后不再升级，currentExp 归 0（行为明确）
inline constexpr int kMaxLevel = 50;

// 升级所需经验统一入口（禁止 UI/Player 各自写 level*100 散落公式）：
// RequiredExp(level) = 100 * 1.5^(level-1)，四舍五入整数。
// Level1 -> 100, Level2 -> 150, Level3 -> 225, ...
// level < 1 或 level >= kMaxLevel 返回 0（无升级需求）。
int RequiredExp(int level);

} // namespace legend::progression
