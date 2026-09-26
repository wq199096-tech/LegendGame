#pragma once

#include <cmath>
#include <string>

#include "Engine/Entity/EntityId.h"
#include "Engine/Math/Vector2.h"

namespace legend::entity {

// 实体基类：统一 id / 名称 / 位置（脚底点）/ 激活与可见状态。
// 未来扩展：networkId / templateId / type。
class Entity {
public:
    Entity(EntityId id, std::string name);
    virtual ~Entity() = default;

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    EntityId GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }

    // Position 语义：脚底中心点（Feet Position），不是精灵中心
    const math::Vector2& GetPosition() const { return m_position; }
    void SetPosition(const math::Vector2& position) { m_position = position; }

    bool IsActive() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    EntityId m_id;
    std::string m_name;
    math::Vector2 m_position{0.0f, 0.0f};
    bool m_active = true;
    bool m_visible = true;
};

} // namespace legend::entity
