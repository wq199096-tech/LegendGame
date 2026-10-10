#pragma once

// ---------------------------------------------------------------------------
// 阶段24：Client Visual Runtime —— 真实资源渲染门面。
//
// 职责（指令二十四总体目标）：
//   AssetManager（纹理缓存/fallback）+ 统一 AnimationPlayer + 实体视觉
//   （本地/远程玩家/怪物/NPC/Portal）+ 地图视觉层（Ground/Decoration/Object/
//   Foreground + Fallback Grid）+ 技能 VFX/Projectile + 伤害飘字 + 名字板/头顶
//   血条 + Game HUD + F9 统计 + F10 热重载。
//
// 纪律：
//   - 网络逻辑不进本类（只消费 WorldClientController 只读镜像 + 只读事件钩子）
//   - 服务器权威：伤害/位置/技能判定全部来自服务器事件；Projectile 只是视觉
//   - 缺失资源走 fallback，绝不黑屏/崩溃
// ---------------------------------------------------------------------------

#include "Client/Assets/AssetManager.h"
#include "Client/Audio/AudioRuntime.h"
#include "Client/Ui/ChapterDisplayCatalog.h"
#include "Client/Ui/ChatModel.h"
#include "Client/Ui/ItemDisplayCatalog.h"
#include "Client/Ui/UiModels.h"
#include "Client/Ui/UiTheme.h"
#include "Client/Visuals/AnimationPlayer.h"
#include "Client/Visuals/Font.h"
#include "Client/Visuals/VisualAssetData.h"
#include "Client/Visuals/VisualDataCatalog.h"

#include "Engine/Math/Color.h"
#include "Engine/Math/Vector2.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/SpriteBatch.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace legend::render {
class Camera2D;
class Shader;
class Texture;
}

namespace legend::resource {
class ResourceManager;
}

namespace legend::flow {
struct FlowUiModel;
}

namespace legend::client {

class WorldClientController;
struct WorldNetworkEvent;

// Shared 视觉定义引入（visual_maps.json 结构体）。
using legend::world::MapVisualDefinition;
using legend::world::MapVisualLayer;
using legend::world::MapVisualPlacement;

class VisualRuntime {
public:
    // dataRoot 可空（自动 FindDataRoot）。加载失败不崩溃：IsReady()=false，
    // GameScene 保持旧渲染路径（离线 Debug 模式不受影响）。
    bool Initialize(legend::resource::ResourceManager& resources,
                    legend::render::Shader& spriteShader, const std::string& dataRootOverride);
    void Shutdown();
    bool IsReady() const { return m_ready; }

    // ---- 事件钩子（GameScene 经 WorldClientController::SetVisualEventHook 转发）----
    void OnWorldEvent(const WorldNetworkEvent& event, const WorldClientController& world);

    // ---- 每帧更新（动画推进/特效/飘字/CD/本地平滑）----
    void Update(const WorldClientController& world, float deltaTime);

    // ---- 世界层渲染（mapRenderer 批：Ground -> Decoration -> Y排序实体 -> Foreground
    //      -> Effects）。返回本类可见 sprite 数（统计）。----
    int RenderWorld(legend::render::SpriteBatch& batch, const legend::render::Camera2D& camera,
                    float viewLeft, float viewTop, float viewRight, float viewBottom,
                    const WorldClientController& world);

    // ---- 世界空间名字板/头顶血条/伤害飘字（实体之后调用）----
    void RenderOverlays(legend::render::SpriteBatch& batch,
                        const WorldClientController& world);

    // ---- HUD（屏幕空间；uiBatch 由本类持有，identity camera）----
    void RenderHUD(const WorldClientController& world, const std::string& playerName,
                   float viewportWidth, float viewportHeight, bool mapDebug);

    // ---- 阶段25 指令五十三/五十四：World 连接期间 Loading 覆盖层（不黑屏，轻提示）----
    void RenderLoading(float viewportWidth, float viewportHeight);

