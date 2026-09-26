#pragma once

#include <string>
#include <vector>

#include "Engine/Animation/AnimationFrame.h"

namespace legend::animation {

// 动画片段：有序帧序列 + 循环标记
class AnimationClip {
public:
    std::string name;
    bool loop = true;
    std::vector<AnimationFrame> frames;

    int GetFrameCount() const { return static_cast<int>(frames.size()); }
    const AnimationFrame& GetFrame(int ordinal) const { return frames[ordinal]; }
    float GetTotalDuration() const {
        float total = 0.0f;
        for (const auto& frame : frames) {
            total += frame.duration;
        }
        return total;
    }
};

} // namespace legend::animation
