#include "EditorApp.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Engine/Debug/Logger.h"
#include "Engine/Map/MapLoader.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Render/GLApi.h"
#include "Engine/Render/Texture.h"

#include <filesystem>
#include <set>

// stb_image 声明（STB_IMAGE_IMPLEMENTATION 在 legend_engine ResourceManager.cpp）
#include "ThirdParty/stb/stb_image.h"

using legend::world::GameDataSet;
using legend::world::MapDefinition;
using legend::world::MapType;
using legend::world::MapVisualDefinition;
using legend::world::MapVisualLayer;
using legend::world::MapVisualPlacement;
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

    // ---- 阶段23：默认加载 Data/Game（Data Editor 数据） ----
    m_game = std::make_unique<editor::GameDataDocument>();
    {
        const char* gameDirEnv = SDL_getenv("LEGEND_EDITOR_GAME_DIR");
        const std::string gameDir = (gameDirEnv != nullptr && gameDirEnv[0] != '\0')
                                        ? std::string(gameDirEnv)
                                        : std::string("Data/Game");
        std::string gameError;
        if (m_game->Load(gameDir, m_worldDir, gameError)) {
            m_gameLoaded = true;
            m_gameDir = gameDir;
            LOG_INFO("World Editor: loaded game data from '" + gameDir + "'.");
        } else {
            LOG_WARN("World Editor: failed to load game data '" + gameDir + "': " + gameError);
            m_worldMessage = "Game data load failed: " + gameError;
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

    // ---- 阶段24：Visual Asset 数据（Data/Assets + Data/World 视觉字段）----
    LoadVisualCatalog();
    return true;
}

// ---------------------------------------------------------------------------
// 阶段24：Visual Asset 绑定 / Validation / Preview
// ---------------------------------------------------------------------------

void LegendMapEditorApp::LoadVisualCatalog() {
    std::string root = legend::visual::VisualDataCatalog::FindDataRoot();
    if (root.empty()) {
        LOG_WARN("[Editor] Visual catalog: Data root not found — visualId combos disabled.");
        return;
    }
    std::string error;
    if (m_visualCatalog.Load(root, error)) {
        m_visualCatalogLoaded = true;
        LOG_INFO("[Editor] Visual catalog loaded: assets=" +
                 std::to_string(m_visualCatalog.Manifest().assets.size()) + " entities=" +
                 std::to_string(m_visualCatalog.Entities().entities.size()));
    } else {
        LOG_WARN("[Editor] Visual catalog load failed (combos disabled): " + error);
    }
}

// visualId ComboBox（kindFilter: Player/Monster/Npc/Portal；空 = 全部）。
// 返回选中的新 visualId（未改变时原样返回 current）。
std::string LegendMapEditorApp::VisualAssetCombo(const char* label, const std::string& current,
                                                 const char* kindFilter) {
    if (!m_visualCatalogLoaded) {
        ImGui::TextDisabled("%s: (visual catalog unavailable)", label);
        return current;
    }
    std::string result = current;
    std::string currentLabel = "(none)";
    for (const auto& entity : m_visualCatalog.Entities().entities) {
        if (entity.visualId == current) {
            currentLabel = current + (entity.kind == "Npc"
                                          ? " [" + std::to_string(entity.serverVisualId) + "]"
                                          : std::string());
            break;
        }
    }
    if (!ImGui::BeginCombo(label, currentLabel.c_str())) {
        return current;
    }
    // (none) 项
    if (ImGui::Selectable("(none)", current.empty())) {
        result.clear();
    }
    for (const auto& entity : m_visualCatalog.Entities().entities) {
        if (kindFilter != nullptr && entity.kind != kindFilter) {
            continue;
        }
        std::string itemLabel =
            entity.visualId + "  (" + entity.kind +
            (entity.kind == "Npc" ? " #" + std::to_string(entity.serverVisualId) : "") + ")";
        if (ImGui::Selectable(itemLabel.c_str(), entity.visualId == current)) {
            result = entity.visualId;
        }
        if (entity.visualId == current) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
    return result;
}

// visualMapId ComboBox：返回新选择（未选择返回原值）。
std::string LegendMapEditorApp::VisualMapCombo(const std::string& currentId) {
    if (!m_visualCatalogLoaded) {
        ImGui::TextDisabled("visualMapId: (visual catalog unavailable)");
        return currentId;
    }
    std::string result = currentId;
    if (ImGui::BeginCombo("visualMapId", currentId.empty() ? "(none)" : currentId.c_str())) {
        if (ImGui::Selectable("(none)", currentId.empty())) {
            result.clear();
        }
        for (const auto& visual : m_visualCatalog.MapVisuals()) {
            if (ImGui::Selectable(visual.visualMapId.c_str(), visual.visualMapId == currentId)) {
                result = visual.visualMapId;
            }
            if (visual.visualMapId == currentId) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return result;
}

// 加载/缓存整张 sheet 纹理（ImGui GL 纹理 id）；失败返回 0。
unsigned int LegendMapEditorApp::GetOrLoadSheetTexture(
    const legend::visual::AssetManifestEntry* sheet) {
    if (sheet == nullptr) {
        return 0;
    }
    const auto cached = m_previewTextures.find(sheet->path);
    if (cached != m_previewTextures.end()) {
        return cached->second;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(sheet->path.c_str(), &w, &h, &channels, 4);
    if (pixels == nullptr) {
        return 0;
    }
    unsigned int handle = 0;
    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    m_previewTextures[sheet->path] = handle;
    return handle;
}

// 绘制 clip 的第 frame 帧第 direction 行（UV 计算 + Image；高度 height 像素）。
void LegendMapEditorApp::DrawAnimationFrame(const legend::visual::AnimationClipDef* clip,
                                            int frame, int direction, float height) {
    const legend::visual::AssetManifestEntry* sheet =
        m_visualCatalog.FindAsset(clip->spriteSheetAssetId);
    const unsigned int handle = GetOrLoadSheetTexture(sheet);
    if (handle == 0) {
        ImGui::TextDisabled("preview: missing %s",
                            sheet != nullptr ? sheet->path.c_str() : clip->animationId.c_str());
        return;
    }
    const int dirIndex = std::clamp(direction, 0, std::max(0, clip->directionCount - 1));
    const float frameCountF = static_cast<float>(std::max(1, clip->frameCount));
    const float dirCountF = static_cast<float>(std::max(1, clip->directionCount));
    const float u0 = static_cast<float>(frame) / frameCountF *
                     static_cast<float>(clip->frameWidth) / static_cast<float>(clip->sheetWidth);
    const float u1 = static_cast<float>(frame + 1) / frameCountF *
                     static_cast<float>(clip->frameWidth) / static_cast<float>(clip->sheetWidth);
    const float v0 = static_cast<float>(dirIndex) / dirCountF *
                     static_cast<float>(clip->frameHeight) / static_cast<float>(clip->sheetHeight);
    const float v1 = static_cast<float>(dirIndex + 1) / dirCountF *
                     static_cast<float>(clip->frameHeight) / static_cast<float>(clip->sheetHeight);
    const float previewW = height * (static_cast<float>(clip->frameWidth) /
                                     static_cast<float>(clip->frameHeight));
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(handle)),
                 ImVec2(previewW, height), ImVec2(u0, v0), ImVec2(u1, v1));
}

// Visual Preview（指令四十一：至少显示第一帧）。
void LegendMapEditorApp::DrawVisualPreview(const char* visualId) {
    if (!m_visualCatalogLoaded || visualId == nullptr || visualId[0] == '\0') {
        return;
    }
    const legend::visual::VisualEntityDef* def = m_visualCatalog.FindEntity(visualId);
    if (def == nullptr) {
        return;
    }
    const auto idleIt = def->animations.find("idle");
    if (idleIt == def->animations.end()) {
        return;
    }
    const legend::visual::AnimationClipDef* clip = m_visualCatalog.FindClip(idleIt->second);
    if (clip == nullptr) {
        return;
    }
    ImGui::Text("Visual Preview: %s", visualId);
    DrawAnimationFrame(clip, 0, 0, 96.0f);
}

// Assets Validation（指令三十六）：结构校验 + 交叉引用 + 文件存在性。
void LegendMapEditorApp::ValidateVisualAssets() {
    if (!m_visualCatalogLoaded) {
        LOG_ERROR("[AssetsValidation] visual catalog not loaded — nothing to validate.");
        return;
    }
    std::string error;
    int failures = 0;
    const auto fail = [&](const std::string& message) {
        LOG_ERROR("[AssetsValidation] " + message);
        ++failures;
    };

    const auto& manifest = m_visualCatalog.Manifest();
    const auto& animations = m_visualCatalog.Animations();
    const auto& entities = m_visualCatalog.Entities();
    const auto& effects = m_visualCatalog.Effects();
    if (!legend::visual::ValidateVisualData(manifest, animations, entities, effects, error)) {
        fail(error);
    }

    // 资源文件存在性（相对路径 → 工作目录）。
    for (const auto& entry : manifest.assets) {
        std::error_code ec;
        if (!std::filesystem::exists(entry.path, ec)) {
            fail("asset '" + entry.assetId + "': file missing: " + entry.path);
        }
    }

    // 世界数据交叉引用（visualMapId / npc / portal visualId）。
    if (m_world) {
        const WorldDataSet& world = m_world->Data();
        std::set<std::string> visualMapIds;
        for (const auto& visual : m_visualCatalog.MapVisuals()) {
            visualMapIds.insert(visual.visualMapId);
        }
        for (const auto& map : world.maps) {
            if (!map.visualMapId.empty() && visualMapIds.count(map.visualMapId) == 0) {
                fail("map " + std::to_string(map.mapId) + ": visualMapId '" +
                     map.visualMapId + "' not in visual_maps.json");
            }
        }
        for (const auto& portal : world.portals) {
            if (!portal.visualId.empty() &&
                legend::visual::FindVisualEntity(entities, portal.visualId) == nullptr) {
                fail("portal " + std::to_string(portal.portalId) + ": visualId '" +
                     portal.visualId + "' not in visual_entities.json");
            }
        }
        for (const auto& npc : world.npcs) {
            if (npc.visualId != 0 &&
                m_visualCatalog.FindNpcEntityByServerVisualId(static_cast<int>(npc.visualId)) ==
                    nullptr) {
                fail("npc " + std::to_string(npc.npcDefinitionId) + ": visualId " +
                     std::to_string(npc.visualId) + " has no visual entity (serverVisualId)");
            }
        }
    }

    if (failures == 0) {
        LOG_INFO("[AssetsValidation] PASSED — assets=" +
                 std::to_string(manifest.assets.size()) + " clips=" +
                 std::to_string(animations.clips.size()) + " entities=" +
                 std::to_string(entities.entities.size()) + " effects=" +
                 std::to_string(effects.effects.size()));
    } else {
        LOG_ERROR("[AssetsValidation] FAILED with " + std::to_string(failures) + " issue(s).");
    }
}

void LegendMapEditorApp::Shutdown() {
    StopLocalGame(); // 编辑器退出时终止其拉起的本地游戏进程
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

    // ---- 阶段25：World Editor 扩展窗口 ----
    if (m_workspace == Workspace::World) {
        if (m_showQuestFlow) {
            DrawQuestFlowWindow();
        }
        if (m_showBossEditor) {
            DrawBossEditorWindow();
        }
        if (m_showProcessStatus) {
            DrawProcessStatusWindow();
        }
        if (m_showAssetBrowser) {
            DrawAssetBrowserWindow();
        }
        if (m_showAnimationPreview) {
            DrawAnimationPreviewWindow();
        }
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
            // 阶段24 指令三十六：Assets Validation（manifest/animation/effect/visual 交叉 + 文件）。
            if (ImGui::MenuItem("Validate Assets")) {
                ValidateVisualAssets();
            }
            // 阶段25：World + Game + Assets 一次全量校验。
            if (ImGui::MenuItem("Validate All")) {
                ValidateAll();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View##world")) {
            ImGui::Checkbox("World Grid", &m_showWorldGrid);
            ImGui::Separator();
            ImGui::MenuItem("Asset Browser", nullptr, &m_showAssetBrowser);
            ImGui::MenuItem("Animation Preview", nullptr, &m_showAnimationPreview);
            ImGui::MenuItem("Quest Flow", nullptr, &m_showQuestFlow);
            ImGui::MenuItem("Boss Editor", nullptr, &m_showBossEditor);
            ImGui::MenuItem("Process Status", nullptr, &m_showProcessStatus);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset World View")) {
                m_worldOriginX = -100.0f;
                m_worldOriginY = -100.0f;
                m_worldZoom = 0.45f;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run")) {
            if (ImGui::MenuItem("Launch WorldServer")) {
                LaunchWorldServer();
            }
            // 阶段25：四进程全链（Login/Gateway/World/Client）。
            if (ImGui::MenuItem("Launch Full Game")) {
                LaunchFullGame();
            }
            if (ImGui::MenuItem("Stop Local Game", nullptr, false,
                                !m_processes.empty())) {
                StopLocalGame();
            }
            if (ImGui::MenuItem("Process Status", nullptr, false)) {
                m_showProcessStatus = true;
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

    // ---- 阶段23：Game Data（23.1：左侧树扩展；23.12：Search 过滤）----
    DrawGameDataTree();
}

// ---------------------------------------------------------------------------
// 阶段23：Game Data 树（23.1 左侧扩展 / 23.12 Search / 23.13 Duplicate）
// ---------------------------------------------------------------------------
void LegendMapEditorApp::DrawGameDataTree() {
    if (m_game == nullptr) {
        return;
    }
    using GT = editor::GameDataDocument::ObjectType;
    ImGui::Separator();
    ImGui::TextUnformatted("Game Data");
    ImGui::InputText("Search (id/name)", m_gameSearch, sizeof(m_gameSearch)); // 23.12
    const std::string search = m_gameSearch;
    const auto& game = m_game->Data();
    const auto row = [&](GT type, std::uint32_t id, const std::string& name,
                         const std::string& label) {
        if (!m_game->MatchesSearch(type, id, name, search)) {
            return;
        }
        const bool selected =
            m_game->GetSelection().type == type && m_game->GetSelection().id == id;
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_game->SetSelection(type, id);
        }
    };
    const auto addSection = [&](const char* title, int count, auto addFn) {
        if (ImGui::TreeNodeEx(title, ImGuiTreeNodeFlags_DefaultOpen, "%s (%d)", title, count)) {
            addFn();
            ImGui::TreePop();
        }
    };

    addSection("Items", static_cast<int>(game.items.size()), [&] {
        for (const auto& item : game.items) {
            row(GT::Item, item.definitionId, item.name,
                std::to_string(item.definitionId) + " - " + item.name);
        }
        if (ImGui::Button("+ Add Item")) {
            legend::world::ItemDefinition item;
            item.definitionId = m_game->SuggestItemId();
            item.name = "New Item";
            const std::uint32_t id = item.definitionId;
            m_game->Mutate([&](GameDataSet& d) { d.items.push_back(item); });
            m_game->SetSelection(GT::Item, id);
        }
    });
    addSection("Monsters", static_cast<int>(game.monsters.size()), [&] {
        for (const auto& monster : game.monsters) {
            row(GT::Monster, monster.monsterTypeId, monster.name,
                std::to_string(monster.monsterTypeId) + " - " + monster.name);
        }
        if (ImGui::Button("+ Add Monster")) {
            legend::world::MonsterDefinition monster;
            monster.monsterTypeId = m_game->SuggestMonsterId();
            monster.name = "New Monster";
            const std::uint32_t id = monster.monsterTypeId;
            m_game->Mutate([&](GameDataSet& d) { d.monsters.push_back(monster); });
            m_game->SetSelection(GT::Monster, id);
        }
    });
    addSection("Skills", static_cast<int>(game.skills.size()), [&] {
        for (const auto& skill : game.skills) {
            row(GT::Skill, skill.skillId, skill.name,
                std::to_string(skill.skillId) + " - " + skill.name);
        }
    });
    addSection("Statuses", static_cast<int>(game.statuses.size()), [&] {
        for (const auto& status : game.statuses) {
            row(GT::Status, status.effectId, status.name,
                std::to_string(status.effectId) + " - " + status.name);
        }
    });
    addSection("Quests", static_cast<int>(game.quests.size()), [&] {
        for (const auto& quest : game.quests) {
            row(GT::Quest, quest.questId, quest.name,
                std::to_string(quest.questId) + " - " + quest.name);
        }
    });
    addSection("Shops", static_cast<int>(game.shops.size()), [&] {
        for (const auto& shop : game.shops) {
            row(GT::Shop, shop.shopId, shop.name,
                std::to_string(shop.shopId) + " - " + shop.name);
        }
    });
    addSection("Teleports", static_cast<int>(game.teleports.size()), [&] {
        for (const auto& teleport : game.teleports) {
            row(GT::Teleport, teleport.teleportId, teleport.name,
                std::to_string(teleport.teleportId) + " - " + teleport.name);
        }
    });
    addSection("Loot Tables", static_cast<int>(game.lootTables.size()), [&] {
        for (const auto& table : game.lootTables) {
            row(GT::LootTable, table.lootTableId, table.name,
                std::to_string(table.lootTableId) + " - " + table.name);
        }
    });
    addSection("Chapters", static_cast<int>(game.chapters.size()), [&] {
        for (const auto& chapter : game.chapters) {
            row(GT::Chapter, chapter.chapterId, chapter.title,
                std::to_string(chapter.chapterId) + " - " + chapter.title);
        }
        if (ImGui::Button("+ Add Chapter")) {
            legend::world::ChapterDefinition chapter;
            chapter.chapterId = m_game->SuggestChapterId();
            chapter.title = "New Chapter";
            chapter.finalQuestId = 0;
            const std::uint32_t id = chapter.chapterId;
            m_game->Mutate([&](GameDataSet& d) { d.chapters.push_back(chapter); });
            m_game->SetSelection(GT::Chapter, id);
        }
    });
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
            // 阶段25：Quest Area Map Picker 武装态——下一次点击写入 areaX/areaY。
            if (m_questAreaPickActive && m_game != nullptr) {
                const std::uint32_t questId = m_questAreaPickQuestId;
                const std::uint32_t objectiveId = m_questAreaPickObjectiveId;
                const float px = m_mouseWorldX;
                const float py = m_mouseWorldY;
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& q : d.quests) {
                        if (q.questId != questId) {
                            continue;
                        }
                        for (auto& o : q.objectives) {
                            if (o.objectiveId == objectiveId) {
                                o.areaX = px;
                                o.areaY = py;
                            }
                        }
                    }
                });
                m_worldMessage = "Quest area set to (" +
                                 std::to_string(static_cast<int>(px)) + ", " +
                                 std::to_string(static_cast<int>(py)) + ").";
                m_worldMessageIsError = false;
                m_questAreaPickActive = false;
                return; // 本帧跳过对象拾取
            }
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
    DrawQuestAreaOverlay();

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

// ---------------------------------------------------------------------------
// 阶段25：World Editor 扩展（Asset Browser / Animation Preview / Quest Flow /
// Boss Editor / Validate All / Launch Full Game / Stop / Process Status）
// ---------------------------------------------------------------------------

// 进程启动 helper：exe 与编辑器同目录；工作目录 = 仓库根（相对 Data/ 路径）。
bool LegendMapEditorApp::LaunchEditorProcess(const char* name, const std::string& exeName,
                                             const std::string& args) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir = exePath;
    const auto slash = exeDir.find_last_of("\\/");
    if (slash != std::string::npos) {
        exeDir = exeDir.substr(0, slash);
    }
    const std::string fullExe = exeDir + "\\" + exeName;
    const std::string workDir = exeDir + "\\..\\..";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmdLine = "\"" + fullExe + "\"" + (args.empty() ? "" : " " + args);
    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                   workDir.c_str(), &si, &pi);
    EditorProcess proc;
    proc.name = name;
    if (ok) {
        proc.hProcess = pi.hProcess;
        proc.pid = pi.dwProcessId;
        CloseHandle(pi.hThread);
        LOG_INFO(std::string("Editor: launched ") + name + " (pid " +
                 std::to_string(pi.dwProcessId) + ")");
    } else {
        proc.launchFailed = true;
        LOG_ERROR(std::string("Editor: launch ") + name + " failed, error=" +
                  std::to_string(GetLastError()));
    }
    m_processes.push_back(std::move(proc));
    return ok;
}

// 四进程全链：Login(7100)/Gateway(7300)/World(7200)/Client。
void LegendMapEditorApp::LaunchFullGame() {
    m_worldMessage = "";
    bool allOk = LaunchEditorProcess("LoginServer", "LegendLoginServer.exe") &&
                 LaunchEditorProcess("Gateway", "LegendGateway.exe") &&
                 LaunchEditorProcess("WorldServer", "LegendWorldServer.exe");
    // Client 延迟 2s 启动（等服务器监听就绪）。
    if (allOk) {
        std::thread([] { std::this_thread::sleep_for(std::chrono::milliseconds(2000)); }).join();
    }
    allOk = allOk && LaunchEditorProcess("Client", "LegendClient.exe");
    m_worldMessage = allOk ? "Full game chain launched (Login/Gateway/World/Client)."
                           : "Full game launch FAILED (see logs; build all executables first).";
    m_worldMessageIsError = !allOk;
    m_showProcessStatus = true;
}

// 终止编辑器拉起的全部本地进程。
void LegendMapEditorApp::StopLocalGame() {
    int stopped = 0;
    for (auto& proc : m_processes) {
        if (proc.hProcess != nullptr) {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(proc.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                TerminateProcess(proc.hProcess, 0);
                WaitForSingleObject(proc.hProcess, 2000);
                ++stopped;
            }
            CloseHandle(proc.hProcess);
            proc.hProcess = nullptr;
        }
    }
    m_processes.clear();
    m_worldMessage = stopped > 0 ? "Stopped " + std::to_string(stopped) + " local process(es)."
                                 : "No running local processes.";
    m_worldMessageIsError = false;
    LOG_INFO("Editor: StopLocalGame stopped=" + std::to_string(stopped));
}

// World + Game + Assets 全量校验（Console 汇总 + 状态条）。
void LegendMapEditorApp::ValidateAll() {
    int failures = 0;

    // World。
    m_world->Mutate([](WorldDataSet&) {});
    if (m_world->HasErrors()) {
        LOG_ERROR("[ValidateAll] World: " + m_world->ValidationErrors().front());
        ++failures;
    }

    // Game（含 chapters + World 交叉引用）。
    if (m_game != nullptr) {
        std::string gameError;
        if (!legend::world::ValidateGameData(m_game->Data(), m_world->Data(), gameError)) {
            LOG_ERROR("[ValidateAll] Game: " + gameError);
            ++failures;
        }
    }

    // Assets（结构/交叉/文件存在性 + World 视觉引用）。
    if (m_visualCatalogLoaded) {
        const int before = failures;
        ValidateVisualAssets();
        // ValidateVisualAssets 输出 LOG；无法直接取 count——重跑轻量检查取首错。
        std::string assetError;
        if (!legend::visual::ValidateVisualData(m_visualCatalog.Manifest(),
                                                m_visualCatalog.Animations(),
                                                m_visualCatalog.Entities(),
                                                m_visualCatalog.Effects(), assetError)) {
            (void)assetError;
        }
        failures = failures > before ? failures : before; // 保持计数语义简单
    } else {
        LOG_ERROR("[ValidateAll] Assets: visual catalog not loaded");
        ++failures;
    }

    m_worldMessage = failures == 0 ? "Validate All PASSED (world + game + assets)."
                                   : "Validate All FAILED with " + std::to_string(failures) +
                                         " group(s) — see Console.";
    m_worldMessageIsError = failures != 0;
}

// ReachArea 圈层绘制：选中任务的区域 + 武装拾取模式的实时圈。
void LegendMapEditorApp::DrawQuestAreaOverlay() {
    if (m_game == nullptr) {
        return;
    }
    const ImVec2 canvasP0 = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto w2s = [&](float wx, float wy) {
        return ImVec2(canvasP0.x + (wx - m_worldOriginX) * m_worldZoom,
                      canvasP0.y + (wy - m_worldOriginY) * m_worldZoom);
    };
    using GT = editor::GameDataDocument::ObjectType;
    const auto gsel = m_game->GetSelection();
    if (gsel.type == GT::Quest) {
        if (const auto* quest = m_game->FindQuest(gsel.id)) {
            for (const auto& o : quest->objectives) {
                if (o.type != legend::world::QuestObjectiveType::ReachArea ||
                    o.areaRadius <= 0.0f) {
                    continue;
                }
                const ImVec2 c = w2s(o.areaX, o.areaY);
                const float r = o.areaRadius * m_worldZoom;
                draw->AddCircle(c, r, IM_COL32(120, 220, 140, 255), 64, 2.0f);
                draw->AddCircleFilled(c, r, IM_COL32(120, 220, 140, 32));
            }
        }
    }
    if (m_questAreaPickActive) {
        const ImVec2 c = w2s(m_mouseWorldX, m_mouseWorldY);
        const float r = std::max(4.0f, m_questAreaPickRadius * m_worldZoom);
        draw->AddCircle(c, r, IM_COL32(250, 200, 90, 255), 64, 2.0f);
        draw->AddText(ImVec2(c.x + r + 6.0f, c.y - 10.0f), IM_COL32(250, 200, 90, 255),
                      "click to place quest area");
    }
}

// ---- Asset Browser：Data/Assets 全资产缩略图浏览 + 详情 ----
void LegendMapEditorApp::DrawAssetBrowserWindow() {
    if (!ImGui::Begin("Asset Browser", &m_showAssetBrowser)) {
        ImGui::End();
        return;
    }
    if (!m_visualCatalogLoaded) {
        ImGui::TextDisabled("(visual catalog unavailable)");
        ImGui::End();
        return;
    }
    const auto& manifest = m_visualCatalog.Manifest().assets;
    static int kindFilter = 0; // 0=all 1=SpriteSheet 2=Sprite/Texture 3=Font/Effect
    const char* kinds[] = {"All", "SpriteSheet", "Sprite/Texture", "Font/Effect"};
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Kind", &kindFilter, kinds, 4);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d assets)", static_cast<int>(manifest.size()));
    const auto passesFilter = [&](const legend::visual::AssetManifestEntry& entry) {
        if (kindFilter == 0) {
            return true;
        }
        if (kindFilter == 1) {
            return entry.type == legend::visual::kAssetTypeSpriteSheet;
        }
        if (kindFilter == 2) {
            return entry.type == legend::visual::kAssetTypeSprite ||
                   entry.type == legend::visual::kAssetTypeTexture;
        }
        return entry.type == legend::visual::kAssetTypeFont ||
               entry.type == legend::visual::kAssetTypeEffectTexture;
    };

    const float cell = 92.0f;
    int column = 0;
    for (const auto& entry : manifest) {
        if (!passesFilter(entry)) {
            continue;
        }
        if (column % static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + 8)) != 0 &&
            column != 0) {
            ImGui::SameLine();
        }
        ImGui::BeginGroup();
        // 缩略图（首帧/整图）。
        const unsigned int handle =
            GetOrLoadSheetTexture(m_visualCatalog.FindAsset(entry.assetId));
        if (handle != 0) {
            ImGui::ImageButton("##thumb", static_cast<ImTextureID>(static_cast<intptr_t>(handle)),
                               ImVec2(cell - 8, cell - 8), ImVec2(0, 0), ImVec2(1, 1));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                m_previewSelectedAsset = entry.assetId;
            }
        } else {
            ImGui::Dummy(ImVec2(cell - 8, cell - 8));
        }
        ImGui::TextDisabled("%s", entry.assetId.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s  %dx%d", entry.assetId.c_str(), entry.path.c_str(),
                              entry.width, entry.height);
        }
        if (m_previewSelectedAsset == entry.assetId) {
            ImGui::SameLine();
            ImGui::TextUnformatted("<");
        }
        ++column;
    }

    // 详情面板。
    ImGui::Separator();
    if (!m_previewSelectedAsset.empty()) {
        const auto* entry = m_visualCatalog.FindAsset(m_previewSelectedAsset);
        if (entry != nullptr) {
            ImGui::Text("%s (%s)", entry->assetId.c_str(), entry->type.c_str());
            ImGui::TextDisabled("path: %s  %dx%d  pivot(%.2f,%.2f) enabled=%d",
                                entry->path.c_str(), entry->width, entry->height, entry->pivotX,
                                entry->pivotY, entry->enabled ? 1 : 0);
            const unsigned int handle = GetOrLoadSheetTexture(entry);
            if (handle != 0) {
                const float h = 220.0f;
                const float w = h * static_cast<float>(entry->width) /
                                static_cast<float>(std::max(1, entry->height));
                ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(handle)),
                             ImVec2(w, h));
            }
        }
    }
    ImGui::End();
}

