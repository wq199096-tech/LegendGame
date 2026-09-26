#include "Engine/Scene/SceneManager.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Scene/Scene.h"

namespace legend::scene {

void SceneManager::SetScene(std::shared_ptr<Scene> scene) {
    if (m_current) {
        LOG_INFO("Unloading scene: " + m_current->GetName());
        m_current->OnUnload();
        LOG_INFO("Scene unloaded: " + m_current->GetName());
    }

    m_current = std::move(scene);

    if (m_current) {
        LOG_INFO("Loading scene: " + m_current->GetName());
        m_current->OnLoad();
        LOG_INFO("Scene loaded: " + m_current->GetName());
    }
}

void SceneManager::Update(float deltaTime) {
    if (m_current) {
        m_current->Update(deltaTime);
        m_current->DestroyPendingObjects();
    }
}

void SceneManager::Render(render::Renderer& renderer, render::Camera2D& camera) {
    if (m_current) {
        m_current->Render(renderer, camera);
    }
}

void SceneManager::Shutdown() {
    if (m_current) {
        LOG_INFO("Unloading scene: " + m_current->GetName());
        m_current->OnUnload();
        m_current.reset();
    }
}

} // namespace legend::scene
