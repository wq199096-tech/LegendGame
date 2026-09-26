#pragma once

#include <memory>
#include <string>

#include "Engine/Map/Map.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Core/Window.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::render {
class Texture;
}

// LegendMapEditor —— 地图编辑器 Prototype（Dear ImGui 仅用于开发工具）
// 功能：New/Open/Save/Save As、Tile 绘制、碰撞编辑、物件放置/删除、
//       Grid 显示、Collision Overlay、滚轮缩放、右/中键平移。
// 与游戏共用 Engine/Map 的数据结构与序列化（MapLoader）。
class LegendMapEditorApp {
public:
    int Run();

private:
    enum class EditMode {
        Ground,
        Collision,
        Objects,
    };

    bool Initialize();
    void Shutdown();
    void ProcessEvents(bool& quit);
    void HandleMapEditing();
    void DrawUI();
    void DrawMenuBar();
    void DrawPalettePanel();
    void DrawInfoPanel();

    bool OpenMap(const std::string& path);
    bool SaveMapTo(const std::string& path);
    void NewMap();
    void SyncObjectCollision(const legend::map::MapObject& object, bool blocked);
    void UpdateWindowTitle();

    legend::Window m_window;
    legend::render::Renderer m_renderer;
    legend::resource::ResourceManager m_resources;
    legend::map::MapRenderer m_mapRenderer;
    legend::render::Camera2D m_camera;
    std::shared_ptr<legend::map::Map> m_map;
    std::shared_ptr<legend::render::Texture> m_whiteTexture;

    std::string m_mapPath;
    bool m_mapDirty = false;

    EditMode m_mode = EditMode::Ground;
    uint16_t m_selectedTile = 1;
    std::string m_selectedObjectType = "tree";
    uint32_t m_selectedObjectId = 0;

    bool m_showCollisionOverlay = true;
    bool m_showGrid = true;
    bool m_panActive = false;
    float m_lastPanX = 0.0f;
    float m_lastPanY = 0.0f;
    bool m_prevLeftMouseDown = false;

    float m_viewportW = 1.0f;
    float m_viewportH = 1.0f;

    // 对话框状态
    bool m_showOpenDialog = false;
    bool m_showSaveAsDialog = false;
    char m_pathBuffer[512] = {};
    std::string m_dialogError;
    bool m_requestQuit = false;

    std::string m_lastTitle;
    bool m_initialized = false;
    bool m_imguiInitialized = false;

    // 自动化冒烟测试（环境变量触发）
    bool m_smokeQuitAfterSave = false;
    double m_smokeQuitTimer = 0.0;
};
