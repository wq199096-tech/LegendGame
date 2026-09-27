#pragma once

#include <cstddef>
#include <vector>

#include "Engine/Entity/ActorType.h"
#include "Engine/Entity/EntityId.h"
#include "Engine/Math/Vector2.h"

namespace legend::entity {

class Character;

// 场景内活跃 Character Actor 注册表（非拥有引用；生命周期由 WorldActorManager 的
// unique_ptr 保证）。统一管理 Player / NPC / Monster 引用，
// GameScene 不再各自保存 vector<Player> / vector<Monster> 到处遍历。
class ActorRegistry {
public:
    // 注册 Actor；同一指针或同一 id 重复注册忽略并告警
    void Register(Character* actor);
    // 注销指定 id；不存在时静默（目标失效是正常流程）
    void Unregister(EntityId id);
    void Clear() { m_actors.clear(); }

    // 按 id 查找（不存在返回 nullptr）；只查活跃注册项，不校验 active。
    // 注册表本身不拥有对象，const 查询同样返回可变指针（const_cast 语义集中于此）。
    Character* Get(EntityId id) const;

    // 全部 Actor（注册顺序）
    const std::vector<Character*>& GetAll() const { return m_actors; }

    // 指定类型的全部 Actor
    std::vector<Character*> GetByType(ActorType type) const;

    // 圆形范围内（DistanceSquared 判定，不含边界等于 radius）的活跃 Actor
    std::vector<Character*> FindInRadius(const math::Vector2& position, float radius) const;

    std::size_t Count() const { return m_actors.size(); }

private:
    std::vector<Character*> m_actors; // 保持注册顺序（渲染统计/遍历稳定）
};

} // namespace legend::entity
