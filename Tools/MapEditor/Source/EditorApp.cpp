#include "EditorApp.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Engine/Debug/Logger.h"
#include "Engine/Map/MapLoader.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Render/Texture.h"

using legend::world::MapDefinition;
using legend::world::MapType;
using legend::world::MonsterSpawnDefinition;
using legend::world::NpcDefinition;
using legend::world::NpcType;
using legend::world::PortalDefinition;
using legend::world::WorldDataSet;

namespace editor = legend::editor;

namespace {

constexpr float kDefaultZoom = 1.0f;
constexpr int kDefaultMapTiles = 100;

legend::map::MapObject MakeObjectForType(const std::string& type) {
    legend::map::MapObject object;
    object.textureId = type;
    object.renderOrder = 0;
    if (type == "tree") {
        object.width = 96.0f;
        object.height = 96.0f;
        object.blocking = true;
        object.occluder = true;
    } else if (type == "rock") {
        object.width = 48.0f;
        object.height = 48.0f;
        object.blocking = true;
        object.occluder = false;
    } else if (type == "building") {
        object.width = 192.0f;
        object.height = 128.0f;
        object.blocking = true;
        object.occluder = true;
    } else { // flower 等装饰
        object.width = 24.0f;
        object.height = 24.0f;
        object.blocking = false;
        object.occluder = false;
    }
    return object;
}

} // namespace

int LegendMapEditorApp::Run() {
    if (!Initialize()) {
        Shutdown();
        return 1;
    }

    LOG_INFO("LegendMapEditor entering main loop.");

    bool quit = false;
    while (!quit) {
        ProcessEvents(quit);

        int viewportW = 1;
        int viewportH = 1;
        m_renderer.QueryViewportSize(viewportW, viewportH);
        m_viewportW = static_cast<float>(viewportW);
        m_viewportH = static_cast<float>(viewportH);

        if (m_workspace == Workspace::TileMap) {
            HandleMapEditing();
        }

        // ---- 场景绘制（World 工作区完全由 ImGui DrawList 绘制）----
        m_renderer.BeginFrame(m_camera);
        if (m_workspace == Workspace::TileMap) {
        m_mapRenderer.BeginFrame(m_camera, m_viewportW, m_viewportH);
        if (m_map) {
            m_mapRenderer.RenderGround(*m_map);

            // 物件按底部 Y 排序绘制
            std::vector<const legend::map::MapObject*> sorted;
            sorted.reserve(m_map->GetObjects().Objects().size());
            for (const auto& object : m_map->GetObjects().Objects()) {
                sorted.push_back(&object);
            }
            std::sort(sorted.begin(), sorted.end(), legend::map::MapRenderer::YSortCompare);
            for (const legend::map::MapObject* object : sorted) {
                m_mapRenderer.DrawMapObject(*object);
            }
            m_mapRenderer.Flush();

            // 选中物件高亮（半透明黄色框）
            if (m_selectedObjectId != 0 && m_whiteTexture) {
                if (const legend::map::MapObject* selected =
                        m_map->GetObjects().FindObject(m_selectedObjectId)) {
                    m_renderer.DrawSprite(
                        *m_whiteTexture, {selected->x, selected->y},
                        legend::render::SpriteDrawParams{
                            {selected->width / 64.0f, selected->height / 64.0f},
                            0.0f, false, false,
                            legend::math::Color(1.0f, 1.0f, 0.0f, 0.30f)});
                }
            }

            // Grid：64px 世界网格线（跟随地图坐标，无漂移）
            if (m_showGrid && m_whiteTexture) {
                const float tileSize = static_cast<float>(m_map->GetTileSize());
                const int tx0 = legend::map::WorldToTileIndex(m_mapRenderer.GetViewLeft(), tileSize);
                const int tx1 = legend::map::WorldToTileIndex(m_mapRenderer.GetViewRight(), tileSize);
                const int ty0 = legend::map::WorldToTileIndex(m_mapRenderer.GetViewTop(), tileSize);
                const int ty1 = legend::map::WorldToTileIndex(m_mapRenderer.GetViewBottom(), tileSize);
                const legend::math::Color gridColor(0.0f, 0.0f, 0.0f, 0.22f);
                const float viewMidY = (m_mapRenderer.GetViewTop() + m_mapRenderer.GetViewBottom()) * 0.5f;
                const float viewMidX = (m_mapRenderer.GetViewLeft() + m_mapRenderer.GetViewRight()) * 0.5f;
                const float viewH = m_mapRenderer.GetViewBottom() - m_mapRenderer.GetViewTop();
                const float viewW = m_mapRenderer.GetViewRight() - m_mapRenderer.GetViewLeft();
                for (int tx = tx0; tx <= tx1 + 1; ++tx) {
                    const float x = legend::map::TileToWorldMin(tx, tileSize);
                    m_renderer.DrawSprite(
                        *m_whiteTexture, {x, viewMidY},
                        legend::render::SpriteDrawParams{
                            {2.0f / 64.0f, viewH / 64.0f}, 0.0f, false, false, gridColor});
                }
                for (int ty = ty0; ty <= ty1 + 1; ++ty) {
                    const float y = legend::map::TileToWorldMin(ty, tileSize);
                    m_renderer.DrawSprite(
                        *m_whiteTexture, {viewMidX, y},
                        legend::render::SpriteDrawParams{
                            {viewW / 64.0f, 2.0f / 64.0f}, 0.0f, false, false, gridColor});
                }
            }

            if (m_showCollisionOverlay) {
                m_mapRenderer.RenderCollisionOverlay(*m_map);
            }
        }
        m_mapRenderer.EndFrame();
        } // Workspace::TileMap

        // ---- ImGui 绘制 ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        HandleWorldShortcuts();
        DrawUI();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        m_renderer.EndFrame(); // swap

        // 22.16：存在未保存修改 → 确认弹窗（DrawUI 内打开）。
        if (m_requestQuit) {
            const bool tileDirty = m_workspace == Workspace::TileMap && m_mapDirty;
            const bool worldDirty = m_workspace == Workspace::World && m_world &&
                                    m_world->IsDirty();
            if (m_forceQuit) {
                quit = true;
            } else if ((!tileDirty && !worldDirty && !m_confirmExitShown) ||
                       (m_confirmExitDismissed)) {
                m_requestQuit = false;
                m_confirmExitShown = false;
                m_confirmExitDismissed = false;
            }
        }

        if (m_smokeQuitAfterSave) {
            m_smokeQuitTimer -= 1.0 / 60.0;
            if (m_smokeQuitTimer <= 0.0) {
                LOG_INFO("Editor smoke test complete, quitting.");
                quit = true;
            }
        }
        UpdateWindowTitle();
    }

    LOG_INFO("LegendMapEditor leaving main loop.");
    Shutdown();
    return 0;
}

