#include "Engine/Map/MapRenderer.h"

#include <algorithm>

#include "Engine/Debug/Logger.h"
#include "Engine/Map/Map.h"
#include "Engine/Map/MapChunk.h"
#include "Engine/Map/ObjectLayer.h"
#include "Engine/Render/Camera2D.h"
#include "Engine/Render/Shader.h"
#include "Engine/Render/Texture.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::map {

bool MapRenderer::Initialize(render::Shader& spriteShader) {
    m_shader = &spriteShader;
    if (!m_batch.Initialize()) {
        LOG_ERROR("MapRenderer: SpriteBatch initialize failed.");
        return false;
    }
    LOG_INFO("MapRenderer initialized.");
    return true;
}

void MapRenderer::Shutdown() {
    m_batch.Shutdown();
    m_chunkCaches.clear();
    m_tileTextures.clear();
    m_objectTextures.clear();
}

void MapRenderer::RegisterTileTexture(uint16_t tileId,
                                      std::shared_ptr<render::Texture> texture) {
    m_tileTextures[tileId] = std::move(texture);
}

void MapRenderer::RegisterObjectTexture(const std::string& textureId,
                                        std::shared_ptr<render::Texture> texture) {
    m_objectTextures[textureId] = std::move(texture);
}

void MapRenderer::CreateDefaultPlaceholderTextures(legend::resource::ResourceManager& resources) {
    using legend::math::Color;
    // Tile 占位纹理（Tile ID -> 颜色区分）
    RegisterTileTexture(static_cast<uint16_t>(TileId::Grass),
                        resources.CreateCheckerTexture("map/tile/grass", 64, 16,
                                                       Color::FromRGBA8(86, 152, 74),
                                                       Color::FromRGBA8(70, 132, 60)));
    RegisterTileTexture(static_cast<uint16_t>(TileId::Dirt),
                        resources.CreateCheckerTexture("map/tile/dirt", 64, 16,
                                                       Color::FromRGBA8(168, 124, 80),
                                                       Color::FromRGBA8(150, 108, 66)));
    RegisterTileTexture(static_cast<uint16_t>(TileId::Stone),
                        resources.CreateCheckerTexture("map/tile/stone", 64, 16,
                                                       Color::FromRGBA8(140, 140, 148),
                                                       Color::FromRGBA8(112, 112, 120)));
    RegisterTileTexture(static_cast<uint16_t>(TileId::Water),
                        resources.CreateCheckerTexture("map/tile/water", 64, 16,
                                                       Color::FromRGBA8(62, 110, 200),
                                                       Color::FromRGBA8(48, 88, 170)));
    // 物件占位纹理
    RegisterObjectTexture("tree", resources.CreateCheckerTexture("map/obj/tree", 64, 16,
                                                                 Color::FromRGBA8(36, 96, 40),
                                                                 Color::FromRGBA8(24, 72, 28)));
    RegisterObjectTexture("rock", resources.CreateCheckerTexture("map/obj/rock", 64, 16,
                                                                 Color::FromRGBA8(144, 144, 150),
                                                                 Color::FromRGBA8(104, 104, 110)));
    RegisterObjectTexture("building", resources.CreateCheckerTexture("map/obj/building", 64, 16,
                                                                     Color::FromRGBA8(176, 140, 96),
                                                                     Color::FromRGBA8(140, 108, 70)));
    RegisterObjectTexture("flower", resources.CreateCheckerTexture("map/obj/flower", 64, 16,
                                                                   Color::FromRGBA8(238, 220, 90),
                                                                   Color::FromRGBA8(220, 180, 60)));
}

void MapRenderer::BeginFrame(render::Camera2D& camera, float viewportWidth, float viewportHeight) {
    m_viewportWidth = viewportWidth;
    m_viewportHeight = viewportHeight;

    m_currentStats = FrameStats{};

    // 可视世界范围（相机中心定义，y 轴向下）
    const float halfW = viewportWidth * 0.5f / camera.GetZoom();
    const float halfH = viewportHeight * 0.5f / camera.GetZoom();
    m_viewLeft = camera.GetPosition().x - halfW;
    m_viewRight = camera.GetPosition().x + halfW;
    m_viewTop = camera.GetPosition().y - halfH;
    m_viewBottom = camera.GetPosition().y + halfH;

    m_batch.Begin(*m_shader, camera, viewportWidth, viewportHeight);
}

