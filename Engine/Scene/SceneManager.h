#pragma once

#include <memory>

namespace legend::render {
class Renderer;
class Camera2D;
}

namespace legend::scene {

class Scene;

class SceneManager {
public:
    // 切换场景：旧场景 OnUnload，新场景 OnLoad
    void SetScene(std::shared_ptr<Scene> scene);

    void Update(float deltaTime);
    void Render(render::Renderer& renderer, render::Camera2D& camera);
    void Shutdown();

    Scene* GetCurrent() const { return m_current.get(); }

private:
    std::shared_ptr<Scene> m_current;
};

} // namespace legend::scene