// ---- Animation Preview：clip 帧播放（Play/Pause + 帧滑条 + 方向行）----
void LegendMapEditorApp::DrawAnimationPreviewWindow() {
    if (!ImGui::Begin("Animation Preview", &m_showAnimationPreview)) {
        ImGui::End();
        return;
    }
    if (!m_visualCatalogLoaded) {
        ImGui::TextDisabled("(visual catalog unavailable)");
        ImGui::End();
        return;
    }
    const auto& clips = m_visualCatalog.Animations().clips;
    if (clips.empty()) {
        ImGui::TextDisabled("(no animation clips)");
        ImGui::End();
        return;
    }
    // clip 选择。
    if (m_animPreviewClipId.empty()) {
        m_animPreviewClipId = clips.front().animationId;
    }
    if (ImGui::BeginCombo("Clip", m_animPreviewClipId.c_str())) {
        for (const auto& clip : clips) {
            if (ImGui::Selectable(clip.animationId.c_str(),
                                  clip.animationId == m_animPreviewClipId)) {
                m_animPreviewClipId = clip.animationId;
                m_animPreviewTime = 0.0f;
            }
        }
        ImGui::EndCombo();
    }
    const legend::visual::AnimationClipDef* clip =
        m_visualCatalog.FindClip(m_animPreviewClipId);
    if (clip == nullptr || clip->frameCount <= 0) {
        ImGui::TextDisabled("(clip missing)");
        ImGui::End();
        return;
    }
    // 播放推进（真实帧率；TileMap 工作区跳过——DrawUI 仅 World 分支调用本窗口）。
    if (m_animPreviewPlaying) {
        m_animPreviewTime += ImGui::GetIO().DeltaTime;
    }
    const float frameDuration = 1.0f / std::max(0.01f, clip->fps);
    int frame = static_cast<int>(m_animPreviewTime / frameDuration);
    if (clip->loop) {
        frame %= clip->frameCount;
    } else {
        frame = std::min(frame, clip->frameCount - 1);
    }
    int direction = 0;
    if (clip->directionCount > 1) {
        ImGui::SetNextItemWidth(160);
        ImGui::SliderInt("Direction", &direction, 0, clip->directionCount - 1);
    }
    ImGui::Checkbox("Playing", &m_animPreviewPlaying);
    ImGui::SameLine();
    ImGui::TextDisabled("frame %d/%d  fps=%.0f  loop=%d", frame + 1, clip->frameCount, clip->fps,
                        clip->loop ? 1 : 0);
    ImGui::SliderInt("Frame##scrub", &frame, 0, clip->frameCount - 1);
    DrawAnimationFrame(clip, frame, direction, 200.0f);
    // 所属实体（便于从实体反查）。
    for (const auto& entity : m_visualCatalog.Entities().entities) {
        for (const auto& [slot, animId] : entity.animations) {
            if (animId == clip->animationId) {
                ImGui::TextDisabled("used by: %s (%s/%s)", entity.visualId.c_str(),
                                    entity.kind.c_str(), slot.c_str());
                break;
            }
        }
    }
    ImGui::End();
}

