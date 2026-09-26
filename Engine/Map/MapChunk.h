#pragma once

namespace legend::map {

// 渲染缓存单元：16x16 Tile 组成一个 Chunk（1024x1024 world units，Tile=64）
class MapChunk {
public:
    static constexpr int kSize = 16;

    MapChunk() = default;
    MapChunk(int chunkX, int chunkY) : m_chunkX(chunkX), m_chunkY(chunkY) {}

    int GetChunkX() const { return m_chunkX; }
    int GetChunkY() const { return m_chunkY; }

    bool IsDirty() const { return m_dirty; }
    void SetDirty(bool dirty) { m_dirty = dirty; }

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

    // 该 Chunk 覆盖的 Tile 范围（闭区间起点）
    int GetStartTileX() const { return m_chunkX * kSize; }
    int GetStartTileY() const { return m_chunkY * kSize; }

private:
    int m_chunkX = 0;
    int m_chunkY = 0;
    bool m_dirty = true;    // 数据变化后需要重建渲染缓存
    bool m_visible = false; // 上一帧是否可见
};

} // namespace legend::map