bool LegendMapEditorApp::Initialize() {
    legend::debug::Logger::Init("Logs");
    LOG_INFO("==================================================");
    LOG_INFO("LegendMapEditor V0.2 starting up.");
    LOG_INFO("==================================================");

    if (!m_window.Create("LegendGame World Editor", 1440, 900)) {
        LOG_ERROR("Editor initialization failed: could not create window.");
        return false;
    }
    if (!m_renderer.Initialize(m_window.GetHandle())) {
        LOG_ERROR("Editor initialization failed: could not initialize renderer.");
        m_window.Destroy();
        return false;
    }
    m_resources.Initialize("Assets");
    if (!m_mapRenderer.Initialize(m_renderer.GetSpriteShader())) {
        LOG_ERROR("Editor initialization failed: could not initialize MapRenderer.");
        m_renderer.Shutdown();
        m_window.Destroy();
        return false;
    }
    m_mapRenderer.CreateDefaultPlaceholderTextures(m_resources);
    m_whiteTexture = m_resources.CreateSolidTexture(
        "editor/white", 8, legend::math::Color(1.0f, 1.0f, 1.0f, 1.0f));

    // ---- ImGui 初始化（仅开发工具 UI） ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL3_InitForOpenGL(m_window.GetHandle(),
                                 static_cast<SDL_GLContext>(m_renderer.GetGLContext()));
    ImGui_ImplOpenGL3_Init("#version 330");
    m_imguiInitialized = true;

    // ---- 阶段22：默认加载 Data/World（World 工作区主数据） ----
    m_world = std::make_unique<editor::WorldDocument>();
    {
        const char* worldDirEnv = SDL_getenv("LEGEND_EDITOR_WORLD_DIR");
        const std::string worldDir = (worldDirEnv != nullptr && worldDirEnv[0] != '\0')
                                         ? std::string(worldDirEnv)
                                         : std::string("Data/World");
        std::string worldError;
        if (m_world->Load(worldDir, worldError)) {
            m_worldLoaded = true;
            m_worldDir = worldDir;
            LOG_INFO("World Editor: loaded world data from '" + worldDir + "'.");
        } else {
            LOG_WARN("World Editor: failed to load '" + worldDir + "': " + worldError +
                     " (File > Open World Data to retry.)");
            m_worldMessage = "Load failed: " + worldError;
            m_worldMessageIsError = true;
        }
    }

    // 默认打开测试地图
    const char* mapPathEnv = SDL_getenv("LEGEND_EDITOR_MAP");
    const std::string initialPath = (mapPathEnv != nullptr && mapPathEnv[0] != '\0')
                                        ? std::string(mapPathEnv)
                                        : std::string("Assets/Maps/TestMap/map.json");
    if (!OpenMap(initialPath)) {
        LOG_WARN("No initial map loaded ('" + initialPath + "'). Use File > New Map.");
    }

    m_camera.SetZoom(kDefaultZoom);
    if (m_map) {
        m_camera.SetPosition({m_map->GetWorldWidth() * 0.5f, m_map->GetWorldHeight() * 0.5f});
    }

    // ---- 自动化冒烟测试：改 Tile + 放物件 + 保存 + 退出 ----
    const char* smoke = SDL_getenv("LEGEND_EDITOR_SMOKE");
    if (smoke != nullptr && smoke[0] == '1' && m_map) {
        LOG_INFO("Editor smoke test: painting tile (50,45) -> Water, placing tree, saving.");
        m_map->SetGroundTile(50, 45, static_cast<uint16_t>(legend::map::TileId::Water));

        legend::map::MapObject tree = MakeObjectForType("tree");
        tree.id = m_map->GetObjects().GetMaxObjectId() + 1;
        tree.name = "smoke_tree";
        tree.x = legend::map::TileToWorldCenter(52, 64.0f);
        tree.y = legend::map::TileToWorldCenter(45, 64.0f);
        // SpawnObject 自动维护 Object Collision 引用计数
        m_map->SpawnObject(tree);
        if (tree.occluder) {
            m_map->GetOcclusion().AddOccluder(tree.id);
        }

        const char* out = SDL_getenv("LEGEND_EDITOR_SMOKE_OUT");
        const std::string outPath = (out != nullptr && out[0] != '\0')
                                        ? std::string(out)
                                        : std::string("Build/editor_test_map.json");
        if (SaveMapTo(outPath)) {
            LOG_INFO("Editor smoke test: saved to " + outPath);
        }
        m_smokeQuitAfterSave = true;
        m_smokeQuitTimer = 2.0;
    }

    // ---- 22.20：World 数据冒烟（LEGEND_EDITOR_SMOKE=world）----
    // Load → Validate（Load 内含）→ 保存回写（原子+backup 路径）→ 退出。
    if (smoke != nullptr && std::strcmp(smoke, "world") == 0 && m_world) {
        LOG_INFO("Editor world smoke test: validating + re-saving world data.");
        std::string smokeError;
        if (!m_worldLoaded) {
            m_smokeFailed = true;
            LOG_ERROR("Editor world smoke FAILED: no world data loaded.");
        } else if (!m_world->Save(smokeError)) {
            m_smokeFailed = true;
            LOG_ERROR("Editor world smoke FAILED: save error: " + smokeError);
        } else {
            LOG_INFO("Editor world smoke PASSED: roundtrip save ok.");
        }
        m_smokeQuitAfterSave = true;
        m_smokeQuitTimer = 1.0;
    }

    m_initialized = true;
    return true;
}

void LegendMapEditorApp::Shutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }
    m_mapRenderer.Shutdown();
    m_renderer.Shutdown();
    m_window.Destroy();
    legend::debug::Logger::Shutdown();
}

void LegendMapEditorApp::ProcessEvents(bool& quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // 22.16：走 m_requestQuit（主循环检查 Dirty → 确认弹窗）。
                m_requestQuit = true;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (m_workspace == Workspace::TileMap && !ImGui::GetIO().WantCaptureMouse &&
                    event.wheel.y != 0.0f) {
                    m_camera.SetZoom(m_camera.GetZoom() *
                                     (event.wheel.y > 0.0f ? 1.1f : 1.0f / 1.1f));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (m_workspace == Workspace::TileMap &&
                    (event.button.button == SDL_BUTTON_RIGHT ||
                     event.button.button == SDL_BUTTON_MIDDLE) &&
                    !ImGui::GetIO().WantCaptureMouse) {
                    m_panActive = true;
                    m_lastPanX = event.button.x;
                    m_lastPanY = event.button.y;
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_RIGHT ||
                    event.button.button == SDL_BUTTON_MIDDLE) {
                    m_panActive = false;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (m_panActive) {
                    const float dx = event.motion.x - m_lastPanX;
                    const float dy = event.motion.y - m_lastPanY;
                    m_camera.Move({-dx / m_camera.GetZoom(), -dy / m_camera.GetZoom()});
                    m_lastPanX = event.motion.x;
                    m_lastPanY = event.motion.y;
                }
                break;

            default:
                break;
        }
    }
}

void LegendMapEditorApp::HandleMapEditing() {
    if (!m_map) {
        m_prevLeftMouseDown = false;
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        m_prevLeftMouseDown = false;
        return;
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    const bool lmb = (buttons & SDL_BUTTON_LMASK) != 0;
    const bool rmb = (buttons & SDL_BUTTON_RMASK) != 0;

    const legend::math::Vector2 world =
        m_camera.ScreenToWorld({mouseX, mouseY}, m_viewportW, m_viewportH);
    const legend::map::TilePoint tile =
        legend::map::WorldToTile(world.x, world.y, static_cast<float>(m_map->GetTileSize()));

    if (m_mode == EditMode::Ground) {
        if (lmb) {
            // Ground 编辑只负责 Ground：Water 的阻挡由 Terrain 派生，
            // Manual Collision 数据完全不跟随 Ground 编辑改变
            if (m_map->SetGroundTile(tile.x, tile.y, m_selectedTile)) {
                m_mapDirty = true;
            }
        }
    } else if (m_mode == EditMode::Collision) {
        if (lmb) {
            m_map->GetCollision().SetBlocked(tile.x, tile.y, true);
            m_mapDirty = true;
        }
        if (rmb) {
            m_map->GetCollision().SetBlocked(tile.x, tile.y, false);
            m_mapDirty = true;
        }
    } else { // Objects
        if (lmb && !m_prevLeftMouseDown) {
            // 先尝试选择（从最上层开始）
            const legend::map::MapObject* hit = nullptr;
            const auto& objects = m_map->GetObjects().Objects();
            for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
                if (it->ContainsPoint(world.x, world.y)) {
                    hit = &*it;
                    break;
                }
            }
            if (hit != nullptr) {
                m_selectedObjectId = hit->id;
                LOG_INFO("Editor: selected object " + std::to_string(hit->id) + " '" +
                         hit->name + "'.");
            } else {
                legend::map::MapObject object = MakeObjectForType(m_selectedObjectType);
                object.id = m_map->GetObjects().GetMaxObjectId() + 1;
                object.name = m_selectedObjectType + "_" + std::to_string(object.id);
                object.x = world.x;
                object.y = world.y;
                // SpawnObject 自动维护 Object Collision 引用计数（三源碰撞）
                m_map->SpawnObject(object);
                if (object.occluder) {
                    m_map->GetOcclusion().AddOccluder(object.id);
                }
                m_selectedObjectId = object.id;
                m_mapDirty = true;
                LOG_INFO("Editor: placed object '" + object.name + "' at (" +
                         std::to_string(object.x) + "," + std::to_string(object.y) + ").");
            }
        }
    }
    m_prevLeftMouseDown = lmb;
}