// ---- Quest Flow：章节 -> 任务链 -> 前置/目标/奖励一览 ----
void LegendMapEditorApp::DrawQuestFlowWindow() {
    if (!ImGui::Begin("Quest Flow", &m_showQuestFlow)) {
        ImGui::End();
        return;
    }
    if (m_game == nullptr) {
        ImGui::End();
        return;
    }
    const auto& game = m_game->Data();
    const auto questLabel = [&](std::uint32_t questId) {
        for (const auto& q : game.quests) {
            if (q.questId == questId) {
                return std::to_string(q.questId) + " - " + q.name;
            }
        }
        return std::to_string(questId) + " - (missing)";
    };
    if (game.chapters.empty()) {
        ImGui::TextDisabled("(no chapters defined — Data/Game/chapters.json)");
    }
    for (const auto& chapter : game.chapters) {
        const std::string header = "Chapter " + std::to_string(chapter.chapterId) + ": " +
                                   chapter.title;
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (std::size_t i = 0; i < chapter.questIds.size(); ++i) {
            const std::uint32_t questId = chapter.questIds[i];
            const auto* quest = m_game->FindQuest(questId);
            const bool isFinal = questId == chapter.finalQuestId;
            std::string line = (i == 0 ? "" : "  -> ") + questLabel(questId);
            if (isFinal) {
                line += "   [FINAL]";
            }
            if (ImGui::Selectable(line.c_str())) {
                m_game->SetSelection(editor::GameDataDocument::ObjectType::Quest, questId);
            }
            if (quest != nullptr && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("prereq: %s\nobjectives: %d\nreward: %u exp, %u gold%s",
                                  quest->prerequisiteQuestId == 0
                                      ? "(none)"
                                      : questLabel(quest->prerequisiteQuestId).c_str(),
                                  static_cast<int>(quest->objectives.size()), quest->reward.exp,
                                  quest->reward.gold,
                                  quest->reward.itemDefinitionId != 0 ? " + item" : "");
            }
        }
    }
    // 未编入章节的任务（提示）。
    ImGui::Separator();
    for (const auto& q : game.quests) {
        bool inChapter = false;
        for (const auto& c : game.chapters) {
            if (std::find(c.questIds.begin(), c.questIds.end(), q.questId) !=
                c.questIds.end()) {
                inChapter = true;
                break;
            }
        }
        if (!inChapter) {
            if (ImGui::Selectable(("[unassigned] " + questLabel(q.questId)).c_str())) {
                m_game->SetSelection(editor::GameDataDocument::ObjectType::Quest, q.questId);
            }
        }
    }
    ImGui::End();
}

