#include "Engine/Progression/ExperienceTable.h"

#include <cmath>

namespace legend::progression {

int RequiredExp(int level) {
    if (level < 1 || level >= kMaxLevel) {
        return 0; // 非法等级或已满级：无升级需求
    }
    // 1.5^n 增长极快：1.5^49 ≈ 2.8e10 超出 int 上限，clamp 防溢出 UB（高等级需求近似封顶）
    const double raw = 100.0 * std::pow(1.5, level - 1);
    constexpr double kIntMax = 2147483647.0;
    if (raw >= kIntMax) {
        return 2147483647;
    }
    return static_cast<int>(std::lround(raw));
}

} // namespace legend::progression