void LegendMapEditorApp::DrawUI() {
    DrawMenuBar();
    if (m_workspace == Workspace::World) {
        DrawWorldWorkspace();
        if (m_showWorldOpenDialog) {
            ImGui::OpenPopup("Open World Data");
            m_showWorldOpenDialog = false;
        }
        if (ImGui::BeginPopupModal("Open World Data", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("##worlddir", m_pathBuffer, sizeof(m_pathBuffer));
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                OpenWorldDir(m_pathBuffer);
                if (!m_worldMessageIsError) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            if (!m_worldMessage.empty()) {
                ImGui::TextColored(
                    m_worldMessageIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                          : ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    "%s", m_worldMessage.c_str());
            }
            ImGui::EndPopup();
        }
        if (m_showWorldSaveAsDialog) {
            ImGui::OpenPopup("Save World As");
            m_showWorldSaveAsDialog = false;
        }
        if (ImGui::BeginPopupModal("Save World As", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("##worlddiras", m_pathBuffer, sizeof(m_pathBuffer));
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                // Save As：先重定向目录，再走同一原子保存路径。
                m_world->SetDirectory(m_pathBuffer);
                std::string saveError;
                if (m_world->Save(saveError)) {
                    m_worldMessage = "World data saved to '" + std::string(m_pathBuffer) + "'.";
                    m_worldMessageIsError = false;
                    m_worldDir = m_pathBuffer;
                    ImGui::CloseCurrentPopup();
                } else {
                    m_worldMessage = "Save failed: " + saveError;
                    m_worldMessageIsError = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            if (!m_worldMessage.empty()) {
                ImGui::TextColored(
                    m_worldMessageIsError ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                          : ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    "%s", m_worldMessage.c_str());
            }
            ImGui::EndPopup();
        }
    } else {
        DrawPalettePanel();
        DrawInfoPanel();
    }

    // 22.16：未保存退出确认（请求时打开一次）。
    {
        const bool tileDirty = m_workspace == Workspace::TileMap && m_mapDirty;
        const bool worldDirty = m_workspace == Workspace::World && m_world &&
                                m_world->IsDirty();
        if (m_requestQuit && !m_forceQuit && (tileDirty || worldDirty) &&
            !m_confirmExitShown) {
            m_confirmExitShown = true;
            ImGui::OpenPopup("Quit without saving?");
        }
    }
    if (ImGui::BeginPopupModal("Quit without saving?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("There are unsaved changes. Quit anyway?");
        if (ImGui::Button("Quit", ImVec2(120, 0))) {
            m_forceQuit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_confirmExitDismissed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_showOpenDialog) {
        ImGui::OpenPopup("Open Map");
        m_showOpenDialog = false;
    }
    if (ImGui::BeginPopupModal("Open Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("##openpath", m_pathBuffer, sizeof(m_pathBuffer));
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (OpenMap(m_pathBuffer)) {
                ImGui::CloseCurrentPopup();
                m_dialogError.clear();
            } else {
                m_dialogError = "Failed to open map (see Logs/latest.log).";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            m_dialogError.clear();
        }
        if (!m_dialogError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_dialogError.c_str());
        }
        ImGui::EndPopup();
    }

    if (m_showSaveAsDialog) {
        ImGui::OpenPopup("Save As");
        m_showSaveAsDialog = false;
    }
    if (ImGui::BeginPopupModal("Save As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("##savepath", m_pathBuffer, sizeof(m_pathBuffer));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (SaveMapTo(m_pathBuffer)) {
                ImGui::CloseCurrentPopup();
                m_dialogError.clear();
            } else {
                m_dialogError = "Failed to save map (see Logs/latest.log).";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            m_dialogError.clear();
        }
        if (!m_dialogError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_dialogError.c_str());
        }
        ImGui::EndPopup();
    }
}

void LegendMapEditorApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (m_workspace == Workspace::World) {
            if (ImGui::MenuItem("New World")) {
                m_world->NewFromDefaults();
                m_worldLoaded = true;
                m_worldMessage = "New world created (unsaved). Use Save World As to pick a directory.";
                m_worldMessageIsError = false;
            }
            if (ImGui::MenuItem("Open World Data...", "Ctrl+O")) {
                std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s", m_worldDir.c_str());
                m_showWorldOpenDialog = true;
            }
            if (ImGui::MenuItem("Save World", "Ctrl+S", false,
                                m_worldLoaded && !m_world->HasErrors())) {
                std::string error;
                if (m_world->Save(error)) {
                    m_worldMessage = "World data saved atomically (backup rotated).";
                    m_worldMessageIsError = false;
                } else {
                    m_worldMessage = "Save failed: " + error;
                    m_worldMessageIsError = true;
                }
            }
            if (ImGui::MenuItem("Save World As...", nullptr, false, m_worldLoaded)) {
                std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s", m_worldDir.c_str());
                m_showWorldSaveAsDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Tile Map...", "Ctrl+M")) {
                m_workspace = Workspace::TileMap;
                std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s",
                              m_mapPath.empty() ? "Assets/Maps/TestMap/map.json"
                                                : m_mapPath.c_str());
                m_showOpenDialog = true;
            }
        } else {
            if (ImGui::MenuItem("New Map")) {
                NewMap();
            }
            if (ImGui::MenuItem("Open Map...", "Ctrl+O")) {
                std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s",
                              m_mapPath.empty() ? "Assets/Maps/TestMap/map.json" : m_mapPath.c_str());
                m_showOpenDialog = true;
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {
                if (m_mapPath.empty()) {
                    std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s",
                                  "Assets/Maps/TestMap/map.json");
                    m_showSaveAsDialog = true;
                } else {
                    SaveMapTo(m_mapPath);
                }
            }
            if (ImGui::MenuItem("Save As...")) {
                std::snprintf(m_pathBuffer, sizeof(m_pathBuffer), "%s",
                              m_mapPath.empty() ? "Assets/Maps/TestMap/map.json" : m_mapPath.c_str());
                m_showSaveAsDialog = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open World Data")) {
                m_workspace = Workspace::World;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            m_requestQuit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (m_workspace == Workspace::World) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_world->CanUndo())) {
                m_world->Undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_world->CanRedo())) {
                m_world->Redo();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Selected", "Del", false,
                                m_world->GetSelection().type !=
                                    editor::WorldDocument::ObjectType::None)) {
                m_world->RemoveSelected();
            }
            if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false,
                                m_world->GetSelection().type !=
                                    editor::WorldDocument::ObjectType::None)) {
                m_world->DuplicateSelected();
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("World Workspace", nullptr, m_workspace == Workspace::World)) {
            m_workspace = Workspace::World;
        }
        if (ImGui::MenuItem("Tile Map Workspace", nullptr, m_workspace == Workspace::TileMap)) {
            m_workspace = Workspace::TileMap;
        }
        ImGui::Separator();
        if (m_workspace == Workspace::World) {
            ImGui::Checkbox("World Grid", &m_showWorldGrid);
            if (ImGui::MenuItem("Reset World View")) {
                m_worldOriginX = -100.0f;
                m_worldOriginY = -100.0f;
                m_worldZoom = 0.45f;
            }
        } else {
            ImGui::Checkbox("Collision Overlay", &m_showCollisionOverlay);
            ImGui::Checkbox("Grid", &m_showGrid);
            if (ImGui::MenuItem("Reset Zoom")) {
                m_camera.SetZoom(kDefaultZoom);
            }
        }
        ImGui::EndMenu();
    }
    if (m_workspace == Workspace::World) {
        if (ImGui::BeginMenu("World")) {
            if (ImGui::MenuItem("Validate World")) {
                // 文档模型每次 Mutate 后实时校验；此处强制重算并汇报。
                m_world->Mutate([](WorldDataSet&) {});
                m_worldMessage = m_world->HasErrors()
                                     ? "Validation FAILED: " + m_world->ValidationErrors().front()
                                     : "Validation OK (no errors).";
                m_worldMessageIsError = m_world->HasErrors();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run")) {
            if (ImGui::MenuItem("Launch WorldServer")) {
                LaunchWorldServer();
            }
            ImGui::EndMenu();
        }
    }
    ImGui::EndMainMenuBar();
}

