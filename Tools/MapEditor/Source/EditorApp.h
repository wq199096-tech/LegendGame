#pragma once

#include <memory>
#include <string>

#include "Engine/Map/Map.h"
#include "Engine/Map/MapRenderer.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Renderer.h"
#include "Engine/Core/Window.h"
#include "Engine/Resource/ResourceManager.h"
#include "Tools/MapEditor/Source/GameDataDocument.h"
#include "Tools/MapEditor/Source/WorldDocument.h"

namespace legend::render {
class Texture;
}

// LegendGame World Editor（阶段22 升级自 LegendMapEditor）
// 两个工作区：
//   TileMap —— 原地图编辑器（Tile 绘制/碰撞/物件，与游戏共用 Engine/Map 序列化）
//   World   —— 数据驱动世界编辑器（Maps/NPCs/Monster Spawns/Portals 的
//              World Tree + Canvas + Inspector + Validation，读写 Data/World JSON）
// 功能：Pan/Zoom/Grid/世界·鼠标坐标/点选/拖拽/Delete/Ctrl+D/Ctrl+Z/Ctrl+Y、
//       Undo/Redo 100 步、Dirty 标题 *、关闭确认、实时 Validation（Error 禁存）、
//       原子保存 + .backup 轮换、Validate World / Launch WorldServer。
class LegendMapEditorApp {
public:
    int Run();

private:
    enum class EditMode {
        Ground,
        Collision,
        Objects,
    };

    // 22.2：工作区（View 菜单切换；默认 World）。
    enum class Workspace {
        World,
        TileMap,
    };

    // World canvas 交互状态。
    enum class WorldDragKind {
        None,
        Pan,
        Object, // 拖拽选中对象（NPC/Spawn center/Portal/Map 无拖拽）
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
    void UpdateWindowTitle();

    // ---- World 工作区（阶段22）----
    void HandleWorldShortcuts();
    void DrawWorldWorkspace();      // 三栏 + 底部 Validation + 状态条
    void DrawWorldTree();           // 左：Maps/NPCs/Spawns/Portals 树
    void DrawGameDataTree();        // 左：Items/Monsters/Skills/Statuses/Quests/Shops/Teleports/Loot（23.1/23.12）
    void DrawWorldCanvas();         // 中：Pan/Zoom/Grid/对象绘制/拾取/拖拽
    void DrawWorldInspector();      // 右：选中对象字段编辑
    void DrawWorldInspectorFields(); // World 字段编辑主体（World/Game 两个 Inspector 共存）
    void DrawGameDataInspector();   // 右：Game 定义字段编辑（23.3~23.10/23.18）
    void DrawWorldBottomPanel();    // 下：Validation/Console/Status/Save
    void DrawWorldObjects();        // canvas 绘制具体对象
    void WorldScreenToWorld(float sx, float sy, float& wx, float& wy) const;
    legend::editor::WorldDocument::ObjectType PickObjectAt(float wx, float wy,
                                                           std::uint32_t& outId) const;
    void MutateSelectedPos(float wx, float wy);
    void OpenWorldDir(const std::string& dir);
    void LaunchWorldServer();
    void UpdateWorldTitle();

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
    bool m_showWorldOpenDialog = false;   // World 数据目录
    bool m_showWorldSaveAsDialog = false;
    char m_pathBuffer[512] = {};
    std::string m_dialogError;
    bool m_requestQuit = false;
    bool m_confirmExitShown = false;   // 22.16：未保存确认弹窗显示中
    bool m_confirmExitDismissed = false;
    bool m_forceQuit = false;          // 弹窗内点 Exit

    std::string m_lastTitle;
    bool m_initialized = false;
    bool m_imguiInitialized = false;

    // 自动化冒烟测试（环境变量触发）
    bool m_smokeQuitAfterSave = false;
    double m_smokeQuitTimer = 0.0;
    bool m_smokeFailed = false;

    // ---- 阶段22：World 工作区状态 ----
    Workspace m_workspace = Workspace::World;
    std::unique_ptr<legend::editor::WorldDocument> m_world;
    std::string m_worldDir = "Data/World";
    bool m_worldLoaded = false;
    std::string m_worldMessage;      // Console 行（Load/Save/Launch 结果）
    bool m_worldMessageIsError = false;

    // ---- 阶段23：Game Data Editor 状态 ----
    std::unique_ptr<legend::editor::GameDataDocument> m_game;
    std::string m_gameDir = "Data/Game";
    bool m_gameLoaded = false;
    char m_gameSearch[96] = {};      // 23.12：ID/Name 过滤
    char m_gInsName[128] = {};       // Inspector 文本缓冲（owner 跟踪）
    char m_gInsText[256] = {};
    char m_gInsDesc[256] = {};
    legend::editor::GameDataDocument::ObjectType m_gOwnerType =
        legend::editor::GameDataDocument::ObjectType::None;
    std::uint32_t m_gOwnerId = 0;

    // canvas 视图（世界坐标 → 屏幕像素：screen = (world - origin) * zoom）
    float m_worldOriginX = -100.0f;
    float m_worldOriginY = -100.0f;
    float m_worldZoom = 0.45f;
    WorldDragKind m_worldDrag = WorldDragKind::None;
    float m_dragStartWX = 0.0f;
    float m_dragStartWY = 0.0f;
    float m_dragOrigOX = 0.0f;
    float m_dragOrigOY = 0.0f;
    float m_dragCurWX = 0.0f; // Object 拖拽中的临时位置（release 才 Mutate，防 undo 洪泛）
    float m_dragCurWY = 0.0f;
    float m_mouseWorldX = 0.0f;
    float m_mouseWorldY = 0.0f;
    bool m_showWorldGrid = true;

    // Inspector 文本编辑缓冲（owner 跟踪：选中变化时重新载入）
    char m_insName[128] = {};
    char m_insTitle[128] = {};
    char m_insText[256] = {};
    char m_insQuestIds[128] = {};
    legend::editor::WorldDocument::ObjectType m_insOwnerType =
        legend::editor::WorldDocument::ObjectType::None;
    std::uint32_t m_insOwnerId = 0;
    bool m_insDirty = false; // buffer 与文档值不一致（InputText 提交时置位）
};

// 供 Tests/MapEditorDataChecks.cpp 直接测文档模型（GUI 需人工确认项在 README 记录）。
