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
    void OnWorldEvent(const WorldNetworkEvent& event);

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

    // 本地玩家视觉位置（相机跟随；服务器位置 1-exp(-12dt) 平滑，>300 snap）。
    float LocalVisualX() const { return m_localVisualX; }
    float LocalVisualY() const { return m_localVisualY; }

    // F10：热重载（manifest/animation/effect 重新加载 + 纹理重载；失败保留旧资源）。
    void ReloadAssets();

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

    void ClearTransientVisuals(); // MapChanged/断线：特效/飘字/实体视觉清场

    legend::resource::ResourceManager* m_resources = nullptr;
    legend::render::Shader* m_spriteShader = nullptr;
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
};

} // namespace legend::client