    // ---- Stage26 指令二十一/二十二：玩家流程页（Boot/Connecting/Login/Register/
    //      Lobby/Create/EnteringWorld/Disconnected/FatalError；实现于 FlowPages.cpp）。
    //      GameScene::Render 早退分支调用；动作经 DrainUiRequests 回 GameScene。----
    void RenderFlowPages(const flow::FlowUiModel& model, float viewportWidth,
                         float viewportHeight);
    // Stage26 指令二十七：角色大厅立绘（identity 相机 + 手工 EntityVisual；
    // 实现/状态在 FlowPages.cpp）。
    void RenderCharacterPortrait(const std::string& visualId, const math::Vector2& topLeft,
                                 float width, float height);
    // Stage26 指令十一：本地玩家进世界用大厅选中的造型渲染（覆盖 classId 推导）。
    void SetLocalPlayerVisualOverride(std::uint16_t visualId);
    // 流程页/立绘渲染需要的 uiBatch/text 访问（FlowPages.cpp 内部使用为主）。
    legend::render::SpriteBatch& UiBatch() { return m_uiBatch; }
    TextRenderer& Text() { return m_text; }
    bool TextReady() const { return m_text.IsReady(); }

    // 本地玩家视觉位置（相机跟随；服务器位置 1-exp(-12dt) 平滑，>300 snap）。
    float LocalVisualX() const { return m_localVisualX; }
    float LocalVisualY() const { return m_localVisualY; }

    // F10：热重载（manifest/animation/effect 重新加载 + 纹理重载；失败保留旧资源）。
    void ReloadAssets();

    // ---- 阶段25：正式 UI（指令十六~六十三）----
    // 窗口开关（按键在 GameScene 处理，状态在这里）。
    void ToggleInventory() { m_inventoryVisible = !m_inventoryVisible; }
    void ToggleCharacterPanel() { m_characterVisible = !m_characterVisible; }
    void ToggleSettings() { m_settingsVisible = !m_settingsVisible; }
    bool InventoryVisible() const { return m_inventoryVisible; }
    bool CharacterPanelVisible() const { return m_characterVisible; }
    bool SettingsVisible() const { return m_settingsVisible; }
    // Stage27 指令七：本地玩家名字板可配置显示（第一版默认显示）。
    void ToggleShowLocalName() { m_showLocalName = !m_showLocalName; }
    bool ShowLocalName() const { return m_showLocalName; }

    // ---- Stage27：聊天窗口（指令十九~二十六；模型纯数据，渲染在本类）----
    ChatModel& Chat() { return m_chat; }
    const ChatModel& Chat() const { return m_chat; }
    // GameScene 每帧喂入 IME 组合串（仅展示预览，不进 draft）。
    void SetChatComposition(const std::string& composition) { m_chatComposition = composition; }
    // GameScene 每帧喂入滚轮增量（聊天历史滚动用）。
    void SetMouseWheel(float delta) { m_lastMouseWheelDelta = delta; }
    // 聊天窗口渲染（RenderHUD 链尾调用；左下角，屏幕空间）。
    // 实现于 ChatWindow.cpp（与 FlowPages.cpp 同模式拆分）。
    void RenderChatWindow(const WorldClientController& world, float viewportWidth,
                          float viewportHeight);
    // Settings 值（Audio 模块每帧读取；G 任务）。
    float MasterVolume() const { return m_masterVolume; }
    float MusicVolume() const { return m_musicVolume; }
    float SfxVolume() const { return m_sfxVolume; }
    // UI 渲染产生的请求（GameScene 每帧 Drain 后转发给 WorldClientController——
    // 服务器依旧权威，UI 只发意图）。
    struct UiRequest {
        enum class Kind {
            DialogueOption,   // index = 1-based option
            ShopBuy,          // index = shop entry 0-based
            ShopSell,         // instanceId + quantity 1
            EquipDefinition,  // definitionId（SendEquipFirstOf）
            UnequipSlot,      // equipmentSlot (1=Weapon 2=Armor)
            WindowFullscreen, // 切换全屏（GameScene 执行）
            WindowResolution, // index = 分辨率档位
            FlowUi,           // Stage26：流程页动作（flowAction = FlowUiAction::Kind）
            Chat,             // Stage27：聊天动作（index 1 = 发送草稿）
        };
        Kind kind = Kind::DialogueOption;
        int index = 0;
        std::uint32_t definitionId = 0;
        std::uint64_t instanceId = 0;
        std::uint8_t equipmentSlot = 0;
        // Stage26：流程页动作/焦点/选型（index 复用：字段 id / 角色下标 / visualId-1）。
        std::uint8_t flowAction = 0; // FlowUiAction::Kind
    };
    std::vector<UiRequest> DrainUiRequests();
    // 鼠标状态（GameScene 每帧喂入；窗口点击交互用）。
    void SetMouseState(float x, float y, bool clicked) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        if (clicked) {
            m_lastMouseClicked = true;
        }
    }

