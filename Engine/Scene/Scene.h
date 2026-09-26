#pragma once

#include <memory>
#include <string>
#include <vector>

namespace legend::render {
class Renderer;
class Camera2D;
}

namespace legend::scene {

class GameObject;

// 场景基类：管理 GameObject 的生命周期、更新与渲染
class Scene {
public:
    explicit Scene(std::string name);
    // 析构函数类外定义：GameObject 在此头文件中仅为前置声明
    virtual ~Scene();

    // 场景加载（创建对象、资源等）
    virtual void OnLoad() {}
    // 场景卸载
    virtual void OnUnload() {}

    virtual void Update(float deltaTime);
    virtual void Render(render::Renderer& renderer, render::Camera2D& camera);

    GameObject* CreateGameObject(const std::string& name = "GameObject");
    void DestroyPendingObjects();

    const std::string& GetName() const { return m_name; }
    const std::vector<std::unique_ptr<GameObject>>& GetObjects() const { return m_objects; }

private:
    friend class GameObject;
    std::string m_name;
    std::vector<std::unique_ptr<GameObject>> m_objects;
};

} // namespace legend::scene