void LegendMapEditorApp::DrawPalettePanel() {
    ImGui::SetNextWindowPos(ImVec2(8, 32), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(250, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Palette")) {
        ImGui::End();
        return;
    }

    const char* modeNames[] = {"Ground", "Collision", "Objects"};
    for (int i = 0; i < 3; ++i) {
        if (ImGui::RadioButton(modeNames[i], static_cast<int>(m_mode) == i)) {
            m_mode = static_cast<EditMode>(i);
        }
    }
    ImGui::Separator();

    if (m_mode == EditMode::Ground) {
        ImGui::TextUnformatted("LMB paint / drag to draw:");
        struct TileDef {
            const char* name;
            uint16_t id;
            ImVec4 color;
        };
        const TileDef tiles[] = {
            {"Grass", 1, ImVec4(0.34f, 0.60f, 0.29f, 1.0f)},
            {"Dirt", 2, ImVec4(0.66f, 0.49f, 0.31f, 1.0f)},
            {"Stone", 3, ImVec4(0.55f, 0.55f, 0.58f, 1.0f)},
            {"Water", 4, ImVec4(0.24f, 0.43f, 0.78f, 1.0f)},
        };
        for (const TileDef& tile : tiles) {
            ImGui::PushStyleColor(ImGuiCol_Button, tile.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tile.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, tile.color);
            if (ImGui::Button(tile.name, ImVec2(180, 0))) {
                m_selectedTile = tile.id;
            }
            ImGui::PopStyleColor(3);
            if (m_selectedTile == tile.id) {
                ImGui::SameLine();
                ImGui::TextUnformatted("<");
            }
        }
    } else if (m_mode == EditMode::Collision) {
        ImGui::TextUnformatted("LMB: set blocked");
        ImGui::TextUnformatted("RMB: clear blocked");
        ImGui::Separator();
        ImGui::TextUnformatted("Enable View > Collision Overlay");
        ImGui::TextUnformatted("to see blocked tiles.");
    } else {
        ImGui::TextUnformatted("LMB on empty: place object");
        ImGui::TextUnformatted("LMB on object: select");
        ImGui::Separator();
        for (const char* type : {"tree", "rock", "building"}) {
            if (ImGui::Button(type, ImVec2(180, 0))) {
                m_selectedObjectType = type;
            }
            if (m_selectedObjectType == type) {
                ImGui::SameLine();
                ImGui::TextUnformatted("<");
            }
        }
        if (m_selectedObjectId != 0) {
            ImGui::Separator();
            ImGui::Text("Selected id: %u", m_selectedObjectId);
            if (ImGui::Button("Delete Selected", ImVec2(180, 0))) {
                if (const legend::map::MapObject* object =
                        m_map->GetObjects().FindObject(m_selectedObjectId)) {
                    const std::string name = object->name;
                    // DespawnObject 只递减 Object Collision 引用计数，
                    // 不会误清 Water 地形碰撞或其他物件的阻挡
                    m_map->DespawnObject(m_selectedObjectId);
                    m_map->GetOcclusion().RemoveOccluder(m_selectedObjectId);
                    m_mapDirty = true;
                    m_selectedObjectId = 0;
                    LOG_INFO("Editor: deleted object '" + name + "'.");
                }
            }
        }
    }
    ImGui::End();
}

void LegendMapEditorApp::DrawInfoPanel() {
    ImGui::SetNextWindowPos(ImVec2(8, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 220), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map Info")) {
        ImGui::End();
        return;
    }
    if (m_map) {
        ImGui::Text("Name: %s", m_map->GetName().c_str());
        ImGui::Text("Size: %d x %d tiles (%.0f x %.0f world)", m_map->GetWidth(),
                    m_map->GetHeight(), m_map->GetWorldWidth(), m_map->GetWorldHeight());
        ImGui::Text("Objects: %d", static_cast<int>(m_map->GetObjects().Objects().size()));
        ImGui::Text("Path: %s", m_mapPath.empty() ? "[unsaved]" : m_mapPath.c_str());
        ImGui::Text("Unsaved changes: %s", m_mapDirty ? "yes" : "no");
        const auto& stats = m_mapRenderer.GetLastFrameStats();
        ImGui::Text("Chunks: %d  Tiles: %d  DC: %d", stats.visibleChunks, stats.renderedTiles,
                    stats.drawCalls);
    } else {
        ImGui::TextUnformatted("No map loaded.");
    }
    ImGui::Text("Camera: %.0f, %.0f   Zoom: %.2f", m_camera.GetPosition().x,
                m_camera.GetPosition().y, m_camera.GetZoom());
    ImGui::End();
}

bool LegendMapEditorApp::OpenMap(const std::string& path) {
    auto map = legend::map::MapLoader::Load(path);
    if (!map) {
        return false;
    }
    m_map = map;
    m_mapPath = path;
    m_mapDirty = false;
    m_selectedObjectId = 0;
    m_camera.SetPosition({map->GetWorldWidth() * 0.5f, map->GetWorldHeight() * 0.5f});
    return true;
}

bool LegendMapEditorApp::SaveMapTo(const std::string& path) {
    if (!m_map) {
        return false;
    }
    if (legend::map::MapLoader::Save(*m_map, path)) {
        m_mapPath = path;
        m_mapDirty = false;
        return true;
    }
    return false;
}

void LegendMapEditorApp::NewMap() {
    auto map = std::make_shared<legend::map::Map>();
    map->SetName("NewMap");
    map->SetSize(kDefaultMapTiles, kDefaultMapTiles, 64);
    for (int y = 0; y < kDefaultMapTiles; ++y) {
        for (int x = 0; x < kDefaultMapTiles; ++x) {
            map->SetGroundTile(x, y, static_cast<uint16_t>(legend::map::TileId::Grass));
        }
    }
    for (int x = 0; x < kDefaultMapTiles; ++x) {
        map->GetCollision().SetBlocked(x, 0, true);
        map->GetCollision().SetBlocked(x, kDefaultMapTiles - 1, true);
    }
    for (int y = 0; y < kDefaultMapTiles; ++y) {
        map->GetCollision().SetBlocked(0, y, true);
        map->GetCollision().SetBlocked(kDefaultMapTiles - 1, y, true);
    }
    m_map = map;
    m_mapPath.clear();
    m_mapDirty = true;
    m_selectedObjectId = 0;
    m_camera.SetPosition({map->GetWorldWidth() * 0.5f, map->GetWorldHeight() * 0.5f});
    LOG_INFO("Editor: new 100x100 map created.");
}

void LegendMapEditorApp::UpdateWindowTitle() {
    std::string title;
    if (m_workspace == Workspace::World) {
        title = "LegendGame World Editor";
        if (m_world && m_worldLoaded) {
            title += " - " + (m_worldDir.empty() ? std::string("[unsaved]") : m_worldDir);
        }
        if (m_world && m_world->IsDirty()) {
            title += " *";
        }
    } else {
        title = "LegendGame World Editor - Tile Map";
        if (m_map) {
            title += " - " + (m_mapPath.empty() ? std::string("[unsaved]") : m_mapPath);
        }
        if (m_mapDirty) {
            title += " *";
        }
    }
    if (title != m_lastTitle) {
        m_window.SetTitle(title);
        m_lastTitle = title;
    }
}

// ---------------------------------------------------------------------------
// 阶段22：World 工作区实现（22.2~22.17）
// ---------------------------------------------------------------------------

void LegendMapEditorApp::WorldScreenToWorld(float sx, float sy, float& wx, float& wy) const {
    wx = m_worldOriginX + sx / m_worldZoom;
    wy = m_worldOriginY + sy / m_worldZoom;
}

void LegendMapEditorApp::HandleWorldShortcuts() {
    if (m_workspace != Workspace::World || m_world == nullptr) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }
    const bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        m_world->Undo();
    }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) ||
                 (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyShift))) {
        m_world->Redo();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        m_world->DuplicateSelected();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (m_worldLoaded && !m_world->HasErrors()) {
            std::string error;
            m_worldMessage = m_world->Save(error)
                                 ? "World data saved atomically (backup rotated)."
                                 : "Save failed: " + error;
            m_worldMessageIsError = m_worldMessage.rfind("Save failed", 0) == 0;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        m_world->RemoveSelected();
    }
}

void LegendMapEditorApp::DrawWorldWorkspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuH = ImGui::GetFrameHeightWithSpacing();
    ImGui::SetNextWindowPos(ImVec2(0, menuH));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - menuH + 6.0f));
    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##WorldWorkspace", nullptr, hostFlags)) {
        ImGui::End();
        return;
    }
    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float leftW = 280.0f;
    const float rightW = 350.0f;
    const float bottomH = 170.0f;

    // 左：World Tree
    ImGui::BeginChild("##wtree", ImVec2(leftW, availH), ImGuiChildFlags_Borders);
    DrawWorldTree();
    ImGui::EndChild();
    ImGui::SameLine();

    // 中：Canvas + 底部 Validation/Console/Status
    const float centerX = availW - leftW - rightW - 12.0f;
    ImGui::BeginChild("##wcenter", ImVec2(centerX, availH), 0,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##wcanvas", ImVec2(0, availH - bottomH - 8.0f),
                      ImGuiChildFlags_Borders);
    DrawWorldCanvas();
    ImGui::EndChild();
    ImGui::BeginChild("##wbottom", ImVec2(0, 0), ImGuiChildFlags_Borders);
    DrawWorldBottomPanel();
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();

    // 右：Inspector
    ImGui::BeginChild("##winspector", ImVec2(rightW, availH), ImGuiChildFlags_Borders);
    DrawWorldInspector();
    ImGui::EndChild();

    ImGui::End();
}