void MapRenderer::RebuildChunkCache(Map& map, MapChunk& chunk, ChunkCache& cache) {
    cache.byTexture.clear();

    const int tileSize = map.GetTileSize();
    const int startTileX = chunk.GetStartTileX();
    const int startTileY = chunk.GetStartTileY();
    const int endTileX = std::min(startTileX + MapChunk::kSize, map.GetWidth());
    const int endTileY = std::min(startTileY + MapChunk::kSize, map.GetHeight());

    for (int ty = startTileY; ty < endTileY; ++ty) {
        for (int tx = startTileX; tx < endTileX; ++tx) {
            const uint16_t tileId = map.GetGroundTile(tx, ty);
            if (tileId == static_cast<uint16_t>(TileId::Empty)) {
                continue;
            }
            auto& vertexList = [&]() -> std::vector<float>& {
                for (auto& entry : cache.byTexture) {
                    if (entry.first == tileId) {
                        return entry.second;
                    }
                }
                cache.byTexture.emplace_back(tileId, std::vector<float>{});
                return cache.byTexture.back().second;
            }();

            // Tile 四边形：以 Tile 中心定位
            const float centerX = TileToWorldCenter(tx, static_cast<float>(tileSize));
            const float centerY = TileToWorldCenter(ty, static_cast<float>(tileSize));
            const float half = tileSize * 0.5f;

            const float corners[4][4] = {
                {centerX - half, centerY - half, 0.0f, 0.0f},
                {centerX + half, centerY - half, 1.0f, 0.0f},
                {centerX + half, centerY + half, 1.0f, 1.0f},
                {centerX - half, centerY + half, 0.0f, 1.0f},
            };
            const int indices[6] = {0, 1, 2, 0, 2, 3};
            for (int i = 0; i < 6; ++i) {
                const int idx = indices[i];
                vertexList.push_back(corners[idx][0]);
                vertexList.push_back(corners[idx][1]);
                vertexList.push_back(corners[idx][2]);
                vertexList.push_back(corners[idx][3]);
                vertexList.push_back(1.0f); // tint: 白色
                vertexList.push_back(1.0f);
                vertexList.push_back(1.0f);
                vertexList.push_back(1.0f);
            }
        }
    }
    cache.built = true;
    chunk.SetDirty(false);
}

void MapRenderer::RenderGround(Map& map) {
    // 每帧重置全部 Chunk 可见状态，保证 visible 真实反映当前帧
    map.ResetChunkVisibility();

    const int chunkCountX = map.GetChunkCountX();
    const int chunkCountY = map.GetChunkCountY();
    if (chunkCountX <= 0 || chunkCountY <= 0) {
        return;
    }

    // 可视 Chunk 范围 + 1 Chunk 边距（防边缘闪烁）
    const int cx0 = std::clamp(WorldToChunkIndex(m_viewLeft, static_cast<float>(map.GetTileSize())) - 1, 0, chunkCountX - 1);
    const int cx1 = std::clamp(WorldToChunkIndex(m_viewRight, static_cast<float>(map.GetTileSize())) + 1, 0, chunkCountX - 1);
    const int cy0 = std::clamp(WorldToChunkIndex(m_viewTop, static_cast<float>(map.GetTileSize())) - 1, 0, chunkCountY - 1);
    const int cy1 = std::clamp(WorldToChunkIndex(m_viewBottom, static_cast<float>(map.GetTileSize())) + 1, 0, chunkCountY - 1);

    for (int cy = cy0; cy <= cy1; ++cy) {
        for (int cx = cx0; cx <= cx1; ++cx) {
            MapChunk* chunk = map.GetChunk(cx, cy);
            if (chunk == nullptr) {
                continue;
            }
            chunk->SetVisible(true);
            ++m_currentStats.visibleChunks;

            const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cy)) << 32) |
                                 static_cast<uint32_t>(cx);
            ChunkCache& cache = m_chunkCaches[key];
            if (chunk->IsDirty() || !cache.built) {
                RebuildChunkCache(map, *chunk, cache);
            }

            // 按纹理分组批量提交（每 quad 48 float）
            for (const auto& [tileId, vertices] : cache.byTexture) {
                auto it = m_tileTextures.find(tileId);
                if (it == m_tileTextures.end() || !it->second) {
                    continue;
                }
                const size_t quadCount = vertices.size() / 48;
                m_batch.PushVertices(*it->second, vertices.data(), quadCount);
                m_currentStats.renderedTiles += static_cast<int>(quadCount);
            }
        }
    }
}

