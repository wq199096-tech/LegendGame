#include "Engine/Progression/ExperienceTable.h"

#include <cmath>

namespace legend::progression {

ExperienceValue RequiredExp(int level) {
    if (level < 1 || level >= kMaxLevel) {
        return 0; // 非法等级或已满级：无升级需求
    }
    // Level 1~49 全部返回真实经验需求（1.5^49 ≈ 2.8e10 在 int64 范围内，无 clamp/截断）
    return static_cast<ExperienceValue>(std::llround(100.0 * std::pow(1.5, level - 1)));
}

} // namespace legend::progression
