#include "Engine/Scene/Scene.h"

#include <algorithm>
#include <vector>

#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Render/Texture.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SpriteRenderer.h"

namespace legend::scene {

Scene::Scene(std::string name) : m_name(std::move(name)) {}

Scene::~Scene() = default;

GameObject* Scene::CreateGameObject(const std::string& name) {
    m_objects.push_back(std::unique_ptr<GameObject>(new GameObject(*this, name)));
    return m_objects.back().get();
}

void Scene::Update(float deltaTime) {
    for (auto& object : m_objects) {
        if (object && !object->IsPendingDestroy()) {
            object->Update(deltaTime);
        }
    }
}

void Scene::Render(render::Renderer& renderer, render::Camera2D& camera) {
    (void)camera; // 摄像机矩阵已在 Renderer::BeginFrame 中生效

    // 收集全部 SpriteRenderer，按 RenderOrder 排序后绘制
    std::vector<SpriteRenderer*> sprites;
    sprites.reserve(m_objects.size());
    for (auto& object : m_objects) {
        if (!object || object->IsPendingDestroy()) {
            continue;
        }
        if (auto* spriteRenderer = object->GetComponent<SpriteRenderer>()) {
            sprites.push_back(spriteRenderer);
        }
    }

    std::stable_sort(sprites.begin(), sprites.end(),
                     [](const SpriteRenderer* a, const SpriteRenderer* b) {
                         return a->GetRenderOrder() < b->GetRenderOrder();
                     });

    for (SpriteRenderer* spriteRenderer : sprites) {
        const std::shared_ptr<render::Texture>& texture = spriteRenderer->GetTexture();
        if (!texture || !texture->IsValid()) {
            continue;
        }
        const Transform& transform = spriteRenderer->GetOwner().GetTransform();

        render::SpriteDrawParams params;
        params.scale = transform.GetScale();
        params.rotationDegrees = transform.GetRotation();
        params.flipX = spriteRenderer->GetFlipX();
        params.flipY = spriteRenderer->GetFlipY();
        params.tint = spriteRenderer->GetColor();

        renderer.DrawSprite(*texture, transform.GetPosition(), params);
    }
}

void Scene::DestroyPendingObjects() {
    std::erase_if(m_objects, [](const std::unique_ptr<GameObject>& object) {
        return object && object->IsPendingDestroy();
    });
}

} // namespace legend::scene