void LegendMapEditorApp::DrawWorldTree() {
    if (m_world == nullptr) {
        return;
    }
    const WorldDataSet& data = m_world->Data();
    ImGui::TextUnformatted("World Tree");
    ImGui::Separator();
    ImGui::TextDisabled("dir: %s", m_worldDir.c_str());

    using OT = editor::WorldDocument::ObjectType;
    const auto selectRow = [this](OT type, std::uint32_t id, const std::string& label) {
        const bool selected =
            m_world->GetSelection().type == type && m_world->GetSelection().id == id;
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_world->SetSelection(type, id);
        }
        if (selected && ImGui::IsItemFocused()) {
            ImGui::SetItemDefaultFocus();
        }
    };

    if (ImGui::TreeNodeEx("##maps", ImGuiTreeNodeFlags_DefaultOpen, "Maps (%d)",
                          static_cast<int>(data.maps.size()))) {
        for (const auto& map : data.maps) {
            selectRow(OT::Map, map.mapId,
                      std::to_string(map.mapId) + " - " + map.name);
        }
        if (ImGui::Button("+ Add Map")) {
            MapDefinition map;
            map.mapId = m_world->SuggestMapId();
            map.name = "New Map " + std::to_string(map.mapId);
            map.type = MapType::Field;
            map.minX = 0.0f;
            map.minY = 0.0f;
            map.maxX = 2000.0f;
            map.maxY = 2000.0f;
            map.spawnX = 1000.0f;
            map.spawnY = 1000.0f;
            map.respawnX = 1000.0f;
            map.respawnY = 1000.0f;
            const std::uint32_t id = map.mapId;
            m_world->Mutate([&](WorldDataSet& d) { d.maps.push_back(map); });
            m_world->SetSelection(OT::Map, id);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("##npcs", ImGuiTreeNodeFlags_DefaultOpen, "NPCs (%d)",
                          static_cast<int>(data.npcs.size()))) {
        for (const auto& npc : data.npcs) {
            selectRow(OT::Npc, npc.npcDefinitionId,
                      std::to_string(npc.npcDefinitionId) + " - " + npc.name);
        }
        if (ImGui::Button("+ Add NPC")) {
            NpcDefinition npc;
            npc.npcDefinitionId = m_world->SuggestNpcId();
            npc.name = "New NPC";
            npc.npcType = NpcType::QuestGiver;
            npc.mapId = 1;
            npc.spawnX = 400.0f;
            npc.spawnY = 400.0f;
            npc.interactionRange = 120.0f;
            npc.visualId = npc.npcDefinitionId;
            const std::uint32_t id = npc.npcDefinitionId;
            m_world->Mutate([&](WorldDataSet& d) { d.npcs.push_back(npc); });
            m_world->SetSelection(OT::Npc, id);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("##spawns", ImGuiTreeNodeFlags_DefaultOpen, "Monster Spawns (%d)",
                          static_cast<int>(data.monsterSpawns.size()))) {
        for (const auto& spawn : data.monsterSpawns) {
            selectRow(OT::Spawn, spawn.spawnId,
                      std::to_string(spawn.spawnId) + " - map " +
                          std::to_string(spawn.mapId) + " x" +
                          std::to_string(spawn.count));
        }
        if (ImGui::Button("+ Add Spawn")) {
            MonsterSpawnDefinition spawn;
            spawn.spawnId = m_world->SuggestSpawnId();
            spawn.mapId = 1;
            spawn.centerX = 1000.0f;
            spawn.centerY = 1000.0f;
            spawn.radius = 200.0f;
            spawn.count = 5;
            spawn.respawnSeconds = 8;
            const std::uint32_t id = spawn.spawnId;
            m_world->Mutate([&](WorldDataSet& d) { d.monsterSpawns.push_back(spawn); });
            m_world->SetSelection(OT::Spawn, id);
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("##portals", ImGuiTreeNodeFlags_DefaultOpen, "Portals (%d)",
                          static_cast<int>(data.portals.size()))) {
        for (const auto& portal : data.portals) {
            selectRow(OT::Portal, portal.portalId,
                      std::to_string(portal.portalId) + " - map " +
                          std::to_string(portal.sourceMapId) + " -> " +
                          std::to_string(portal.destinationMapId));
        }
        if (ImGui::Button("+ Add Portal")) {
            PortalDefinition portal;
            portal.portalId = m_world->SuggestPortalId();
            portal.sourceMapId = 1;
            portal.x = 500.0f;
            portal.y = 500.0f;
            portal.interactionRadius = 100.0f;
            portal.destinationMapId = 1;
            portal.destinationX = 500.0f;
            portal.destinationY = 500.0f;
            portal.minLevel = 1;
            portal.goldCost = 0;
            const std::uint32_t id = portal.portalId;
            m_world->Mutate([&](WorldDataSet& d) { d.portals.push_back(portal); });
            m_world->SetSelection(OT::Portal, id);
        }
        ImGui::TreePop();
    }
}

void LegendMapEditorApp::DrawWorldCanvas() {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // 背景
    draw->AddRectFilled(canvasP0, canvasP1, IM_COL32(24, 26, 32, 255));

    // ---- 网格（100 世界单位）----
    if (m_showWorldGrid) {
        const float step = 100.0f * m_worldZoom;
        if (step > 8.0f) {
            const float startX = std::fmod(-m_worldOriginX * m_worldZoom, step);
            for (float x = startX; x < canvasSize.x; x += step) {
                draw->AddLine(ImVec2(canvasP0.x + x, canvasP0.y),
                              ImVec2(canvasP0.x + x, canvasP1.y),
                              IM_COL32(60, 64, 76, 255));
            }
            const float startY = std::fmod(-m_worldOriginY * m_worldZoom, step);
            for (float y = startY; y < canvasSize.y; y += step) {
                draw->AddLine(ImVec2(canvasP0.x, canvasP0.y + y),
                              ImVec2(canvasP1.x, canvasP0.y + y),
                              IM_COL32(60, 64, 76, 255));
            }
        }
    }

    // ---- 交互：缩放/平移/拾取/拖拽 ----
    if (ImGui::IsWindowHovered()) {
        const float wheel = io.MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = m_worldZoom;
            m_worldZoom *= wheel > 0.0f ? 1.1f : 1.0f / 1.1f;
            m_worldZoom = std::clamp(m_worldZoom, 0.05f, 4.0f);
            // 鼠标锚点缩放：保持鼠标下世界坐标不动
            const ImVec2 mouse(io.MousePos.x - canvasP0.x, io.MousePos.y - canvasP0.y);
            const float wx = m_worldOriginX + mouse.x / oldZoom;
            const float wy = m_worldOriginY + mouse.y / oldZoom;
            m_worldOriginX = wx - mouse.x / m_worldZoom;
            m_worldOriginY = wy - mouse.y / m_worldZoom;
        }
        const float localX = io.MousePos.x - canvasP0.x;
        const float localY = io.MousePos.y - canvasP0.y;
        WorldScreenToWorld(localX, localY, m_mouseWorldX, m_mouseWorldY);

        // 平移（中键/右键拖）
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const ImVec2 delta = io.MouseDelta;
            m_worldOriginX -= delta.x / m_worldZoom;
            m_worldOriginY -= delta.y / m_worldZoom;
        }

        // 拾取 / 拖拽（左键）
        using OT = editor::WorldDocument::ObjectType;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            std::uint32_t hitId = 0;
            const OT hit = PickObjectAt(m_mouseWorldX, m_mouseWorldY, hitId);
            m_world->SetSelection(hit, hitId);
            if (hit == OT::Npc || hit == OT::Spawn || hit == OT::Portal) {
                m_worldDrag = WorldDragKind::Object;
                m_dragStartWX = m_mouseWorldX;
                m_dragStartWY = m_mouseWorldY;
                m_dragCurWX = m_mouseWorldX;
                m_dragCurWY = m_mouseWorldY;
            }
        } else if (m_worldDrag == WorldDragKind::Object &&
                   ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_dragCurWX = m_mouseWorldX;
            m_dragCurWY = m_mouseWorldY;
        } else if (m_worldDrag == WorldDragKind::Object &&
                   ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // release 一次性 Mutate（拖拽过程只动临时位置，undo 栈不被刷满）
            const float dx = m_dragCurWX - m_dragStartWX;
            const float dy = m_dragCurWY - m_dragStartWY;
            MutateSelectedPos(m_dragCurWX, m_dragCurWY);
            (void)dx;
            (void)dy;
            m_worldDrag = WorldDragKind::None;
        }
    }

    DrawWorldObjects();

    // 左上角提示
    draw->AddText(canvasP0, IM_COL32(160, 170, 190, 255),
                  "LMB: select/drag  MMB/RMB drag: pan  Wheel: zoom  Del/Ctrl+D/Ctrl+Z/Ctrl+Y");
}