    // ---- 统计（F9 面板；指令四十二）----
    struct Stats {
        int visibleSprites = 0;
        int drawCalls = 0;
        int texturesLoaded = 0;
        int effectsActive = 0;
    };
    const Stats& GetStats() const { return m_stats; }
    const legend::visual::VisualDataCatalog* Catalog() const { return m_catalog.get(); }

private:
    // ---- 实体视觉状态（统一 AnimationPlayer；指令十六禁止三套播放器）----
    enum class EntityKind { LocalPlayer, RemotePlayer, Monster, Npc, Portal };

    struct EntityVisual {
        EntityKind kind = EntityKind::RemotePlayer;
        std::string visualId;      // visual_entities.json 引用（空 = fallback 绘制）
        visual::AnimationPlayer player;
        std::string slot;          // idle/walk/attack/cast/hit/death
        int direction = 0;         // Direction8 顺序
        float attackHold = 0.0f;   // 攻击动画剩余时间
        float hitHold = 0.0f;      // 受击动画剩余时间
        bool dead = false;
        float prevX = 0.0f;        // 方向估算（render 位置差分）
        float prevY = 0.0f;
        bool hasPrev = false;
    };

    EntityVisual& EnsureVisual(std::unordered_map<std::uint64_t, EntityVisual>& map,
                               std::uint64_t id, EntityKind kind, const std::string& visualId);
    // visualId -> clip 表绑定（EnsureVisual/热重载共用；找不到定义时保持 fallback）。
    void ApplyEntityDefinition(EntityVisual& ev);
    void PruneVisuals(const WorldClientController& world);
    void UpdateEntityVisual(EntityVisual& ev, float dt, bool moving, bool casting, bool alive);
    void ApplySlot(EntityVisual& ev, const char* slotName);
    void DrawEntitySprite(legend::render::SpriteBatch& batch, EntityVisual& ev,
                          const math::Vector2& feet, float scaleBoost = 1.0f);
    // 层物件绘制（Decoration/Object/Foreground 共用；layerIndex: 1/2/3）。
    void DrawPlacements(legend::render::SpriteBatch& batch, const MapVisualDefinition* visual,
                        int layerIndex, float viewLeft, float viewTop, float viewRight,
                        float viewBottom);

    // ---- 技能 VFX ----
    struct ActiveEffect {
        std::uint64_t castId = 0;      // 关联施法（Projectile/Whirlwind 移除用）
        const visual::EffectDef* def = nullptr;
        math::Vector2 pos;
        float elapsed = 0.0f;
        float totalDuration = 0.0f;
        bool looping = false;
        bool projectile = false;
        math::Vector2 from;
        math::Vector2 to;
        float travelTime = 0.0f;
    };
    void SpawnEffect(const std::string& effectId, const math::Vector2& pos,
                     std::uint64_t castId = 0);
    void SpawnProjectile(const std::string& effectId, const math::Vector2& from,
                         const math::Vector2& to, std::uint64_t castId);
    void DrawEffects(legend::render::SpriteBatch& batch);

    // ---- 伤害飘字 ----
    struct DamageNumber {
        math::Vector2 pos;
        math::Vector2 velocity;
        float age = 0.0f;
        float life = 0.9f;
        std::string text;
        math::Color color;
    };
    void SpawnDamageNumber(const math::Vector2& pos, std::uint32_t amount, bool isDot,
                           bool onSelf);

    // ---- HUD helpers ----
    void DrawBar(legend::render::SpriteBatch& batch, const math::Vector2& topLeft, float width,
                 float height, float pct, const math::Color& fill);
    void DrawPanel(legend::render::SpriteBatch& batch, const math::Vector2& topLeft, float width,
                   float height, float alpha = 0.55f);

    // ---- 阶段25：正式 UI 状态 ----
    ui::ToastManager m_toasts;
    ui::MapBanner m_mapBanner;
    ui::BossBar m_bossBar;
    ui::LevelUpFx m_levelUpFx;
    ui::ItemDisplayCatalog m_itemDisplay;
    ui::ChapterDisplayCatalog m_chapterDisplay;
    ui::SkillSlotState m_skillSlots[3];
    std::uint16_t m_lastErrorToastCode = 0;
    std::uint32_t m_prevLevelForUi = 0;
    bool m_inventoryVisible = false;
    bool m_characterVisible = false;
    bool m_settingsVisible = false;
    bool m_showLocalName = true; // Stage27 指令七：本地玩家名字板默认显示
    int m_selectedInventorySlot = -1;
    int m_selectedShopIndex = -1;
    int m_lastClickedInventorySlot = -1;
    float m_inventoryClickTimer = 10.0f;
    // Settings 值（本地持久化 savedata/client_settings.json；指令五十一/六）。
    float m_masterVolume = 0.8f;
    float m_musicVolume = 0.6f;
    float m_sfxVolume = 0.8f;
    bool m_fullscreen = false;
    int m_resolutionIndex = 1; // 0=1280x720 1=1600x900 2=1920x1080
    bool m_tutorialShown = false;
    bool m_settingsDirty = false;
    float m_worldTimeSeconds = 0.0f;
    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_lastMouseClicked = false;
    float m_lastMouseWheelDelta = 0.0f; // Stage27：滚轮增量（聊天历史滚动）
    std::vector<UiRequest> m_uiRequests;

