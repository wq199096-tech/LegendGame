#pragma once

namespace legend::animation {

// 动画帧：帧索引指向 SpriteSheet 的帧序号
struct AnimationFrame {
    int frameIndex = 0;
    float duration = 0.1f;
};

} // namespace legend::animation
