#pragma once

namespace legend::scene {

class GameObject;

// 组件基类：所有挂在 GameObject 上的逻辑与表现均继承于此
class Component {
public:
    explicit Component(GameObject& owner) : m_owner(owner) {}
    virtual ~Component() = default;

    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;

    virtual void Update(float deltaTime) { (void)deltaTime; }

    GameObject& GetOwner() const { return m_owner; }

private:
    GameObject& m_owner;
};

} // namespace legend::scene