void LegendMapEditorApp::DrawWorldObjects() {
    if (m_world == nullptr) {
        return;
    }
    const ImVec2 canvasP0 = ImGui::GetWindowPos();       // child 窗口原点（含边框 1px）
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto w2s = [&](float wx, float wy) {
        return ImVec2(canvasP0.x + (wx - m_worldOriginX) * m_worldZoom,
                      canvasP0.y + (wy - m_worldOriginY) * m_worldZoom);
    };

    const WorldDataSet& data = m_world->Data();
    using OT = editor::WorldDocument::ObjectType;
    const auto sel = m_world->GetSelection();

    // ---- Map bounds（不同图不同色相）----
    int mapIndex = 0;
    for (const auto& map : data.maps) {
        const ImU32 palette[] = {IM_COL32(70, 130, 90, 255), IM_COL32(90, 120, 170, 255),
                                 IM_COL32(160, 120, 70, 255), IM_COL32(120, 90, 150, 255)};
        const ImU32 color = palette[mapIndex % 4];
        const ImVec2 a = w2s(map.minX, map.minY);
        const ImVec2 b = w2s(map.maxX, map.maxY);
        draw->AddRect(a, b, color, 0.0f, 0, 2.0f);
        draw->AddRectFilled(a, b, IM_COL32(color & 0xFF, (color >> 8) & 0xFF,
                                           (color >> 16) & 0xFF, 22));
        // Spawn（绿十字）与 Respawn（橙十字）
        const ImVec2 sp = w2s(map.spawnX, map.spawnY);
        const ImVec2 rp = w2s(map.respawnX, map.respawnY);
        draw->AddLine(ImVec2(sp.x - 7, sp.y), ImVec2(sp.x + 7, sp.y), IM_COL32(90, 220, 120, 255), 2.0f);
        draw->AddLine(ImVec2(sp.x, sp.y - 7), ImVec2(sp.x, sp.y + 7), IM_COL32(90, 220, 120, 255), 2.0f);
        draw->AddLine(ImVec2(rp.x - 7, rp.y), ImVec2(rp.x + 7, rp.y), IM_COL32(230, 150, 60, 255), 2.0f);
        draw->AddLine(ImVec2(rp.x, rp.y - 7), ImVec2(rp.x, rp.y + 7), IM_COL32(230, 150, 60, 255), 2.0f);
        draw->AddText(ImVec2(a.x + 6, a.y + 4), color,
                      (std::to_string(map.mapId) + " " + map.name).c_str());
        draw->AddText(ImVec2(sp.x + 10, sp.y - 8), IM_COL32(90, 220, 120, 255), "Spawn");
        draw->AddText(ImVec2(rp.x + 10, rp.y + 2), IM_COL32(230, 150, 60, 255), "Respawn");
        ++mapIndex;
    }

    // ---- Monster Spawn（圆形刷怪范围 + 中心）----
    for (const auto& spawn : data.monsterSpawns) {
        const bool selected = sel.type == OT::Spawn && sel.id == spawn.spawnId;
        float cx = spawn.centerX;
        float cy = spawn.centerY;
        if (selected && m_worldDrag == WorldDragKind::Object) {
            cx = m_dragCurWX;
            cy = m_dragCurWY;
        }
        const ImU32 ringColor = selected ? IM_COL32(255, 220, 80, 255)
                                         : IM_COL32(220, 160, 60, 255);
        const ImVec2 c = w2s(cx, cy);
        const float r = spawn.radius * m_worldZoom;
        if (r > 2.0f) {
            draw->AddCircle(c, r, ringColor, 64, 1.5f);
            draw->AddCircleFilled(c, r, IM_COL32(220, 160, 60, 26));
        }
        draw->AddCircleFilled(c, 4.0f, ringColor);
        char label[64];
        std::snprintf(label, sizeof(label), "Spawn %u x%u", spawn.spawnId, spawn.count);
        draw->AddText(ImVec2(c.x + 8, c.y + 8), ringColor, label);
    }

    // ---- Portal（发光门：菱形 + 目的地箭头标注）----
    for (const auto& portal : data.portals) {
        const bool selected = sel.type == OT::Portal && sel.id == portal.portalId;
        float px = portal.x;
        float py = portal.y;
        if (selected && m_worldDrag == WorldDragKind::Object) {
            px = m_dragCurWX;
            py = m_dragCurWY;
        }
        const ImU32 color = selected ? IM_COL32(120, 255, 220, 255)
                                     : IM_COL32(80, 210, 190, 255);
        const ImVec2 p = w2s(px, py);
        const float s = 11.0f;
        draw->AddQuad(p, ImVec2(p.x + s, p.y + s), ImVec2(p.x, p.y + 2 * s),
                      ImVec2(p.x - s, p.y + s), color, 2.0f);
        draw->AddCircleFilled(p, s * 0.4f, IM_COL32(120, 255, 220, 90));
        char label[64];
        std::snprintf(label, sizeof(label), "Portal %u -> map %u%s", portal.portalId,
                      portal.destinationMapId, portal.enabled ? "" : " [off]");
        draw->AddText(ImVec2(p.x + 14, p.y - 4), color, label);
    }

    // ---- NPC（圆点 + 名字）----
    for (const auto& npc : data.npcs) {
        const bool selected = sel.type == OT::Npc && sel.id == npc.npcDefinitionId;
        float nx = npc.spawnX;
        float ny = npc.spawnY;
        if (selected && m_worldDrag == WorldDragKind::Object) {
            nx = m_dragCurWX;
            ny = m_dragCurWY;
        }
        const ImU32 color = selected ? IM_COL32(255, 230, 120, 255)
                                     : IM_COL32(240, 200, 90, 255);
        const ImVec2 p = w2s(nx, ny);
        draw->AddCircleFilled(p, 6.0f, color);
        draw->AddCircle(p, 9.0f, color, 0, 1.5f);
        char label[96];
        std::snprintf(label, sizeof(label), "%u %s", npc.npcDefinitionId, npc.name.c_str());
        draw->AddText(ImVec2(p.x + 12, p.y - 6), color, label);
    }
}

editor::WorldDocument::ObjectType LegendMapEditorApp::PickObjectAt(float wx, float wy,
                                                                   std::uint32_t& outId) const {
    if (m_world == nullptr) {
        return editor::WorldDocument::ObjectType::None;
    }
    using OT = editor::WorldDocument::ObjectType;
    const WorldDataSet& data = m_world->Data();
    const float tol = 20.0f; // 世界单位命中半径（小对象优先）
    // Portal / NPC / Spawn center / Spawn ring / Map 依次。
    for (const auto& portal : data.portals) {
        const float dx = wx - portal.x;
        const float dy = wy - portal.y;
        if (dx * dx + dy * dy <= tol * tol) {
            outId = portal.portalId;
            return OT::Portal;
        }
    }
    for (const auto& npc : data.npcs) {
        const float dx = wx - npc.spawnX;
        const float dy = wy - npc.spawnY;
        if (dx * dx + dy * dy <= tol * tol) {
            outId = npc.npcDefinitionId;
            return OT::Npc;
        }
    }
    for (const auto& spawn : data.monsterSpawns) {
        const float dx = wx - spawn.centerX;
        const float dy = wy - spawn.centerY;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 <= tol * tol) {
            outId = spawn.spawnId;
            return OT::Spawn;
        }
        const float ringDist = std::sqrt(dist2) - spawn.radius;
        if (spawn.radius > 0.0f && std::fabs(ringDist) <= 15.0f) {
            outId = spawn.spawnId;
            return OT::Spawn;
        }
    }
    for (const auto& map : data.maps) {
        if (map.InBounds(wx, wy)) {
            outId = map.mapId;
            return OT::Map;
        }
    }
    return OT::None;
}