void MapRenderer::DrawMapObject(const MapObject& object) {
    if (object.textureId.empty()) {
        return;
    }
    const auto it = m_objectTextures.find(object.textureId);
    if (it == m_objectTextures.end() || !it->second) {
        if (m_missingTextureWarned != object.textureId) {
            LOG_WARN("MapRenderer: no texture registered for object textureId '" +
                     object.textureId + "', skipped.");
            m_missingTextureWarned = object.textureId;
        }
        return;
    }
    const auto& texture = it->second;
    legend::math::Vector2 scale(object.width / texture->GetWidth(),
                                object.height / texture->GetHeight());
    m_batch.DrawQuad(*texture, {object.x, object.y}, scale, object.rotationDegrees,
                     legend::math::Color(1.0f, 1.0f, 1.0f, 1.0f));
    ++m_currentStats.renderedObjects;
}

void MapRenderer::Flush() {
    m_batch.Flush();
}

void MapRenderer::RenderCollisionOverlay(Map& map) {
    // 叠加四边形借用任意已注册 Tile 纹理作为采样源
    const auto baseIt = m_tileTextures.find(static_cast<uint16_t>(TileId::Grass));
    if (baseIt == m_tileTextures.end() || !baseIt->second) {
        return;
    }

    const float tileSize = static_cast<float>(map.GetTileSize());

    const int tx0 = std::max(0, WorldToTileIndex(m_viewLeft, tileSize));
    const int ty0 = std::max(0, WorldToTileIndex(m_viewTop, tileSize));
    const int tx1 = std::min(map.GetWidth() - 1, WorldToTileIndex(m_viewRight, tileSize));
    const int ty1 = std::min(map.GetHeight() - 1, WorldToTileIndex(m_viewBottom, tileSize));

    const legend::math::Color blockedColor(1.0f, 0.0f, 0.0f, 0.35f);
    int count = 0;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            // F1 显示最终真实阻挡结果：Terrain / Manual / Object 三源合成
            if (!map.IsTileBlocked(tx, ty)) {
                continue;
            }
            const float cx = TileToWorldCenter(tx, tileSize);
            const float cy = TileToWorldCenter(ty, tileSize);
            m_batch.DrawQuad(*baseIt->second, {cx, cy}, {1.0f, 1.0f}, 0.0f, blockedColor);
            ++count;
        }
    }
}

bool MapRenderer::IsObjectVisible(const MapObject& object, float margin) const {
    const float halfW = object.width * 0.5f;
    const float halfH = object.height * 0.5f;
    return object.x + halfW >= m_viewLeft - margin && object.x - halfW <= m_viewRight + margin &&
           object.y + halfH >= m_viewTop - margin && object.y - halfH <= m_viewBottom + margin;
}

bool MapRenderer::YSortCompare(const MapObject* a, const MapObject* b) {
    // 1. 分层：不同 sortLayer 之间不参与 bottomY 互比
    if (a->sortLayer != b->sortLayer) {
        return a->sortLayer < b->sortLayer;
    }
    // 2. 同层严格按底部 Y：bottomY 小的先画（在后面），大的后画（在前/遮挡）
    if (a->GetBottomY() != b->GetBottomY()) {
        return a->GetBottomY() < b->GetBottomY();
    }
    // 3. bottomY 完全相同时才用 renderOrder 平局判定
    return a->renderOrder < b->renderOrder;
}

void MapRenderer::EndFrame() {
    m_batch.End();
    m_currentStats.drawCalls = m_batch.GetDrawCallCount();
    m_lastStats = m_currentStats;
}

} // namespace legend::map
