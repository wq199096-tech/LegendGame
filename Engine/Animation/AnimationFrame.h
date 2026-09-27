#pragma once

#include <string>

namespace legend::animation {

// 动画帧：帧索引指向 SpriteSheet 的帧序号。
// event：进入该帧时抛出的动画事件（如 "attack_hit"），空字符串表示无事件。
// 老 JSON 无 event 字段时 Loader 兼容为空。
struct AnimationFrame {
    int frameIndex = 0;
    float duration = 0.1f;
    std::string event;
};

} // namespace legend::animation
