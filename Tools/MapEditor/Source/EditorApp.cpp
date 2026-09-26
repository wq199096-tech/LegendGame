#include "EditorApp.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "Engine/Debug/Logger.h"
#include "Engine/Map/MapLoader.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Render/Texture.h"

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

        HandleMapEditing();

        // ---- 地图绘制（游戏与编辑器共用 MapRenderer/SpriteBatch） ----
        m_renderer.BeginFrame(m_camera);
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

        // ---- ImGui 绘制 ----
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        DrawUI();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        m_renderer.EndFrame(); // swap

        if (m_requestQuit) {
            quit = true;
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

    if (!m_window.Create("LegendMapEditor - V0.2", 1440, 900)) {
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
                quit = true;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (!ImGui::GetIO().WantCaptureMouse && event.wheel.y != 0.0f) {
                    m_camera.SetZoom(m_camera.GetZoom() *
                                     (event.wheel.y > 0.0f ? 1.1f : 1.0f / 1.1f));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if ((event.button.button == SDL_BUTTON_RIGHT ||
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
    DrawPalettePanel();
    DrawInfoPanel();

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
        if (ImGui::MenuItem("Exit")) {
            m_requestQuit = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::Checkbox("Collision Overlay", &m_showCollisionOverlay);
        ImGui::Checkbox("Grid", &m_showGrid);
        if (ImGui::MenuItem("Reset Zoom")) {
            m_camera.SetZoom(kDefaultZoom);
        }
        ImGui::EndMenu();
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
    std::string title = "LegendMapEditor - V0.2";
    if (m_map) {
        title += " - " + (m_mapPath.empty() ? std::string("[unsaved]") : m_mapPath);
    }
    if (m_mapDirty) {
        title += " *";
    }
    if (title != m_lastTitle) {
        m_window.SetTitle(title);
        m_lastTitle = title;
    }
}