    // Stage27：聊天窗口状态（模型纯数据；组合串展示预览）。
    ChatModel m_chat;
    std::string m_chatComposition;

    void LoadClientSettings();
    void SaveClientSettings();
    void UpdateUiState(const WorldClientController& world, float deltaTime);
    void PushErrorToast(std::uint16_t errorCode);
    ui::MinimapModel BuildMinimapModel(const WorldClientController& world) const;
    void HandleUiMouse(const WorldClientController& world, const math::Vector2& refMouse,
                       bool clicked, float scale);
    void RenderHudV2(const WorldClientController& world, const std::string& playerName,
                     float viewportWidth, float viewportHeight, float scale, bool haveText);
    void RenderSkillBar(const WorldClientController& world, float viewportWidth,
                        float viewportHeight, float scale, bool haveText);
    void RenderTracker(const WorldClientController& world, float viewportWidth, float scale,
                       bool haveText);
    void RenderMinimap(const WorldClientController& world, float viewportWidth,
                       float viewportHeight, float scale, bool haveText);
    void RenderGlobalOverlays(const WorldClientController& world, float viewportWidth,
                              float viewportHeight, float scale, bool haveText);
    void RenderWindows(const WorldClientController& world, float viewportWidth,
                       float viewportHeight, float scale, bool haveText,
                       const math::Vector2& refMouse, bool mouseClicked);

    void ClearTransientVisuals(); // MapChanged/断线：特效/飘字/实体视觉清场

    legend::resource::ResourceManager* m_resources = nullptr;
    legend::render::Shader* m_spriteShader = nullptr;
    AudioRuntime m_audio; // 阶段25 指令四十八：Audio Runtime V1
    std::unique_ptr<legend::visual::VisualDataCatalog> m_catalog;
    AssetManager m_assets;
    TextRenderer m_text;
    bool m_ready = false;

    legend::render::SpriteBatch m_uiBatch;   // HUD 屏幕空间批
    legend::render::Camera2D m_identityCamera;

    std::shared_ptr<render::Texture> m_whiteTexture;

    std::unordered_map<std::uint64_t, EntityVisual> m_playerVisuals; // 远程 + 本地（characterId）
    std::unordered_map<std::uint64_t, EntityVisual> m_monsterVisuals;
    std::unordered_map<std::uint64_t, EntityVisual> m_npcVisuals;
    std::unordered_map<std::uint64_t, EntityVisual> m_portalVisuals;

    std::vector<ActiveEffect> m_effects;
    std::vector<DamageNumber> m_damageNumbers;
    std::unordered_map<std::uint32_t, float> m_skillCooldowns; // display only

    // 本地玩家视觉状态
    std::uint64_t m_localCharacterId = 0;
    std::string m_localName;
    int m_localClassId = 0;
    std::uint16_t m_localVisualOverride = 0; // Stage26：大厅造型覆盖（0=不覆盖）
    float m_localVisualX = 0.0f;
    float m_localVisualY = 0.0f;
    float m_localLastServerX = 0.0f;
    float m_localLastServerY = 0.0f;
    bool m_localHasServerPos = false;

    // 当前地图视觉缓存（MapChanged 重解析）
    std::string m_currentVisualMapId;
    const MapVisualDefinition* m_currentMapVisual = nullptr; // catalog 生命周期内有效
    bool m_loggedWorldReady = false; // [VisualSmoke] world-ready 只打一次

    Stats m_stats{};
    int m_drawCallBase = 0;
    int m_uiDrawCallBase = 0;

    // ---- Stage26：流程页状态（FlowPages.cpp 使用）----
    EntityVisual m_portraitVisual;          // 大厅立绘（单一实例；visualId 变化时重建）
    std::string m_portraitVisualId;         // 当前立绘实体名（空=未初始化）
    float m_portraitLastMs = 0.0f;          // 立绘动画推进（墙钟）
};

} // namespace legend::client
