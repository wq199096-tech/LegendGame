#include "Engine/Scene/GameObject.h"

#include "Engine/Scene/Scene.h"

namespace legend::scene {

GameObject::GameObject(Scene& scene, std::string name)
    : m_scene(scene), m_name(std::move(name)) {
    m_transform = std::make_unique<Transform>(*this);
}

GameObject* GameObject::Create(Scene& scene, const std::string& name) {
    return scene.CreateGameObject(name);
}

void GameObject::Update(float deltaTime) {
    for (auto& component : m_components) {
        component->Update(deltaTime);
    }
}

} // namespace legend::scene
