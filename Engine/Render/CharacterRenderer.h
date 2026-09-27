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
    // 世界视觉包围盒（与精灵绘制完全一致的 pivot 数学，供鼠标 HitTest 复用）
    struct WorldRect {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
    };

    // 精灵绘制中心 = feet + visual * (0.5 - pivot)
    static math::Vector2 GetSpriteDrawCenter(const entity::Character& character);

    // 精灵绘制缩放 = visual / 整张纹理尺寸（Quad 世界尺寸恒等于 visualWidth x visualHeight）
    static math::Vector2 GetSpriteScale(const entity::Character& character);

    // 精灵世界矩形（唯一公式，禁止调用方手写另一套 pivot 数学）：
    //   left   = feet.x - width  * pivot.x
    //   right  = feet.x + width  * (1 - pivot.x)
    //   top    = feet.y - height * pivot.y
    //   bottom = feet.y + height * (1 - pivot.y)
    static WorldRect GetSpriteWorldRect(const entity::Character& character);

    // 把角色当前动画帧提交到批渲染
    void Draw(render::SpriteBatch& batch, const entity::Character& character) const;
};

} // namespace legend::render
