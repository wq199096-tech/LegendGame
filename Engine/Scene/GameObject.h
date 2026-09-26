#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Engine/Scene/Component.h"
#include "Engine/Scene/Transform.h"

namespace legend::scene {

class Scene;

// 游戏对象：持有 Transform 与若干组件（未来可挂 Animator/Collider/MonsterAI 等）
class GameObject {
public:
    static GameObject* Create(Scene& scene, const std::string& name = "GameObject");

    const std::string& GetName() const { return m_name; }
    Scene& GetScene() { return m_scene; }

    Transform& GetTransform() { return *m_transform; }
    const Transform& GetTransform() const { return *m_transform; }

    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        auto component = std::make_unique<T>(*this, std::forward<Args>(args)...);
        T* raw = component.get();
        m_components.push_back(std::move(component));
        return raw;
    }

    template <typename T>
    T* GetComponent() const {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        for (const auto& component : m_components) {
            if (T* result = dynamic_cast<T*>(component.get())) {
                return result;
            }
        }
        return nullptr;
    }

    void Update(float deltaTime);
    void Destroy() { m_pendingDestroy = true; }
    bool IsPendingDestroy() const { return m_pendingDestroy; }

private:
    friend class Scene;
    GameObject(Scene& scene, std::string name);

    Scene& m_scene;
    std::string m_name;
    std::unique_ptr<Transform> m_transform;
    std::vector<std::unique_ptr<Component>> m_components;
    bool m_pendingDestroy = false;
};

} // namespace legend::scene