void LegendMapEditorApp::MutateSelectedPos(float wx, float wy) {
    if (m_world == nullptr) {
        return;
    }
    using OT = editor::WorldDocument::ObjectType;
    const auto sel = m_world->GetSelection();
    switch (sel.type) {
        case OT::Npc:
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.spawnX = wx;
                        npc.spawnY = wy;
                    }
                }
            });
            break;
        case OT::Spawn:
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.centerX = wx;
                        spawn.centerY = wy;
                    }
                }
            });
            break;
        case OT::Portal:
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.x = wx;
                        portal.y = wy;
                    }
                }
            });
            break;
        default:
            break;
    }
}

void LegendMapEditorApp::OpenWorldDir(const std::string& dir) {
    if (m_world == nullptr) {
        return;
    }
    std::string error;
    if (m_world->Load(dir, error)) {
        m_worldLoaded = true;
        m_worldDir = dir;
        m_worldMessage = "World data loaded from '" + dir + "'.";
        m_worldMessageIsError = false;
        m_worldOriginX = -100.0f;
        m_worldOriginY = -100.0f;
    } else {
        m_worldLoaded = false;
        m_worldMessage = "Load failed: " + error;
        m_worldMessageIsError = true;
    }
}

void LegendMapEditorApp::LaunchWorldServer() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = exePath;
    const auto slash = exeDir.find_last_of("\\/");
    if (slash != std::string::npos) {
        exeDir = exeDir.substr(0, slash);
    }
    // LegendWorldServer.exe 与编辑器同目录；工作目录 = 仓库根（Data/World 相对路径）。
    const std::string serverExe = exeDir + "\\LegendWorldServer.exe";
    const std::string workDir = exeDir + "\\..\\..";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> cmdLine(serverExe.begin(), serverExe.end());
    cmdLine.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                   workDir.c_str(), &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        m_worldMessage = "WorldServer launched (cwd '" + workDir + "').";
        m_worldMessageIsError = false;
        LOG_INFO("Editor: launched WorldServer from " + serverExe);
    } else {
        const DWORD err = GetLastError();
        m_worldMessage = "Launch WorldServer failed (Win32 error " + std::to_string(err) +
                         "). Build LegendWorldServer first.";
        m_worldMessageIsError = true;
        LOG_ERROR("Editor: LaunchWorldServer failed, error=" + std::to_string(err));
    }
}

