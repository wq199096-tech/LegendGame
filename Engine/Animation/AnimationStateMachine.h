#pragma once

#include <string>

#include "Engine/Entity/Character.h"
#include "Engine/Entity/Direction8.h"

namespace legend::animation {

// 动画状态机：根据 Character 的 moving / direction 集中选择 Clip。
// 集中管理状态选择，GameScene 不允许手写 if(direction==...) play(...)。
class AnimationStateMachine {
public:
    static std::string SelectClip(bool moving, entity::Direction8 direction);

    // 每帧调用：按 Character 当前状态切换/推进动画
    void Update(entity::Character& character) const;
};

} // namespace legend::animation