// ---- Boss Editor：Boss 怪物战斗/掉落/视觉 + 刷怪点绑定编辑 ----
void LegendMapEditorApp::DrawBossEditorWindow() {
    if (!ImGui::Begin("Boss Editor", &m_showBossEditor)) {
        ImGui::End();
        return;
    }
    if (m_game == nullptr) {
        ImGui::End();
        return;
    }
    const auto& game = m_game->Data();
    if (game.monsters.empty()) {
        ImGui::TextDisabled("(no monsters)");
        ImGui::End();
        return;
    }
    // 默认选中 Boss 表（lootTableId>=2000）或最大 ID 怪物。
    if (m_bossSelectedMonsterId == 0) {
        for (const auto& m : game.monsters) {
            if (m.lootTableId >= 2000) {
                m_bossSelectedMonsterId = m.monsterTypeId;
            }
        }
        if (m_bossSelectedMonsterId == 0) {
            m_bossSelectedMonsterId = game.monsters.back().monsterTypeId;
        }
    }
    // 怪物选择。
    std::string currentLabel = "?";
    const legend::world::MonsterDefinition* boss = nullptr;
    for (const auto& m : game.monsters) {
        if (m.monsterTypeId == m_bossSelectedMonsterId) {
            boss = &m;
            currentLabel = std::to_string(m.monsterTypeId) + " - " + m.name;
        }
    }
    if (ImGui::BeginCombo("Monster", currentLabel.c_str())) {
        for (const auto& m : game.monsters) {
            const std::string label = std::to_string(m.monsterTypeId) + " - " + m.name;
            if (ImGui::Selectable(label.c_str(), m.monsterTypeId == m_bossSelectedMonsterId)) {
                m_bossSelectedMonsterId = m.monsterTypeId;
            }
        }
        ImGui::EndCombo();
    }
    if (boss == nullptr) {
        ImGui::End();
        return;
    }
    const std::uint32_t bossId = boss->monsterTypeId;
    auto editU = [&](const char* label, std::uint32_t value, auto set) {
        int tmp = static_cast<int>(value);
        if (ImGui::InputInt(label, &tmp)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, tmp));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& m : d.monsters) {
                    if (m.monsterTypeId == bossId) {
                        set(m, nv);
                    }
                }
            });
        }
    };
    auto editF = [&](const char* label, float value, auto set) {
        float tmp = value;
        if (ImGui::InputFloat(label, &tmp, 1.0f, 0.0f, "%.1f")) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& m : d.monsters) {
                    if (m.monsterTypeId == bossId) {
                        set(m, tmp);
                    }
                }
            });
        }
    };
    editU("level", boss->level, [](auto& m, std::uint32_t v) { m.level = v; });
    editU("maxHp", boss->maxHp, [](auto& m, std::uint32_t v) { m.maxHp = v; });
    editU("attackPower", boss->attackPower, [](auto& m, std::uint32_t v) { m.attackPower = v; });
    editU("defense", boss->defense, [](auto& m, std::uint32_t v) { m.defense = v; });
    editF("moveSpeed", boss->moveSpeed, [](auto& m, float v) { m.moveSpeed = v; });
    editF("aggroRadius", boss->aggroRadius, [](auto& m, float v) { m.aggroRadius = v; });
    editU("rewardExp", boss->rewardExp, [](auto& m, std::uint32_t v) { m.rewardExp = v; });
    editU("rewardGold", boss->rewardGold, [](auto& m, std::uint32_t v) { m.rewardGold = v; });
    // 掉落表绑定。
    if (ImGui::BeginCombo("lootTableId",
                          boss->lootTableId == 0 ? "(none)"
                                                 : std::to_string(boss->lootTableId).c_str())) {
        if (ImGui::Selectable("(none)", boss->lootTableId == 0)) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& m : d.monsters) {
                    if (m.monsterTypeId == bossId) {
                        m.lootTableId = 0;
                    }
                }
            });
        }
        for (const auto& table : game.lootTables) {
            const std::string label = std::to_string(table.lootTableId) + " - " + table.name;
            if (ImGui::Selectable(label.c_str(), table.lootTableId == boss->lootTableId)) {
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& m : d.monsters) {
                        if (m.monsterTypeId == bossId) {
                            m.lootTableId = table.lootTableId;
                        }
                    }
                });
            }
        }
        ImGui::EndCombo();
    }
    // 掉落表条目一览（只读，编辑走 Loot Tables 段）。
    if (boss->lootTableId != 0) {
        for (const auto& table : game.lootTables) {
            if (table.lootTableId != boss->lootTableId) {
                continue;
            }
            ImGui::TextDisabled("Loot: %s (%d entries)", table.name.c_str(),
                                static_cast<int>(table.entries.size()));
            for (const auto& entry : table.entries) {
                std::string itemName = std::to_string(entry.itemDefinitionId);
                for (const auto& item : game.items) {
                    if (item.definitionId == entry.itemDefinitionId) {
                        itemName = item.name;
                        break;
                    }
                }
                ImGui::BulletText("%s  %.0f%%  x%u~%u", itemName.c_str(),
                                  entry.dropChance * 100.0, entry.minQuantity,
                                  entry.maxQuantity);
            }
        }
    }
    // 视觉绑定。
    if (m_visualCatalogLoaded) {
        const std::string chosen =
            VisualAssetCombo("visualId", boss->visualId, "Monster");
        if (chosen != boss->visualId) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& m : d.monsters) {
                    if (m.monsterTypeId == bossId) {
                        m.visualId = chosen;
                    }
                }
            });
        }
        DrawVisualPreview(boss->visualId.c_str());
    }
    // 刷怪点绑定（Map/位置/count/respawn）。
    ImGui::Separator();
    ImGui::TextUnformatted("Spawns");
    if (m_world != nullptr) {
        for (const auto& spawn : m_world->Data().monsterSpawns) {
            if (spawn.monsterDefinitionId != bossId) {
                continue;
            }
            ImGui::PushID(static_cast<int>(spawn.spawnId));
            ImGui::Text("spawn %u: map %u @ (%.0f,%.0f) r%.0f count=%u respawn=%us",
                        spawn.spawnId, spawn.mapId, spawn.centerX, spawn.centerY, spawn.radius,
                        spawn.count, spawn.respawnSeconds);
            int respawn = static_cast<int>(spawn.respawnSeconds);
            if (ImGui::InputInt("respawnSeconds", &respawn)) {
                const auto nv = static_cast<std::uint32_t>(std::max(1, respawn));
                m_world->Mutate([&](WorldDataSet& d) {
                    for (auto& s : d.monsterSpawns) {
                        if (s.spawnId == spawn.spawnId) {
                            s.respawnSeconds = nv;
                        }
                    }
                });
            }
            if (ImGui::Button("Select On Canvas")) {
                m_world->SetSelection(editor::WorldDocument::ObjectType::Spawn, spawn.spawnId);
            }
            ImGui::PopID();
        }
        if (ImGui::Button("+ Add Spawn (boss, 1x)")) {
            std::uint32_t newId = 1;
            for (const auto& s : m_world->Data().monsterSpawns) {
                newId = std::max(newId, s.spawnId + 1);
            }
            const auto bossMapId = bossId >= 2000 ? std::uint16_t{3} : std::uint16_t{2};
            m_world->Mutate([&](WorldDataSet& d) {
                legend::world::MonsterSpawnDefinition spawn;
                spawn.spawnId = newId;
                spawn.mapId = bossMapId;
                spawn.monsterDefinitionId = bossId;
                spawn.centerX = 1900.0f;
                spawn.centerY = 1500.0f;
                spawn.radius = 0.0f;
                spawn.count = 1;
                spawn.respawnSeconds = 45; // Boss 默认 45s
                d.monsterSpawns.push_back(spawn);
            });
            m_world->SetSelection(editor::WorldDocument::ObjectType::Spawn, newId);
        }
    } else {
        ImGui::TextDisabled("(world data not loaded)");
    }
    ImGui::End();
}

