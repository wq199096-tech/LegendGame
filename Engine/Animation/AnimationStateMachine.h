#pragma once

#include <string>

#include "Engine/Entity/Character.h"
#include "Engine/Entity/Direction8.h"

namespace legend::animation {

// 动画状态机：集中选择 Clip，禁止 GameScene 手写 if(direction==...) play(...)。
// 阶段5优先级（严格）：Dead > HitReact > Attacking > Walk(moving) > Idle。
class AnimationStateMachine {
public:
    // 根据 Character 的 actionState / moving / direction 选择 Clip 名
    static std::string SelectClip(const entity::Character& character);
    static std::string SelectClip(bool moving, entity::Direction8 direction); // 兼容旧接口（仅 idle/walk）

    // 每帧调用：按 Character 当前状态切换动画
    void Update(entity::Character& character) const;
};

} // namespace legend::animation