void LegendMapEditorApp::DrawWorldInspector() {
    if (m_world == nullptr) {
        return;
    }
    ImGui::TextUnformatted("Properties Inspector");
    ImGui::Separator();
    using OT = editor::WorldDocument::ObjectType;
    const auto sel = m_world->GetSelection();
    if (sel.type == OT::None) {
        ImGui::TextDisabled("Nothing selected. Click an object on the canvas or World Tree.");
        return;
    }

    // 文本缓冲 owner 跟踪：选中对象变化 → 从文档重载。
    const auto refillBuffers = [&]() {
        m_insOwnerType = sel.type;
        m_insOwnerId = sel.id;
        m_insName[0] = m_insTitle[0] = m_insText[0] = m_insQuestIds[0] = '\0';
        if (sel.type == OT::Map) {
            if (const auto* m = m_world->FindMap(static_cast<std::uint16_t>(sel.id))) {
                std::snprintf(m_insName, sizeof(m_insName), "%s", m->name.c_str());
            }
        } else if (sel.type == OT::Npc) {
            if (const auto* n = m_world->FindNpc(sel.id)) {
                std::snprintf(m_insName, sizeof(m_insName), "%s", n->name.c_str());
                if (n->dialogueId != 0) {
                    for (const auto& d : m_world->Data().dialogues) {
                        if (d.dialogueId == n->dialogueId) {
                            std::snprintf(m_insTitle, sizeof(m_insTitle), "%s", d.title.c_str());
                            std::snprintf(m_insText, sizeof(m_insText), "%s", d.text.c_str());
                            break;
                        }
                    }
                }
                std::string ids;
                for (std::size_t i = 0; i < n->questIds.size(); ++i) {
                    ids += std::to_string(n->questIds[i]);
                    if (i + 1 < n->questIds.size()) {
                        ids += ",";
                    }
                }
                std::snprintf(m_insQuestIds, sizeof(m_insQuestIds), "%s", ids.c_str());
            }
        }
    };
    if (sel.type != m_insOwnerType || sel.id != m_insOwnerId) {
        refillBuffers();
    }

    // 地图下拉（22.8：目标地图使用 ComboBox）。
    const auto mapCombo = [&](const char* label, std::uint16_t& value) {
        if (ImGui::BeginCombo(label, [&] {
                const auto* m = m_world->FindMap(value);
                static std::string preview;
                preview = m ? (std::to_string(m->mapId) + " - " + m->name) : "?";
                return preview.c_str();
            }())) {
            for (const auto& map : m_world->Data().maps) {
                const bool selected = map.mapId == value;
                if (ImGui::Selectable((std::to_string(map.mapId) + " - " + map.name).c_str(),
                                      selected)) {
                    value = map.mapId;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    };

    // 数值编辑（InputXxx 返回 true = 提交）→ Mutate。
    if (sel.type == OT::Map) {
        const auto* m = m_world->FindMap(static_cast<std::uint16_t>(sel.id));
        if (m == nullptr) {
            ImGui::TextDisabled("Map not found.");
            return;
        }
        ImGui::Text("mapId: %u", m->mapId);
        if (ImGui::InputText("Name", m_insName, sizeof(m_insName))) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.name = m_insName;
                    }
                }
            });
        }
        int typeIdx = static_cast<int>(m->type) - 1;
        if (ImGui::Combo("Type", &typeIdx, "Town\0Field\0Dungeon\0")) {
            const auto newType = static_cast<MapType>(typeIdx + 1);
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.type = newType;
                    }
                }
            });
        }
        float bounds[4] = {m->minX, m->minY, m->maxX, m->maxY};
        if (ImGui::InputFloat4("Bounds (minX minY maxX maxY)", bounds, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.minX = bounds[0];
                        map.minY = bounds[1];
                        map.maxX = bounds[2];
                        map.maxY = bounds[3];
                    }
                }
            });
        }
        float spawn[2] = {m->spawnX, m->spawnY};
        if (ImGui::InputFloat2("Spawn", spawn, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.spawnX = spawn[0];
                        map.spawnY = spawn[1];
                    }
                }
            });
        }
        float respawn[2] = {m->respawnX, m->respawnY};
        if (ImGui::InputFloat2("Respawn", respawn, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.respawnX = respawn[0];
                        map.respawnY = respawn[1];
                    }
                }
            });
        }
    } else if (sel.type == OT::Npc) {
        const auto* n = m_world->FindNpc(sel.id);
        if (n == nullptr) {
            ImGui::TextDisabled("NPC not found.");
            return;
        }
        ImGui::Text("npcDefinitionId: %u", n->npcDefinitionId);
        if (ImGui::InputText("Name", m_insName, sizeof(m_insName))) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.name = m_insName;
                    }
                }
            });
        }
        int typeIdx = static_cast<int>(n->npcType) - 1;
        if (ImGui::Combo("NPC Type", &typeIdx, "QuestGiver\0Merchant\0Teleporter\0MultiFunction\0")) {
            const auto newType = static_cast<NpcType>(typeIdx + 1);
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.npcType = newType;
                    }
                }
            });
        }
        std::uint16_t mapId = n->mapId;
        mapCombo("Map", mapId);
        if (mapId != n->mapId) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.mapId = mapId;
                    }
                }
            });
        }
        float pos[2] = {n->spawnX, n->spawnY};
        if (ImGui::InputFloat2("Position (x y)", pos, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.spawnX = pos[0];
                        npc.spawnY = pos[1];
                    }
                }
            });
        }
        float range = n->interactionRange;
        if (ImGui::InputFloat("interactionRange", &range, 5.0f, 0.0f, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.interactionRange = range;
                    }
                }
            });
        }
        int dialogueId = static_cast<int>(n->dialogueId);
        if (ImGui::InputInt("dialogueId (0=none)", &dialogueId)) {
            const auto newId = static_cast<std::uint32_t>(std::max(0, dialogueId));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.dialogueId = newId;
                    }
                }
                if (newId != 0) {
                    // dialogueId = npcDefinitionId（阶段20 语义）：缺则补默认文本。
                    const bool exists = [&] {
                        for (const auto& d : d.dialogues) {
                            if (d.dialogueId == newId) {
                                return true;
                            }
                        }
                        return false;
                    }();
                    if (!exists) {
                        d.dialogues.push_back({newId, "Dialogue " + std::to_string(newId),
                                               "..."});
                    }
                }
            });
            refillBuffers();
        }
        if (ImGui::InputText("Dialogue Title", m_insTitle, sizeof(m_insTitle))) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& d2 : d.dialogues) {
                    if (d2.dialogueId == n->dialogueId) {
                        d2.title = m_insTitle;
                    }
                }
            });
        }
        if (ImGui::InputTextMultiline("Dialogue Text", m_insText, sizeof(m_insText))) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& d2 : d.dialogues) {
                    if (d2.dialogueId == n->dialogueId) {
                        d2.text = m_insText;
                    }
                }
            });
        }
        int shopId = static_cast<int>(n->shopId);
        if (ImGui::InputInt("shopId (0=none)", &shopId)) {
            const auto newId = static_cast<std::uint32_t>(std::max(0, shopId));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.shopId = newId;
                    }
                }
            });
        }
        int teleportId = static_cast<int>(n->teleportId);
        if (ImGui::InputInt("teleportId (0=none)", &teleportId)) {
            const auto newId = static_cast<std::uint32_t>(std::max(0, teleportId));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.teleportId = newId;
                    }
                }
            });
        }
        if (ImGui::InputText("questIds (comma sep)", m_insQuestIds, sizeof(m_insQuestIds))) {
            std::vector<std::uint32_t> ids;
            std::stringstream ss(m_insQuestIds);
            std::string token;
            while (std::getline(ss, token, ',')) {
                if (!token.empty()) {
                    ids.push_back(static_cast<std::uint32_t>(std::strtoul(token.c_str(),
                                                                          nullptr, 10)));
                }
            }
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.questIds = ids;
                    }
                }
            });
        }
        int visualId = static_cast<int>(n->visualId);
        if (ImGui::InputInt("visualId", &visualId)) {
            const auto newId = static_cast<std::uint32_t>(std::max(0, visualId));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& npc : d.npcs) {
                    if (npc.npcDefinitionId == sel.id) {
                        npc.visualId = newId;
                    }
                }
            });
        }
    } else if (sel.type == OT::Spawn) {
        const auto* s = m_world->FindSpawn(sel.id);
        if (s == nullptr) {
            ImGui::TextDisabled("Spawn not found.");
            return;
        }
        ImGui::Text("spawnId: %u", s->spawnId);
        std::uint16_t mapId = s->mapId;
        mapCombo("Map", mapId);
        if (mapId != s->mapId) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.mapId = mapId;
                    }
                }
            });
        }
        int monsterId = static_cast<int>(s->monsterDefinitionId);
        if (ImGui::InputInt("monsterDefinitionId", &monsterId)) {
            const auto newId = static_cast<std::uint32_t>(std::max(1, monsterId));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.monsterDefinitionId = newId;
                    }
                }
            });
        }
        float center[2] = {s->centerX, s->centerY};
        if (ImGui::InputFloat2("Center (x y)", center, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.centerX = center[0];
                        spawn.centerY = center[1];
                    }
                }
            });
        }
        float radius = s->radius;
        if (ImGui::InputFloat("radius", &radius, 10.0f, 0.0f, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.radius = std::max(0.0f, radius);
                    }
                }
            });
        }
        int count = static_cast<int>(s->count);
        if (ImGui::InputInt("count", &count)) {
            const auto newCount = static_cast<std::uint32_t>(std::max(0, count));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.count = newCount;
                    }
                }
            });
        }
        int respawnSeconds = static_cast<int>(s->respawnSeconds);
        if (ImGui::InputInt("respawnSeconds", &respawnSeconds)) {
            const auto newSeconds = static_cast<std::uint32_t>(std::max(1, respawnSeconds));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.respawnSeconds = newSeconds;
                    }
                }
            });
        }
        bool enabled = s->enabled;
        if (ImGui::Checkbox("enabled", &enabled)) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& spawn : d.monsterSpawns) {
                    if (spawn.spawnId == sel.id) {
                        spawn.enabled = enabled;
                    }
                }
            });
        }
    } else if (sel.type == OT::Portal) {
        const auto* p = m_world->FindPortal(sel.id);
        if (p == nullptr) {
            ImGui::TextDisabled("Portal not found.");
            return;
        }
        ImGui::Text("portalId: %u", p->portalId);
        std::uint16_t sourceMapId = p->sourceMapId;
        mapCombo("Source Map", sourceMapId);
        if (sourceMapId != p->sourceMapId) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.sourceMapId = sourceMapId;
                    }
                }
            });
        }
        float pos[2] = {p->x, p->y};
        if (ImGui::InputFloat2("Position (x y)", pos, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.x = pos[0];
                        portal.y = pos[1];
                    }
                }
            });
        }
        float radius = p->interactionRadius;
        if (ImGui::InputFloat("interactionRadius", &radius, 5.0f, 0.0f, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.interactionRadius = std::max(1.0f, radius);
                    }
                }
            });
        }
        std::uint16_t destMapId = p->destinationMapId;
        mapCombo("Destination Map", destMapId); // 22.8：ComboBox
        if (destMapId != p->destinationMapId) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.destinationMapId = destMapId;
                    }
                }
            });
        }
        float dest[2] = {p->destinationX, p->destinationY};
        if (ImGui::InputFloat2("Destination (x y)", dest, "%.1f")) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.destinationX = dest[0];
                        portal.destinationY = dest[1];
                    }
                }
            });
        }
        int minLevel = static_cast<int>(p->minLevel);
        if (ImGui::InputInt("minLevel", &minLevel)) {
            const auto newLevel = static_cast<std::uint32_t>(std::max(1, minLevel));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.minLevel = newLevel;
                    }
                }
            });
        }
        int goldCost = static_cast<int>(p->goldCost);
        if (ImGui::InputInt("goldCost", &goldCost)) {
            const auto newCost = static_cast<std::uint32_t>(std::max(0, goldCost));
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.goldCost = newCost;
                    }
                }
            });
        }
        bool enabled = p->enabled;
        if (ImGui::Checkbox("enabled", &enabled)) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& portal : d.portals) {
                    if (portal.portalId == sel.id) {
                        portal.enabled = enabled;
                    }
                }
            });
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Duplicate (Ctrl+D)")) {
        m_world->DuplicateSelected();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete (Del)")) {
        m_world->RemoveSelected();
    }
}

void LegendMapEditorApp::DrawWorldBottomPanel() {
    if (m_world == nullptr) {
        return;
    }
    // ---- Validation（22.13：Error 显示 + Save 禁用）----
    ImGui::TextUnformatted("Validation");
    if (m_world->HasErrors()) {
        for (const auto& error : m_world->ValidationErrors()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[Error] %s", error.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "No validation errors.");
    }
    ImGui::Separator();
    // ---- Console ----
    if (!m_worldMessage.empty()) {
        ImGui::TextColored(m_worldMessageIsError
                               ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                               : ImVec4(0.55f, 0.85f, 1.0f, 1.0f),
                           "%s", m_worldMessage.c_str());
    }
    ImGui::Separator();
    // ---- 操作条 ----
    ImGui::BeginDisabled(m_world->HasErrors() || !m_worldLoaded);
    if (ImGui::Button("Save World (Ctrl+S)")) {
        std::string error;
        if (m_world->Save(error)) {
            m_worldMessage = "World data saved atomically (backup rotated).";
            m_worldMessageIsError = false;
        } else {
            m_worldMessage = "Save failed: " + error;
            m_worldMessageIsError = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Validate World")) {
        m_world->Mutate([](WorldDataSet&) {});
        m_worldMessage = m_world->HasErrors()
                             ? "Validation FAILED: " + m_world->ValidationErrors().front()
                             : "Validation OK (no errors).";
        m_worldMessageIsError = m_world->HasErrors();
    }
    ImGui::SameLine();
    if (ImGui::Button("Launch WorldServer (Run)")) {
        LaunchWorldServer();
    }
    // ---- Status ----
    const WorldDataSet& data = m_world->Data();
    ImGui::Text(
        "Status: maps=%d npcs=%d spawns=%d portals=%d  dirty=%s  zoom=%.2f  mouse=(%.0f, %.0f)",
        static_cast<int>(data.maps.size()), static_cast<int>(data.npcs.size()),
        static_cast<int>(data.monsterSpawns.size()), static_cast<int>(data.portals.size()),
        m_world->IsDirty() ? "yes" : "no", m_worldZoom, m_mouseWorldX, m_mouseWorldY);
}