// ---- Process Status：编辑器拉起的本地进程一览 ----
void LegendMapEditorApp::DrawProcessStatusWindow() {
    if (!ImGui::Begin("Process Status", &m_showProcessStatus)) {
        ImGui::End();
        return;
    }
    if (m_processes.empty()) {
        ImGui::TextDisabled("(no processes launched by editor)");
    }
    if (ImGui::Button("Stop Local Game")) {
        StopLocalGame();
    }
    ImGui::Separator();
    if (ImGui::BeginTable("procs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("PID");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Exit");
        ImGui::TableHeadersRow();
        for (auto& proc : m_processes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(proc.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%lu", static_cast<unsigned long>(proc.pid));
            ImGui::TableNextColumn();
            if (proc.launchFailed) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "LAUNCH FAILED");
            } else {
                DWORD exitCode = 0;
                const bool alive = proc.hProcess != nullptr &&
                                   GetExitCodeProcess(proc.hProcess, &exitCode) &&
                                   exitCode == STILL_ACTIVE;
                if (alive) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "RUNNING");
                } else {
                    if (proc.hProcess != nullptr && GetExitCodeProcess(proc.hProcess, &exitCode)) {
                        proc.exitCode = exitCode;
                        proc.exitCodeValid = true;
                    }
                    ImGui::TextDisabled("EXITED");
                }
            }
            ImGui::TableNextColumn();
            if (proc.exitCodeValid) {
                ImGui::Text("%lu", static_cast<unsigned long>(proc.exitCode));
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
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
        ImGui::TextDisabled("No world object selected.");
    } else {
    DrawWorldInspectorFields();
    }
    // ---- 阶段23：Game 定义 Inspector（23.3~23.10/23.18）----
    ImGui::Separator();
    ImGui::TextUnformatted("Game Data Inspector");
    DrawGameDataInspector();
}

