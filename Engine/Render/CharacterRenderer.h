#pragma once

#include "Engine/Animation/SpriteSheet.h"
#include "Engine/Entity/Character.h"
#include "Engine/Math/Vector2.h"

namespace legend::render {
class SpriteBatch;
}

namespace legend::render {

// 角色渲染器：Character + AnimationPlayer + SpriteSheet -> 最终 Draw。
// 只负责视觉；不负责键盘、移动逻辑、地图碰撞。
class CharacterRenderer {
public:
    // 精灵绘制中心 = feet + visual * (0.5 - pivot)
    static math::Vector2 GetSpriteDrawCenter(const entity::Character& character);

    // 精灵绘制缩放 = visual / 整张纹理尺寸（Quad 世界尺寸恒等于 visualWidth x visualHeight）
    static math::Vector2 GetSpriteScale(const entity::Character& character);

    // 把角色当前动画帧提交到批渲染
    void Draw(render::SpriteBatch& batch, const entity::Character& character) const;
};

} // namespace legend::render