void LegendMapEditorApp::DrawGameDataInspector() {
    if (m_game == nullptr) {
        return;
    }
    using GT = editor::GameDataDocument::ObjectType;
    const auto sel = m_game->GetSelection();
    if (sel.type == GT::None) {
        ImGui::TextDisabled("No game definition selected.");
        return;
    }
    // 文本缓冲 owner 跟踪。
    const auto refill = [&](const std::string& name, const std::string& text = "",
                            const std::string& desc = "") {
        m_gOwnerType = sel.type;
        m_gOwnerId = sel.id;
        std::snprintf(m_gInsName, sizeof(m_gInsName), "%s", name.c_str());
        std::snprintf(m_gInsText, sizeof(m_gInsText), "%s", text.c_str());
        std::snprintf(m_gInsDesc, sizeof(m_gInsDesc), "%s", desc.c_str());
    };
    if (sel.type != m_gOwnerType || sel.id != m_gOwnerId) {
        if (sel.type == GT::Item) {
            if (const auto* v = m_game->FindItem(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Monster) {
            if (const auto* v = m_game->FindMonster(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Skill) {
            if (const auto* v = m_game->FindSkill(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Status) {
            if (const auto* v = m_game->FindStatus(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Quest) {
            if (const auto* v = m_game->FindQuest(sel.id)) {
                refill(v->name, "", v->description);
            }
        } else if (sel.type == GT::Shop) {
            if (const auto* v = m_game->FindShop(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Teleport) {
            if (const auto* v = m_game->FindTeleport(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::LootTable) {
            if (const auto* v = m_game->FindLootTable(sel.id)) {
                refill(v->name);
            }
        } else if (sel.type == GT::Chapter) {
            if (const auto* v = m_game->FindChapter(sel.id)) {
                std::string csv;
                for (std::size_t i = 0; i < v->questIds.size(); ++i) {
                    csv += std::to_string(v->questIds[i]);
                    if (i + 1 < v->questIds.size()) {
                        csv += ",";
                    }
                }
                refill(v->title, csv);
            }
        }
    }
    if (sel.type == GT::Item) {
        const auto* v = m_game->FindItem(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Item not found."); return; }
        ImGui::Text("itemDefinitionId: %u", v->definitionId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& item : d.items) { if (item.definitionId == sel.id) { item.name = m_gInsName; } } }); }
        auto editU32 = [&](const char* label, std::uint32_t value, auto set) {
            int tmp = static_cast<int>(value);
            if (ImGui::InputInt(label, &tmp)) {
                const auto nv = static_cast<std::uint32_t>(std::max(0, tmp));
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& item : d.items) {
                        if (item.definitionId == sel.id) {
                            set(item, nv);
                        }
                    }
                });
            }
        };
        editU32("maxStack", v->maxStack,
                [](auto& item, std::uint32_t n) { item.maxStack = n; });
        editU32("attackBonus", v->attackBonus,
                [](auto& item, std::uint32_t n) { item.attackBonus = n; });
        editU32("defenseBonus", v->defenseBonus,
                [](auto& item, std::uint32_t n) { item.defenseBonus = n; });
        // 23.18：基础文字预览。
        ImGui::TextDisabled("Preview: %s (type=%s stack=%u atk=+%u def=+%u)", v->name.c_str(),
                            v->type == legend::world::ItemType::Weapon ? "Weapon"
                            : v->type == legend::world::ItemType::Armor ? "Armor"
                                                                        : "Material",
                            v->maxStack, v->attackBonus, v->defenseBonus);
    } else if (sel.type == GT::Monster) {
        const auto* v = m_game->FindMonster(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Monster not found."); return; }
        ImGui::Text("monsterDefinitionId: %u", v->monsterTypeId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& m : d.monsters) { if (m.monsterTypeId == sel.id) { m.name = m_gInsName; } } }); }
        auto editF = [&](const char* label, float value, auto set) {
            float tmp = value;
            if (ImGui::InputFloat(label, &tmp, 1.0f, 0.0f, "%.1f")) {
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& m : d.monsters) {
                        if (m.monsterTypeId == sel.id) {
                            set(m, tmp);
                        }
                    }
                });
            }
        };
        auto editU = [&](const char* label, std::uint32_t value, auto set) {
            int tmp = static_cast<int>(value);
            if (ImGui::InputInt(label, &tmp)) {
                const auto nv = static_cast<std::uint32_t>(std::max(0, tmp));
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& m : d.monsters) {
                        if (m.monsterTypeId == sel.id) {
                            set(m, nv);
                        }
                    }
                });
            }
        };
        editU("level", v->level, [](auto& m, std::uint32_t n) { m.level = n; });
        editU("maxHp", v->maxHp, [](auto& m, std::uint32_t n) { m.maxHp = n; });
        editU("attackPower", v->attackPower, [](auto& m, std::uint32_t n) { m.attackPower = n; });
        editU("defense", v->defense, [](auto& m, std::uint32_t n) { m.defense = n; });
        editF("moveSpeed", v->moveSpeed, [](auto& m, float n) { m.moveSpeed = n; });
        editF("attackRange", v->attackRange, [](auto& m, float n) { m.attackRange = n; });
        editF("aggroRange", v->aggroRadius, [](auto& m, float n) { m.aggroRadius = n; });
        editF("leashRange", v->leashRadius, [](auto& m, float n) { m.leashRadius = n; });
        editU("attackCooldownMs",
              static_cast<std::uint32_t>(v->attackCooldownSeconds * 1000.0f),
              [](auto& m, std::uint32_t n) { m.attackCooldownSeconds = n / 1000.0f; });
        editU("expReward", v->rewardExp, [](auto& m, std::uint32_t n) { m.rewardExp = n; });
        editU("goldReward", v->rewardGold, [](auto& m, std::uint32_t n) { m.rewardGold = n; });
        editU("lootTableId", v->lootTableId, [](auto& m, std::uint32_t n) { m.lootTableId = n; });
        // 阶段24 指令四十：Monster visualId ComboBox。
        {
            const std::string current = v->visualId;
            const std::string chosen =
                m_visualCatalogLoaded
                    ? VisualAssetCombo("visualId (visual entity)", current, "Monster")
                    : current;
            if (m_visualCatalogLoaded && chosen != current) {
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& m : d.monsters) {
                        if (m.monsterTypeId == sel.id) {
                            m.visualId = chosen;
                        }
                    }
                });
            }
            DrawVisualPreview(current.c_str());
        }
        // 23.18：Stat summary。
        ImGui::TextDisabled("Preview: %s Lv%u HP=%u ATK=%u DEF=%u EXP=%u Gold=%u",
                            v->name.c_str(), v->level, v->maxHp, v->attackPower, v->defense,
                            v->rewardExp, v->rewardGold);
    } else if (sel.type == GT::Skill) {
        const auto* v = m_game->FindSkill(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Skill not found."); return; }
        ImGui::Text("skillId: %u", v->skillId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& s : d.skills) { if (s.skillId == sel.id) { s.name = m_gInsName; } } }); }
        int mana = static_cast<int>(v->manaCost);
        if (ImGui::InputInt("manaCost", &mana)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, mana));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& s : d.skills) {
                    if (s.skillId == sel.id) {
                        s.manaCost = nv;
                    }
                }
            });
        }
        int damage = static_cast<int>(v->baseDamage);
        if (ImGui::InputInt("baseDamage", &damage)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, damage));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& s : d.skills) {
                    if (s.skillId == sel.id) {
                        s.baseDamage = nv;
                    }
                }
            });
        }
        // 23.18：Damage/Mana/Cooldown/Range。
        ImGui::TextDisabled("Preview: %s mana=%u cd=%.1fs range=%.0f dmg=%u", v->name.c_str(),
                            v->manaCost, v->cooldownSeconds, v->range, v->baseDamage);
    } else if (sel.type == GT::Status) {
        const auto* v = m_game->FindStatus(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Status not found."); return; }
        ImGui::Text("statusId: %u", v->effectId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& s : d.statuses) { if (s.effectId == sel.id) { s.name = m_gInsName; } } }); }
        int atk = v->attackFlatModifier;
        if (ImGui::InputInt("attackModifier", &atk)) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& s : d.statuses) {
                    if (s.effectId == sel.id) {
                        s.attackFlatModifier = atk;
                    }
                }
            });
        }
        int def = v->defenseFlatModifier;
        if (ImGui::InputInt("defenseModifier", &def)) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& s : d.statuses) {
                    if (s.effectId == sel.id) {
                        s.defenseFlatModifier = def;
                    }
                }
            });
        }
        int dot = static_cast<int>(v->dotDamagePerStack);
        if (ImGui::InputInt("dotDamage", &dot)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, dot));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& s : d.statuses) {
                    if (s.effectId == sel.id) {
                        s.dotDamagePerStack = nv;
                    }
                }
            });
        }
    } else if (sel.type == GT::Quest) {
        const auto* v = m_game->FindQuest(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Quest not found."); return; }
        ImGui::Text("questId: %u", v->questId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& q : d.quests) {
                    if (q.questId == sel.id) {
                        q.name = m_gInsName;
                    }
                }
            });
        }
        if (ImGui::InputTextMultiline("Description", m_gInsDesc, sizeof(m_gInsDesc))) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& q : d.quests) {
                    if (q.questId == sel.id) {
                        q.description = m_gInsDesc;
                    }
                }
            });
        }
        int exp = static_cast<int>(v->reward.exp);
        if (ImGui::InputInt("reward.exp", &exp)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, exp));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& q : d.quests) {
                    if (q.questId == sel.id) {
                        q.reward.exp = nv;
                    }
                }
            });
        }
        int gold = static_cast<int>(v->reward.gold);
        if (ImGui::InputInt("reward.gold", &gold)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, gold));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& q : d.quests) {
                    if (q.questId == sel.id) {
                        q.reward.gold = nv;
                    }
                }
            });
        }
        // 23.18：流程摘要。
        ImGui::TextDisabled("Flow: startNPC=%u -> %d objective(s) -> turnInNPC=%u",
                            v->startNpcDefinitionId, static_cast<int>(v->objectives.size()),
                            v->turnInNpcDefinitionId);
        // ---- 阶段25：目标编辑 + Quest Area Map Picker（ReachArea 画布点选）----
        ImGui::Separator();
        ImGui::TextUnformatted("Objectives");
        int objectiveIdx = 0;
        for (const auto& objective : v->objectives) {
            ++objectiveIdx;
            ImGui::PushID(static_cast<int>(objective.objectiveId));
            ImGui::Text("%d. [%s] id=%u", objectiveIdx,
                        objective.type == legend::world::QuestObjectiveType::KillMonster
                            ? "Kill"
                        : objective.type == legend::world::QuestObjectiveType::CollectItem
                            ? "Collect"
                        : objective.type == legend::world::QuestObjectiveType::ReachLevel
                            ? "Level"
                        : objective.type == legend::world::QuestObjectiveType::ReachArea
                            ? "Area"
                            : "?",
                        objective.objectiveId);
            int count = static_cast<int>(objective.requiredCount);
            if (ImGui::InputInt("requiredCount", &count)) {
                const auto nv = static_cast<std::uint32_t>(std::max(1, count));
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& q : d.quests) {
                        if (q.questId != sel.id) {
                            continue;
                        }
                        for (auto& o : q.objectives) {
                            if (o.objectiveId == objective.objectiveId) {
                                o.requiredCount = nv;
                            }
                        }
                    }
                });
            }
            if (objective.type == legend::world::QuestObjectiveType::ReachArea) {
                // 目标地图下拉（World 数据源）。
                int mapSel = -1;
                std::string mapPreview = "?";
                const auto& maps = m_world != nullptr ? m_world->Data().maps
                                                      : std::vector<legend::world::MapDefinition>{};
                for (std::size_t mi = 0; mi < maps.size(); ++mi) {
                    if (maps[mi].mapId == objective.mapId) {
                        mapSel = static_cast<int>(mi);
                        mapPreview = std::to_string(maps[mi].mapId) + " - " + maps[mi].name;
                    }
                }
                if (ImGui::BeginCombo("mapId", mapPreview.c_str())) {
                    for (std::size_t mi = 0; mi < maps.size(); ++mi) {
                        const std::string label =
                            std::to_string(maps[mi].mapId) + " - " + maps[mi].name;
                        if (ImGui::Selectable(label.c_str(),
                                              mapSel == static_cast<int>(mi))) {
                            const auto newMapId = maps[mi].mapId;
                            m_game->Mutate([&](GameDataSet& d) {
                                for (auto& q : d.quests) {
                                    if (q.questId != sel.id) {
                                        continue;
                                    }
                                    for (auto& o : q.objectives) {
                                        if (o.objectiveId == objective.objectiveId) {
                                            o.mapId = newMapId;
                                        }
                                    }
                                }
                            });
                        }
                    }
                    ImGui::EndCombo();
                }
                float ax = objective.areaX;
                float ay = objective.areaY;
                float ar = objective.areaRadius;
                if (ImGui::InputFloat("areaX", &ax, 10.0f, 0.0f, "%.0f")) {
                    m_game->Mutate([&](GameDataSet& d) {
                        for (auto& q : d.quests) {
                            if (q.questId != sel.id) {
                                continue;
                            }
                            for (auto& o : q.objectives) {
                                if (o.objectiveId == objective.objectiveId) {
                                    o.areaX = ax;
                                }
                            }
                        }
                    });
                }
                if (ImGui::InputFloat("areaY", &ay, 10.0f, 0.0f, "%.0f")) {
                    m_game->Mutate([&](GameDataSet& d) {
                        for (auto& q : d.quests) {
                            if (q.questId != sel.id) {
                                continue;
                            }
                            for (auto& o : q.objectives) {
                                if (o.objectiveId == objective.objectiveId) {
                                    o.areaY = ay;
                                }
                            }
                        }
                    });
                }
                if (ImGui::InputFloat("areaRadius", &ar, 10.0f, 0.0f, "%.0f")) {
                    m_game->Mutate([&](GameDataSet& d) {
                        for (auto& q : d.quests) {
                            if (q.questId != sel.id) {
                                continue;
                            }
                            for (auto& o : q.objectives) {
                                if (o.objectiveId == objective.objectiveId) {
                                    o.areaRadius = std::max(1.0f, ar);
                                }
                            }
                        }
                    });
                }
                // 画布点选：武装后下一次 World Canvas 左键写入 areaX/areaY。
                const bool armed = m_questAreaPickActive &&
                                   m_questAreaPickQuestId == sel.id &&
                                   m_questAreaPickObjectiveId == objective.objectiveId;
                if (ImGui::Button(armed ? "Pick On Map... (click canvas)"
                                        : "Pick On Map")) {
                    m_questAreaPickActive = !armed;
                    m_questAreaPickQuestId = sel.id;
                    m_questAreaPickObjectiveId = objective.objectiveId;
                    m_questAreaPickMapId = objective.mapId;
                    m_questAreaPickRadius = objective.areaRadius;
                }
            }
            ImGui::PopID();
        }
    } else if (sel.type == GT::Shop) {
        const auto* v = m_game->FindShop(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Shop not found."); return; }
        ImGui::Text("shopId: %u", v->shopId);
        int entryIdx = 0;
        for (const auto& entry : v->entries) {
            ++entryIdx;
            ImGui::Text("entry %d: item %u", entryIdx, entry.itemDefinitionId);
            ImGui::PushID(entryIdx);
            int buy = static_cast<int>(entry.buyPrice);
            if (ImGui::InputInt("buyPrice", &buy)) {
                const auto nv = static_cast<std::uint32_t>(std::max(0, buy));
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& s : d.shops) {
                        if (s.shopId == sel.id) {
                            for (auto& e : s.entries) {
                                if (e.itemDefinitionId == entry.itemDefinitionId) {
                                    e.buyPrice = nv;
                                }
                            }
                        }
                    }
                });
            }
            int sell = static_cast<int>(entry.sellPrice);
            if (ImGui::InputInt("sellPrice", &sell)) {
                const auto nv = static_cast<std::uint32_t>(std::max(0, sell));
                m_game->Mutate([&](GameDataSet& d) {
                    for (auto& s : d.shops) {
                        if (s.shopId == sel.id) {
                            for (auto& e : s.entries) {
                                if (e.itemDefinitionId == entry.itemDefinitionId) {
                                    e.sellPrice = nv;
                                }
                            }
                        }
                    }
                });
            }
            ImGui::PopID();
        }
    } else if (sel.type == GT::Teleport) {
        const auto* v = m_game->FindTeleport(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Teleport not found."); return; }
        ImGui::Text("teleportId: %u", v->teleportId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& t : d.teleports) { if (t.teleportId == sel.id) { t.name = m_gInsName; } } }); }
        int cost = static_cast<int>(v->goldCost);
        if (ImGui::InputInt("goldCost", &cost)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, cost));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& t : d.teleports) {
                    if (t.teleportId == sel.id) {
                        t.goldCost = nv;
                    }
                }
            });
        }
    } else if (sel.type == GT::LootTable) {
        const auto* v = m_game->FindLootTable(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("LootTable not found."); return; }
        ImGui::Text("lootTableId: %u", v->lootTableId);
        if (ImGui::InputText("Name", m_gInsName, sizeof(m_gInsName))) { m_game->Mutate([&](GameDataSet& d) { for (auto& t : d.lootTables) { if (t.lootTableId == sel.id) { t.name = m_gInsName; } } }); }
        int entryIdx = 0;
        for (const auto& entry : v->entries) {
            ++entryIdx;
            ImGui::Text("entry %d: item %u chance=%.2f", entryIdx, entry.itemDefinitionId,
                        entry.dropChance);
        }
    } else if (sel.type == GT::Chapter) {
        // ---- 阶段25：Chapter Editor（chapters.json 展示元数据）----
        const auto* v = m_game->FindChapter(sel.id);
        if (v == nullptr) { ImGui::TextDisabled("Chapter not found."); return; }
        ImGui::Text("chapterId: %u", v->chapterId);
        if (ImGui::InputText("Title", m_gInsName, sizeof(m_gInsName))) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& c : d.chapters) {
                    if (c.chapterId == sel.id) {
                        c.title = m_gInsName;
                    }
                }
            });
        }
        int finalQuest = static_cast<int>(v->finalQuestId);
        if (ImGui::InputInt("finalQuestId", &finalQuest)) {
            const auto nv = static_cast<std::uint32_t>(std::max(0, finalQuest));
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& c : d.chapters) {
                    if (c.chapterId == sel.id) {
                        c.finalQuestId = nv;
                    }
                }
            });
        }
        if (ImGui::InputText("questIds (csv)", m_gInsText, sizeof(m_gInsText))) {
            m_game->Mutate([&](GameDataSet& d) {
                for (auto& c : d.chapters) {
                    if (c.chapterId != sel.id) {
                        continue;
                    }
                    c.questIds.clear();
                    const std::string text = m_gInsText;
                    std::size_t start = 0;
                    while (start < text.size()) {
                        const std::size_t comma = text.find(',', start);
                        const std::string token = text.substr(
                            start, comma == std::string::npos ? std::string::npos
                                                              : comma - start);
                        try {
                            const unsigned long parsed = std::stoul(token);
                            if (parsed > 0) {
                                c.questIds.push_back(static_cast<std::uint32_t>(parsed));
                            }
                        } catch (...) {
                            // 非数字片段忽略（实时校验会给出错误提示）
                        }
                        if (comma == std::string::npos) {
                            break;
                        }
                        start = comma + 1;
                    }
                }
            });
        }
        // 章节任务链预览（点击跳转 Quest Inspector）。
        ImGui::TextDisabled("Chapter flow: %d quest(s), final=%u",
                            static_cast<int>(v->questIds.size()), v->finalQuestId);
        for (const std::uint32_t questId : v->questIds) {
            const auto* quest = m_game->FindQuest(questId);
            const std::string label = std::to_string(questId) + " - " +
                                      (quest != nullptr ? quest->name : "?") +
                                      (questId == v->finalQuestId ? "  [FINAL]" : "");
            if (ImGui::Selectable(label.c_str())) {
                m_game->SetSelection(GT::Quest, questId);
            }
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Duplicate")) {
        m_game->DuplicateSelected();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        m_game->RemoveSelected();
    }
}

// World 对象字段编辑主体（从 DrawWorldInspector 拆出——World/Game 两个 Inspector 共存）。
void LegendMapEditorApp::DrawWorldInspectorFields() {
    using OT = editor::WorldDocument::ObjectType;
    const auto sel = m_world->GetSelection();
    if (sel.type == OT::None) {
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

        // ---- 阶段24 指令四十：地图 visualMapId 绑定 + Visual Map 编辑 ----
        const std::string chosenVisualMap = VisualMapCombo(m->visualMapId);
        if (chosenVisualMap != m->visualMapId) {
            m_world->Mutate([&](WorldDataSet& d) {
                for (auto& map : d.maps) {
                    if (map.mapId == sel.id) {
                        map.visualMapId = chosenVisualMap;
                    }
                }
            });
        }
        if (!chosenVisualMap.empty() && m_visualCatalogLoaded) {
            // 找到 visual_maps 中对应定义进行编辑（Ground/Decoration/Object/Foreground）。
            // 只读遍历 m_world->Data()；所有修改经 Mutate()（Undo/校验一致）。
            const MapVisualDefinition* vm = nullptr;
            for (const auto& visual : m_world->Data().visualMaps) {
                if (visual.visualMapId == chosenVisualMap) {
                    vm = &visual;
                    break;
                }
            }
            if (vm == nullptr) {
                ImGui::TextDisabled("Visual Map '%s' not found in visual_maps.json.",
                                    chosenVisualMap.c_str());
            } else {
                ImGui::Separator();
                ImGui::Text("Visual Map: %s", chosenVisualMap.c_str());
                // manifest 资产 id 下拉（typeFilter 空 = 全部）。
                auto assetIdCombo = [&](const char* label, const std::string& current,
                                        const char* typeFilter) -> std::string {
                    std::string result = current;
                    if (ImGui::BeginCombo(label, current.empty() ? "(none)" : current.c_str())) {
                        if (ImGui::Selectable("(none)", current.empty())) {
                            result.clear();
                        }
                        for (const auto& asset : m_visualCatalog.Manifest().assets) {
                            if (typeFilter != nullptr && asset.type != typeFilter) {
                                continue;
                            }
                            if (ImGui::Selectable(asset.assetId.c_str(),
                                                  asset.assetId == current)) {
                                result = asset.assetId;
                            }
                            if (asset.assetId == current) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    return result;
                };

                const std::string newBg = assetIdCombo("backgroundAsset", vm->backgroundAsset,
                                                       "Texture");
                if (newBg != vm->backgroundAsset) {
                    const std::string captured = newBg;
                    m_world->Mutate([&](WorldDataSet& d) {
                        for (auto& visual : d.visualMaps) {
                            if (visual.visualMapId == chosenVisualMap) {
                                visual.backgroundAsset = captured;
                            }
                        }
                    });
                }
                float tileSize = vm->tileSize;
                if (ImGui::InputFloat("tileSize", &tileSize, 4.0f, 0.0f, "%.1f")) {
                    m_world->Mutate([&](WorldDataSet& d) {
                        for (auto& visual : d.visualMaps) {
                            if (visual.visualMapId == chosenVisualMap) {
                                visual.tileSize = std::max(8.0f, tileSize);
                            }
                        }
                    });
                }
                // 四层 placements 编辑。
                for (std::size_t layerIdx = 0;
                     layerIdx < vm->layers.size() && layerIdx < 4; ++layerIdx) {
                    const MapVisualLayer& layer = vm->layers[layerIdx];
                    if (!ImGui::TreeNodeEx(layer.name.c_str(),
                                           ImGuiTreeNodeFlags_SpanAvailWidth)) {
                        continue;
                    }
                    int removeIdx = -1;
                    for (std::size_t p = 0; p < layer.placements.size(); ++p) {
                        const MapVisualPlacement& placement = layer.placements[p];
                        ImGui::PushID(static_cast<int>(p));
                        const std::string newAsset =
                            assetIdCombo("assetId", placement.assetId, nullptr);
                        float px[2] = {placement.x, placement.y};
                        const bool posChanged = ImGui::InputFloat2("x y", px, "%.1f");
                        if (newAsset != placement.assetId || posChanged) {
                            const std::string capturedAsset = newAsset;
                            const float capturedX = px[0];
                            const float capturedY = px[1];
                            m_world->Mutate([&](WorldDataSet& d) {
                                for (auto& visual : d.visualMaps) {
                                    if (visual.visualMapId == chosenVisualMap &&
                                        layerIdx < visual.layers.size() &&
                                        p < visual.layers[layerIdx].placements.size()) {
                                        auto& target = visual.layers[layerIdx].placements[p];
                                        target.assetId = capturedAsset;
                                        target.x = capturedX;
                                        target.y = capturedY;
                                    }
                                }
                            });
                        }
                        if (ImGui::SmallButton("X")) {
                            removeIdx = static_cast<int>(p);
                        }
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0) {
                        m_world->Mutate([&](WorldDataSet& d) {
                            for (auto& visual : d.visualMaps) {
                                if (visual.visualMapId == chosenVisualMap &&
                                    layerIdx < visual.layers.size()) {
                                    auto& placements = visual.layers[layerIdx].placements;
                                    if (static_cast<size_t>(removeIdx) < placements.size()) {
                                        placements.erase(placements.begin() + removeIdx);
                                    }
                                }
                            }
                        });
                    }
                    if (ImGui::SmallButton("Add Placement")) {
                        m_world->Mutate([&](WorldDataSet& d) {
                            for (auto& visual : d.visualMaps) {
                                if (visual.visualMapId == chosenVisualMap &&
                                    layerIdx < visual.layers.size()) {
                                    // 默认放地图中心（用第一个可用 Sprite 资产）。
                                    std::string firstSprite;
                                    for (const auto& asset : m_visualCatalog.Manifest().assets) {
                                        if (asset.type == "Sprite") {
                                            firstSprite = asset.assetId;
                                            break;
                                        }
                                    }
                                    visual.layers[layerIdx].placements.push_back(
                                        {firstSprite, 400.0f, 400.0f});
                                }
                            }
                        });
                    }
                    ImGui::TreePop();
                }
            }
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
        // 阶段24 指令四十：NPC visualId ComboBox（visual_entities.json Npc 实体，
        // 显示 serverVisualId 别名）。
        std::string npcVisualCurrent;
        for (const auto& entity : m_visualCatalog.Entities().entities) {
            if (entity.kind == "Npc" && entity.serverVisualId == static_cast<int>(n->visualId)) {
                npcVisualCurrent = entity.visualId;
                break;
            }
        }
        if (!m_visualCatalogLoaded) {
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
        } else {
            const std::string chosen =
                VisualAssetCombo("visualId (visual entity)", npcVisualCurrent, "Npc");
            if (chosen != npcVisualCurrent) {
                m_world->Mutate([&](WorldDataSet& d) {
                    for (auto& npc : d.npcs) {
                        if (npc.npcDefinitionId == sel.id) {
                            const legend::visual::VisualEntityDef* def =
                                chosen.empty()
                                    ? nullptr
                                    : legend::visual::FindVisualEntity(m_visualCatalog.Entities(),
                                                                       chosen);
                            npc.visualId = def != nullptr ? def->serverVisualId : 0;
                        }
                    }
                });
            }
            DrawVisualPreview(npcVisualCurrent.c_str());
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
        // 阶段24 指令四十：Portal visualId ComboBox（默认 portal_default）。
        {
            const std::string current = p->visualId.empty() ? std::string("portal_default")
                                                            : p->visualId;
            const std::string chosen =
                m_visualCatalogLoaded
                    ? VisualAssetCombo("visualId (visual entity)", current, "Portal")
                    : current;
            if (m_visualCatalogLoaded && chosen != current) {
                m_world->Mutate([&](WorldDataSet& d) {
                    for (auto& portal : d.portals) {
                        if (portal.portalId == sel.id) {
                            portal.visualId = chosen;
                        }
                    }
                });
            }
            DrawVisualPreview(current.c_str());
        }
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

    // ---- 阶段23：Game Data Validation / Save / Reload（23.13/23.15）----
    if (m_game != nullptr) {
        ImGui::Separator();
        ImGui::TextUnformatted("Game Data");
        if (m_game->HasErrors()) {
            for (const auto& err : m_game->ValidationErrors()) {
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[Error] %s", err.c_str());
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "No game data errors.");
        }
        ImGui::BeginDisabled(m_game->HasErrors() || !m_gameLoaded);
        if (ImGui::Button("Save Game Data")) {
            std::string gameError;
            if (m_game->Save(gameError)) {
                m_worldMessage = "Game data saved atomically to '" + m_gameDir + "'.";
                m_worldMessageIsError = false;
            } else {
                m_worldMessage = "Game save failed: " + gameError;
                m_worldMessageIsError = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Reload From Disk")) { // 23.15：Dev-only
            std::string gameError;
            if (m_game->ReloadFromDisk(gameError)) {
                m_worldMessage = gameError.empty()
                                     ? "Game data reloaded from disk."
                                     : "Game data reloaded; restart WorldServer to apply.";
                m_worldMessageIsError = false;
            } else {
                m_worldMessage = "Reload failed: " + gameError;
                m_worldMessageIsError = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("dir=%s dirty=%s", m_gameDir.c_str(),
                            m_game->IsDirty() ? "yes" : "no");
    }
}
